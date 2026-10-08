/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <optional>
#include <vector>

namespace Test {

class Runner;

// A bounded, logged clipboard round trip: write a sentinel the caller
// labels, read it back, and retry a fixed number of times with a delay
// between attempts, so a transient host lock is told apart from a
// clipboard that never reads back.
//
// Why it exists. In Attempt 1 Run 1 of
// 2026/10/07/show-url-tooltips-and-copy-link-on-rich-message-url-buttons
// (work/test.md) the clipboard premise was a single round trip, and it read
// back empty. The app log carried "OleSetClipboard: Failed to set mime data
// (text/plain) on clipboard: COM error 0x800401d0 (OpenClipboard Failed)",
// then Qt's own "Retrying to obtain clipboard." and "Unable to obtain
// clipboard." for about 0.9 s, and the clipboard read fine again later
// (Get-Clipboard). That one lock left every clipboard oracle of the run
// undecided. Run 2 retried by hand, up to 10 attempts 300 ms apart, and its
// premise read ok:1 attempts:1; those are the defaults here. Run 2's
// hand-written loop also Noted the raw text it read back on every attempt,
// which this helper never does (Privacy, below).
//
// It is a different symptom from README.md's unreadable-clipboard row in
// "## Failure diagnosis", observed in
// 2026/09/29/keep-the-focused-word-field-in-view-when-importing-a-wallet,
// where the clipboard read back empty right after setText with an
// undiagnosed host-level cause. A transient lock reads its sentinel back at
// a later attempt; the persistent case ends in a named refusal after the
// bound.
//
// Yielding, never sleeping. AppendClipboardRoundTrip appends ONE stage.
// Attempt 1 runs in that stage's |run|, so a clipboard that reads back at
// once costs no turn. Every later attempt runs from a Qt::PreciseTimer
// base::Timer |delay| ms after the previous attempt ended (re-armed when it
// fires early), so the event loop turns between attempts, and a queued
// marker posted at the end of each failed attempt records in
// ClipboardAttempt::yielded that it really did. |until| only reads
// |finished|, the stage's |then| Notes the CLIPBOARD_ROUND_TRIP summary, and
// its timeout is kDefaultStageTimeout plus, per attempt, the delay and one
// second for Qt's own retries inside the attempt. The reason to yield is
// documented Win32 / OLE behaviour, not something the source run observed:
// OleSetClipboard publishes its formats with delayed rendering through
// OLE's clipboard window on the calling thread, so a clipboard listener in
// another process that reads our previous write sends WM_RENDERFORMAT to
// our main thread and holds the clipboard open until that thread pumps
// messages. A main thread sleeping in QThread::msleep - Run 2's idiom -
// never pumps, so such a lock can last exactly as long as the sleep. Qt's
// own retries are short and do sleep the main thread (qwindowsclipboard.cpp):
// OleSetClipboard three times 100 ms apart on CLIPBRD_E_CANT_OPEN, and
// OleGetClipboard three times 50 ms apart. ClipboardAttempt::writeMs shows
// how long a write spent inside them.
//
// What a success proves. On Windows QWindowsClipboard::mimeData() serves a
// read in process, from the data this process set, while ownsClipboard()
// holds (OleIsCurrentClipboard), with no OS access at all. So a successful
// round trip there proves that the set took and the process still owns the
// clipboard, not that an OS-level read works. |owns| is recorded on every
// attempt and in every ReadClipboard, printed with
// QGuiApplication::platformName(): it is meaningful on Windows, macOS and
// X11, and QPlatformClipboard::ownsMode() answers false on every platform
// plugin that does not implement it.
//
// Privacy. A read is only ever classified - empty, the expected text (the
// attempt's own sentinel), a sentinel this harness wrote earlier, or
// foreign - inside a scope the read text never leaves. Every sentinel is
// registered in a process-wide registry before it is written, and only a
// registered sentinel is ever stored or printed. Foreign text is never
// stored, printed, measured (no length) or hashed, because the owner's
// clipboard can hold secrets (test_text_drop.h). A sentinel an earlier
// process wrote counts as foreign. The helper overwrites the owner's
// clipboard and never saves or restores it: README.md's unreadable-clipboard
// row explains why restoring a clipboard that read back empty writes the
// empty reading back, which clears it.
//
// Sentinels read harness-clipboard:<label>:<serial>:a<attempt>, and every
// attempt is Noted as one row starting
// "CLIPBOARD_ROUND_TRIP_ATTEMPT: label=<label> ", so keep the label one
// token without spaces. The helper never Passes or Fails: it returns the
// reading, complete once its stage has completed, and the caller judges it
// in a later stage. It releases its timer and marker through
// Runner::onFinish.
inline constexpr auto kClipboardRoundTripAttempts = 10;
inline constexpr auto kClipboardRoundTripDelay = crl::time(300);

// The injection seam. |write| and |read| are required: an accessor missing
// either is replaced by SystemClipboardAccessor(). |owns| is optional, and
// a reading taken without it prints owns=n/a. |name| is printed in every
// row the reading produces.
struct ClipboardAccessor {
	QString name;
	Fn<void(const QString &text)> write;
	Fn<QString()> read;
	Fn<bool()> owns;
};

// "system": QGuiApplication::clipboard()'s setText, text and
// ownsClipboard.
[[nodiscard]] ClipboardAccessor SystemClipboardAccessor();

// What one read held. HarnessSentinel is a sentinel this process wrote that
// is not the expected text; Foreign is any other non-empty text.
enum class ClipboardContent {
	Empty,
	Expected,
	HarnessSentinel,
	Foreign,
};

[[nodiscard]] QString ClipboardContentName(ClipboardContent content);

// One attempt. |sentinel| is what it wrote. |heldSentinel| is the earlier
// harness sentinel a HarnessSentinel read held, and stays empty for every
// other content. |owns| is empty when the accessor has no owns reading.
// |yielded| is empty on attempt 1 and otherwise says whether a queued call
// posted after the previous attempt had run before this one, that is,
// whether the event loop turned in between. |atMs| is when the attempt
// began, counted from the round trip's start; |writeMs| is how long its
// write call took. |index| is 1-based.
struct ClipboardAttempt {
	QString sentinel;
	QString heldSentinel;
	std::optional<bool> owns;
	std::optional<bool> yielded;
	crl::time atMs = 0;
	crl::time writeMs = 0;
	ClipboardContent content = ClipboardContent::Empty;
	int index = 0;
};

// Read-* names what the LAST read held when no attempt read its own
// sentinel back within the bound. InvalidBound: fewer than one attempt or
// a negative delay, and nothing was written.
//
// A read-* refusal's |reason| ends with "; console at refusal: " and the
// Test::ConsoleLockText(Test::ReadConsoleLock()) row read at that moment
// (test_console_lock.h): the host console's state, printed for every
// accessor, fakes included, and never a cause on its own. On a locked
// Windows console no process on its desktop can open the clipboard, so a
// round trip ends in read-empty after the whole bound, as in Attempt 1
// Runs 1 and 2 of
// 2026/10/07/add-a-same-turn-context-menu-reader-and-a-retried-clipboard-round-trip-to-the-test-harness
// (work/test.md), whose rows never named the lock. A success and
// invalid-bound carry no reading.
enum class ClipboardRefusal {
	None,
	InvalidBound,
	ReadEmpty,
	ReadStaleSentinel,
	ReadForeign,
};

[[nodiscard]] QString ClipboardRefusalName(ClipboardRefusal refusal);

// |succeededAt| is the 1-based attempt that read its own sentinel back, and
// 0 without one. |finished| turns true when the attempt that ends the round
// trip returns; the stage's |until| reads exactly that. |bound| and |delay|
// are the arguments, filled when the stage is appended, so a timeout's
// details print them before the first attempt.
struct ClipboardRoundTrip {
	QString label;
	QString accessor;
	QString platform;
	std::vector<ClipboardAttempt> attempts;
	QString reason;
	crl::time delay = 0;
	crl::time elapsedMs = 0;
	ClipboardRefusal refusal = ClipboardRefusal::None;
	int bound = 0;
	int succeededAt = 0;
	bool finished = false;
	bool ok = false;
};

// An |accessor| left empty is the system clipboard.
struct ClipboardRoundTripArgs {
	QString label;
	ClipboardAccessor accessor;
	crl::time delay = kClipboardRoundTripDelay;
	int attempts = kClipboardRoundTripAttempts;
};

// Appends the stage "clipboard round trip: <label>" and returns the reading
// it fills. Read it from a later stage.
[[nodiscard]] auto AppendClipboardRoundTrip(
	not_null<Runner*> runner,
	ClipboardRoundTripArgs args)
-> std::shared_ptr<const ClipboardRoundTrip>;

// The row the helper Notes once per attempt:
// "CLIPBOARD_ROUND_TRIP_ATTEMPT: label=.. accessor=.. platform=..
// attempt=<k>/<bound> sentinel=.. read=<what> owns=<1|0|n/a>
// yielded=<1|0|first> atMs=.. writeMs=.. ok=<0|1>", where <what> is
// sentinel (its own, read back), empty, stale-sentinel:<held> or foreign.
[[nodiscard]] QString ClipboardAttemptText(
	const ClipboardRoundTrip &trip,
	const ClipboardAttempt &attempt);

// "CLIPBOARD_ROUND_TRIP: label=.. finished=.. ok=.. succeededAt=..
// attemptsMade=.. bound=.. delayMs=.. elapsedMs=.. refusal=<name>" and
// " - <reason>"; finished=0 while the round trip is pending. A read-*
// refusal's reason ends with "; console at refusal: CONSOLE_LOCK: ..."
// (ClipboardRefusal, above).
[[nodiscard]] QString ClipboardRoundTripText(const ClipboardRoundTrip &trip);

// One read of the clipboard, classified against |expected|, for judging a
// product copy. It is a single pure reading and deliberately not a wait:
// a wait until the clipboard equals |expected| would put the product's
// outcome into a stage's |until|, which the stage contract forbids, and
// Test::TriggerContextMenuAction (test_menu.h) already delivers a copy
// entry's queued Ui::Menu::CreateAction callback before it returns, so the
// product's write has happened when the caller reads. Take a round trip
// first as the premise that this host's clipboard reads back at all. On
// Windows owns=0 right after a product copy means the copy's set failed or
// another process wrote after it. An accessor without |read| is the system
// clipboard. ClipboardReadingText prints neither |expected| nor the read
// text - the caller's Check prints its own expectation - and a
// harness-sentinel reading names the sentinel it held.
struct ClipboardReading {
	QString accessor;
	QString platform;
	QString heldSentinel;
	std::optional<bool> owns;
	ClipboardContent content = ClipboardContent::Empty;

