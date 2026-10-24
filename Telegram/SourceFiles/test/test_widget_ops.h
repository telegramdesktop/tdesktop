/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"
#include "twidget/renderer.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <memory>
#include <optional>
#include <string>
#include <vector>

// The validated-ops tap of a widget message's sandbox host: what
// twidget::WidgetHost sends the product renderer, scanned with widget-sandbox's
// --attacks leak definition and read for the conformance result.
// Reference: docs/helpers/test_widget_ops.md.

namespace Test {

// The renderer factory Tgw::Runtime::construct() builds its widget host
// with, for the message |id|: its HistoryItem::globalId(), the FullMsgId
// and its session's uniqueId(), because each session allocates local
// message ids on its own and two sessions can hold different messages with
// the same FullMsgId. Returns |factory| itself, untouched, unless Active()
// and |id| is armed by ArmWidgetOpsTap(). For an armed id it returns a
// factory that calls |factory| exactly once, with the same arguments, on
// its caller's thread, and wraps the renderer that call returns in a tap
// that forwards every call and callback directly. Main thread. The Release
// branch of this module defines this function alone, as that identity.
[[nodiscard]] twidget::RendererFactory TapWidgetRenderer(
	GlobalMsgId id,
	twidget::RendererFactory factory);

// Everything below is Debug-only: declared here and deliberately left
// undefined in a Release build, because no production code reads it.

// One armed message's record, shared by the main thread, the host's Build
// worker, its host thread and its deleter worker. Opaque; read it with
// ReadWidgetOps().
class WidgetOpsTap;

// A snapshot of one tap. Every count covers every renderer built for the
// message while it was armed and is never reset, except the trusted event
// fields, which count from the last MarkWidgetOpsEvents(). Counts are
// uncapped; every example list keeps only its first 64 entries.
struct WidgetOpsReading {
	// The --attacks scanner's own leak and note texts.
	std::vector<std::string> leakExamples;
	std::vector<std::string> noteExamples;
	// The reason and the first 160 bytes of each scan-gap message.
	std::vector<std::string> gapExamples;
	// The `detail` of each renderer `violation` message, never a leak.
	std::vector<std::string> violationDetails;
	// The `ev.type` of each trusted renderer `event` since the mark.
	std::vector<std::string> trustedEventTypes;

	// Wrapping factories handed out by TapWidgetRenderer() on main.
	int factoriesSelected = 0;
	// Product factory calls that returned or threw; the exception of one
	// that threw is rethrown to WidgetHost after it is counted.
	int factoriesCompleted = 0;
	int factoriesThrew = 0;
	// Wrapping factories whose last copy is gone, called or not: none of
	// them can build a renderer any more.
	int factoriesReleased = 0;
	// Wrappers built, and destroyed only after their product renderer.
	int wrappersBuilt = 0;
	int wrappersDestroyed = 0;

	// Host -> renderer messages, every type.
	int hostMessages = 0;
	// `ops` messages with an `ops` array, each scanned before forwarding,
	// and the ops in them.
	int batches = 0;
	int ops = 0;
	// Host messages that are not JSON, or `ops` messages whose `ops` is not
	// an array: forwarded unchanged, but not scanned.
	int scanGaps = 0;
	int leaks = 0;
	int notes = 0;

	// Renderer -> host `violation` messages.
	int violations = 0;
	// Renderer -> host `event` messages marked `trusted`, and those of them
	// whose `ev.type` is "click", since the last MarkWidgetOpsEvents().
	int trustedEvents = 0;
	int trustedClicks = 0;