	[[nodiscard]] bool matches() const {
		return (content == ClipboardContent::Expected);
	}
};

[[nodiscard]] ClipboardReading ReadClipboard(
	const QString &expected,
	const ClipboardAccessor &accessor = {});
[[nodiscard]] QString ClipboardReadingText(const ClipboardReading &reading);

// The round trip and ReadClipboard measuring themselves, through injected
// accessors except for one live arm. Its first stage marks the end of
// test_log.txt, and every row count and the final scan read only the lines
// appended after that mark, so a reused evidence directory cannot double a
// count. The arms, in order:
// - fail three: reads answer empty, the arm's own first sentinel (a stale
//   harness sentinel), empty, then the written sentinel, 40 ms apart. It
//   reads back at attempt 4 with exactly 4 rows, names what each failed
//   read held, records the injected owns as 0, 0, 0, 1, every later attempt
//   yielded and began at least the delay after the previous one, and every
//   attempt wrote its own labelled sentinel;
// - always empty, with the shipped defaults: refused read-empty after
//   exactly kClipboardRoundTripAttempts attempts and rows, no sooner than
//   nine delays. Its refusal's reason carries the console-lock reading,
//   and the fail-three arm's success carries none;
// - stale: two attempts reading the fail-three arm's first sentinel,
//   refused read-stale-sentinel naming it;
// - foreign: three attempts reading an injected foreign text, refused
//   read-foreign with no held sentinel, and no formatter output carries
//   that text;
// - ReadClipboard over fakes: expected, empty, harness-sentinel naming the
//   sentinel, and foreign without the text;
// - live: one round trip against the system clipboard with the defaults,
//   then ReadClipboard of the sentinel it wrote. It overwrites the owner's
//   clipboard and never restores it. On a host whose clipboard never reads
//   back it FAILs by name: a host property, so rerun, with no code change.
//   On a locked Windows console (Test::ReadConsoleLock() reads locked when
//   the judging stage begins) a round trip that did not read back is
//   TEST_RESULT: N/A instead, naming the lock and printing that reading -
//   rerun on an unlocked console - while every injected-accessor arm and
//   the scan below still run and decide. The judging stage Notes the
//   CONSOLE_LOCK row it read either way;
// - last, a Test::DiscriminatingScan over the appended lines: the injected
//   foreign text is in none of them, with the foreign arm's own three
//   attempt rows as the control the same walk must match.
// It needs no session, network, account fixture or widget. It emits no
// deliberate failure - every refusal is asserted as a passing Check whose
// details carry the refused reading - and it has no teardown stage: every
// round trip registered its own Runner::onFinish release.
void AppendClipboardRoundTripSelfTest(not_null<Runner*> runner);

} // namespace Test