	// The host's post-load ack (`query` with numeric `qid` -1), the
	// renderer's answer to it (`result` with numeric `qid` -1) and the
	// host's death notice (`frozen` with `fatal` true).
	int ackQueries = 0;
	int ackResults = 0;
	int fatalFrozen = 0;
};

// What the newest host selected for one tap was last told: its widget's
// result, read as CLIENTS.md's "Reading the ops" does, and the scheme and
// theme its renderer got. Every field but `serial` and `staleUpdates`
// describes the host of `generation` alone and restarts with a newer one.
struct WidgetOpsLatest {
	// The last `data-conformance` value of the adopted <html>: set by the
	// adopting `el` op's attribute, replaced by each later `attr` op on that
	// id. Raw attribute text, not checked to be JSON. Empty before the first
	// value, once a newer host is selected, and after a later adoption
	// without one; a `rattr` is ignored, as CLIENTS.md's reader does.
	std::optional<std::string> conformance;
	// The raw `{"type":"theme",...}` host message last sent to its renderer.
	std::string themeMessage;
	// The scheme its renderer was given: the factory's theme argument, then
	// each setTheme(). Empty until its wrapper is built.
	std::string scheme;
	// The id of the adopting `el` op, while `adopted`.
	int64 htmlId = 0;
	// The 1-based count of wrapping factories selected for the message when
	// this host's was: above 1, more than one host was constructed for it,
	// and these fields, like a runtime's log, cover only the last.
	int generation = 0;
	// Values this host published, the probe fixture's `updates`.
	int updates = 0;
	// Advances whenever `conformance` changes, a clear included; never reset.
	int serial = 0;
	// setTheme() calls of this host that changed `scheme`.
	int schemeChanges = 0;
	// Host messages with an adoption, a `data-conformance` attribute or a
	// theme, setTheme() calls and builds of an older host's wrapper after a
	// newer host was selected: ignored above, counted here; never reset.
	int staleUpdates = 0;
	bool adopted = false;
};

// Main thread. Arms |id|, the session-qualified id the message's
// HistoryItem::globalId() will return, for every host constructed for it
// from now on and returns its tap; arming an armed id returns the tap it
// already has. A message of another session with the same FullMsgId stays
// unarmed, and gets a tap of its own only when its own id is armed.
[[nodiscard]] std::shared_ptr<WidgetOpsTap> ArmWidgetOpsTap(GlobalMsgId id);

// Main thread. Later constructions for |id| get the product factory itself;
// factories already handed out and the wrappers they built keep the tap.
// Pass the id that was armed, as kept by the caller, not one rebuilt later
// from whichever session is active then; no other id is touched.
void DisarmWidgetOpsTap(GlobalMsgId id);

// Any thread. No factory call and no wrapper of the tap is still pending
// once factoriesReleased == factoriesSelected and wrappersDestroyed ==
// wrappersBuilt; only a reading taken after that is final.
[[nodiscard]] WidgetOpsReading ReadWidgetOps(const WidgetOpsTap &tap);

// Any thread. A snapshot taken under the tap's lock; a caller that parses
// `conformance` can cache the result by `generation` and `serial`.
[[nodiscard]] WidgetOpsLatest ReadWidgetOpsLatest(const WidgetOpsTap &tap);

// Any thread. Restarts the trusted event fields, right before a click.
void MarkWidgetOpsEvents(WidgetOpsTap &tap);

struct WidgetOpsSelfCheckRow {
	QString name;
	QString details;
	bool passed = false;
};

struct WidgetOpsSelfCheck {
	std::vector<WidgetOpsSelfCheckRow> rows;
	// The control tap's final reading, after its factory and wrapper are gone.
	WidgetOpsReading control;
};

// Main thread, synchronous, needs Active(). Drives TapWidgetRenderer()
// with a stub product renderer under message ids of peer 0, which no real
// message has in any session: unarmed identity, armed wrapping and
// forwarding with exact bytes, order and threads, the scanner's 2-leak,
// 1-note control followed by a removal batch, scan gaps, the ack, frozen,
// violation and trusted-event readings, a throwing product factory, normal
// destruction, and one FullMsgId in two sessions (only the armed one
// wrapped, separate readings when both are armed, disarming one leaves the
// other). Disarms its ids before it returns. The caller turns each row
// into a Check().
[[nodiscard]] WidgetOpsSelfCheck RunWidgetOpsSelfCheck();

// Main thread, synchronous, needs Active(). |fixture| is the bytes of
// widget-sandbox's packages/conformance/probe-fixture.json, which gives each
// case's expected values. Feeds each case's messages through a wrapper that
// TapWidgetRenderer() builds around a stub product renderer under message
// ids of peer 0 and compares ReadWidgetOpsLatest() with the case's `expect`:
// htmlId, updates, final and awaitingTargets. Two legs with literal
// expectations follow: a replacement host (a newer selection clears the
// result; a retiring wrapper's late result, theme and setTheme() are
// ignored but its batches scanned; a later adoption without a value clears
// it) and the factory theme, setTheme() and theme message. Disarms its ids
// before it returns. The caller turns each row into a Check().
[[nodiscard]] auto RunWidgetConformanceProbeCheck(const QByteArray &fixture)
-> std::vector<WidgetOpsSelfCheckRow>;

} // namespace Test
