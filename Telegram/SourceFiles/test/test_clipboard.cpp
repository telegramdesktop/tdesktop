/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_clipboard.h"

#include "base/flat_set.h"
#include "base/invoke_queued.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "test/test_console_lock.h"
#include "test/test_log.h"
#include "test/test_probe.h"
#include "test/test_runner.h"

#include <QtCore/QFile>
#include <QtCore/QStringList>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Test {
namespace {

// One attempt's worst case beyond its delay: Qt's own OleSetClipboard retry
// sleeps up to 3 x 100 ms inside the write and its OleGetClipboard retry up
// to 3 x 50 ms inside the read (qwindowsclipboard.cpp), with room to spare.
constexpr auto kClipboardAttemptAllowance = crl::time(1000);

// The self-test's fakes and labels. kForeign stands in for an owner's
// clipboard: it is never placed in a Check's what or details or in a Note,
// and the self-test refers to it only as "the injected foreign text".
const auto kForeign = u"tdesktop-foreign-7c1f0e94-never-logged"_q;
const auto kExpectedCopy = u"harness expected copy"_q;
const auto kFailThreeLabel = u"self-test-fail-three"_q;
const auto kAlwaysEmptyLabel = u"self-test-always-empty"_q;
const auto kStaleLabel = u"self-test-stale"_q;
const auto kForeignLabel = u"self-test-foreign"_q;
const auto kLiveLabel = u"self-test-live"_q;
constexpr auto kSelfTestDelay = crl::time(40);
constexpr auto kFailThreeAttempts = 4;
constexpr auto kStaleAttempts = 2;
constexpr auto kForeignAttempts = 3;

// Everything one appended round trip owns. The timer and the marker hold
// the driver by a plain pointer: the driver owns both, so neither can
// outlive it, and the marker's destruction drops a queued call still
// addressed to it.
struct RoundTripDriver {
	ClipboardRoundTripArgs args;
	std::shared_ptr<ClipboardRoundTrip> trip;
	base::Timer timer;
	base::unique_qptr<QObject> marker;
	crl::time started = 0;
	crl::time nextAt = 0;
	int serial = 0;
	bool markerRan = false;
};

// An injected clipboard: |answer| decides what each read returns from the
// fake's own counters, and |sentinels| keeps every text written to it.
struct FakeClipboard {
	QStringList sentinels;
	QString written;
	Fn<QString(not_null<FakeClipboard*>)> answer;
	int writes = 0;
	int reads = 0;
};

struct ClipboardFakes {
	std::shared_ptr<FakeClipboard> failThree;
	std::shared_ptr<FakeClipboard> alwaysEmpty;
	std::shared_ptr<FakeClipboard> stale;
	std::shared_ptr<FakeClipboard> foreign;
	std::shared_ptr<FakeClipboard> expected;
};

struct ClipboardSelfTest {
	ConsoleLockReading liveConsole;
	qint64 logMark = 0;
};

[[nodiscard]] base::flat_set<QString> &HarnessSentinels() {
	static auto result = base::flat_set<QString>();
	return result;
}

[[nodiscard]] int &RoundTripSerial() {
	static auto result = 0;
	return result;
}

// |read| is compared, looked up and dropped: only a registered harness
// sentinel is ever copied out of it, into |held|.
[[nodiscard]] ClipboardContent Classify(
		const QString &read,
		const QString &expected,
		not_null<QString*> held) {
	if (read.isEmpty()) {
		return ClipboardContent::Empty;
	} else if (!expected.isEmpty() && (read == expected)) {
		return ClipboardContent::Expected;
	} else if (HarnessSentinels().contains(read)) {
		*held = read;
		return ClipboardContent::HarnessSentinel;
	}
	return ClipboardContent::Foreign;
}

[[nodiscard]] QString OwnsText(std::optional<bool> owns) {
	return !owns ? u"n/a"_q : *owns ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString YieldedText(std::optional<bool> yielded) {
	return !yielded ? u"first"_q : *yielded ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString ReadText(const ClipboardAttempt &attempt) {
	switch (attempt.content) {
	case ClipboardContent::Empty: return u"empty"_q;
	case ClipboardContent::Expected: return u"sentinel"_q;
	case ClipboardContent::HarnessSentinel:
		return u"stale-sentinel:"_q + attempt.heldSentinel;
	case ClipboardContent::Foreign: return u"foreign"_q;
	}
	Unexpected("Content in Test::ReadText.");
}

[[nodiscard]] QString ContentText(
		ClipboardContent content,
		const QString &heldSentinel) {
	return heldSentinel.isEmpty()
		? ClipboardContentName(content)
		: (ClipboardContentName(content) + QChar(':') + heldSentinel);
}

[[nodiscard]] ClipboardRefusal RefusalFor(ClipboardContent content) {
	switch (content) {
	case ClipboardContent::Empty: return ClipboardRefusal::ReadEmpty;
	case ClipboardContent::HarnessSentinel:
		return ClipboardRefusal::ReadStaleSentinel;
	case ClipboardContent::Foreign: return ClipboardRefusal::ReadForeign;
	case ClipboardContent::Expected: break;
	}
	Unexpected("Content in Test::RefusalFor.");
}

[[nodiscard]] QString RefusalReason(
		const ClipboardRoundTrip &trip,
		const ClipboardAttempt &last) {
	// Read at refusal time, so a campaign's own refusal names a locked console.
	const auto console = u"; console at refusal: "_q
		+ ConsoleLockText(ReadConsoleLock());
	const auto head = u"no attempt read its own sentinel back within %1 "
		u"attempts %2 ms apart; "_q.arg(
			QString::number(trip.bound),
			QString::number(trip.delay));
	switch (last.content) {
	case ClipboardContent::Empty:
		return head + u"the last read was empty"_q + console;
	case ClipboardContent::HarnessSentinel:
		return head
			+ u"the last read held %1, a sentinel this harness wrote "
			u"earlier, not attempt %2's own"_q.arg(
				last.heldSentinel,
				QString::number(last.index))
			+ console;
	case ClipboardContent::Foreign:
		return head
			+ u"the last read held text this harness did not write, which "
			u"is never printed"_q
			+ console;
	case ClipboardContent::Expected: break;
	}
	Unexpected("Content in Test::RefusalReason.");
}

void FinishRoundTrip(
		not_null<RoundTripDriver*> driver,
		ClipboardRefusal refusal,
		QString reason) {
	const auto trip = driver->trip.get();
	trip->finished = true;
	trip->ok = (refusal == ClipboardRefusal::None);
	trip->refusal = refusal;
	trip->reason = std::move(reason);
	trip->elapsedMs = crl::now() - driver->started;
	driver->timer.cancel();
}

void Attempt(not_null<RoundTripDriver*> driver) {
	const auto trip = driver->trip.get();
	const auto &accessor = driver->args.accessor;
	auto attempt = ClipboardAttempt();
	attempt.index = int(trip->attempts.size()) + 1;
	attempt.sentinel = u"harness-clipboard:%1:%2:a%3"_q.arg(
		trip->label,
		QString::number(driver->serial),
		QString::number(attempt.index));

	// Registered before the write, so a read of it can never be foreign.
	HarnessSentinels().emplace(attempt.sentinel);
	if (attempt.index > 1) {
		attempt.yielded = base::take(driver->markerRan);
	}
	const auto writeAt = crl::now();
	attempt.atMs = writeAt - driver->started;
	accessor.write(attempt.sentinel);
	attempt.writeMs = crl::now() - writeAt;
	{
		// The read text never leaves this scope.
		const auto read = accessor.read();
		attempt.content = Classify(
			read,
			attempt.sentinel,
			&attempt.heldSentinel);
	}
	if (accessor.owns) {
		attempt.owns = accessor.owns();
	}
	trip->attempts.push_back(attempt);
	Note(ClipboardAttemptText(*trip, attempt));

	if (attempt.content == ClipboardContent::Expected) {
		trip->succeededAt = attempt.index;
		FinishRoundTrip(driver, ClipboardRefusal::None, QString());
		return;
	} else if (attempt.index >= trip->bound) {
		FinishRoundTrip(
			driver,
			RefusalFor(attempt.content),
			RefusalReason(*trip, attempt));
		return;
	}
	const auto raw = driver.get();
	driver->markerRan = false;
	InvokeQueued(driver->marker.get(), [raw] {
		raw->markerRan = true;
	});
	driver->nextAt = crl::now() + driver->args.delay;
	driver->timer.callOnce(driver->args.delay, Qt::PreciseTimer);
}

void OnRetryTimer(not_null<RoundTripDriver*> driver) {
	if (driver->trip->finished) {
		return;
	}
	const auto now = crl::now();
	if (now < driver->nextAt) {
		driver->timer.callOnce(driver->nextAt - now, Qt::PreciseTimer);
		return;
	}
	Attempt(driver);
}

void StartRoundTrip(not_null<RoundTripDriver*> driver) {
	const auto &args = driver->args;
	driver->started = crl::now();
	if (args.attempts < 1 || args.delay < 0) {
		FinishRoundTrip(
			driver,
			ClipboardRefusal::InvalidBound,
			u"attempts=%1 delay=%2: a round trip needs at least one attempt "
			u"and a non-negative delay; nothing was written"_q.arg(
				QString::number(args.attempts),
				QString::number(args.delay)));
		return;
	}
	const auto raw = driver.get();
	driver->serial = ++RoundTripSerial();
	driver->marker = base::make_unique_q<QObject>();
	driver->timer.setCallback([raw] {
		OnRetryTimer(raw);
	});
	Attempt(driver);
}

// Idempotent: Runner::onFinish runs it on every path that finishes.
void ReleaseRoundTrip(not_null<RoundTripDriver*> driver) {
	driver->timer.cancel();
	driver->marker = nullptr;
}

[[nodiscard]] ClipboardAccessor FakeAccessor(
		QString name,
		std::shared_ptr<FakeClipboard> fake,
		bool withOwns) {
	auto result = ClipboardAccessor{
		.name = std::move(name),
		.write = [=](const QString &text) {
			++fake->writes;
			fake->written = text;
			fake->sentinels.push_back(text);
		},
		.read = [=] {
			++fake->reads;
			return fake->answer(fake.get());
		},
	};
	if (withOwns) {
		// Not owned before the fourth read, so the fail-three arm records
		// owns 0, 0, 0, 1 and every other fake prints owns=n/a.
		result.owns = [=] {
			return (fake->reads >= kFailThreeAttempts);
		};
	}
	return result;
}

[[nodiscard]] ClipboardFakes MakeFakes() {
	auto result = ClipboardFakes{
		.failThree = std::make_shared<FakeClipboard>(),
		.alwaysEmpty = std::make_shared<FakeClipboard>(),
		.stale = std::make_shared<FakeClipboard>(),
		.foreign = std::make_shared<FakeClipboard>(),
		.expected = std::make_shared<FakeClipboard>(),
	};

	// Read 1 empty, read 2 the arm's own first sentinel (a stale harness
	// sentinel), read 3 empty, and from read 4 on the text just written.
	result.failThree->answer = [](not_null<FakeClipboard*> fake) -> QString {
		if (fake->reads == 2) {
			return fake->sentinels.front();
		} else if (fake->reads < kFailThreeAttempts) {
			return QString();
		}
		return fake->written;
	};
	result.alwaysEmpty->answer = [](not_null<FakeClipboard*>) {
		return QString();
	};

	// The fail-three arm's first sentinel, once that arm has written it.
	const auto failThree = result.failThree;
	result.stale->answer = [=](not_null<FakeClipboard*>) {
		return failThree->sentinels.isEmpty()
			? QString()
			: failThree->sentinels.front();
	};
	result.foreign->answer = [](not_null<FakeClipboard*>) {
		return kForeign;
	};
	result.expected->answer = [](not_null<FakeClipboard*>) {
		return kExpectedCopy;
	};
	return result;
}

[[nodiscard]] QString TestLogPath() {
	return EvidenceDir() + u"test_log.txt"_q;
}

[[nodiscard]] qint64 TestLogSize() {
	return QFile(TestLogPath()).size();
}

// The raw bytes appended from |from| on, decoded as UTF-8: the idiom of
// AppendedSince in test_log_lines.cpp, for the same reason - no
// QIODevice::Text, so nothing the writer produced is translated away.
// Reading only what was appended after the self-test's mark keeps a reused
// evidence directory from doubling a count.
[[nodiscard]] QString TestLogSince(qint64 from) {
	auto file = QFile(TestLogPath());
	if (!file.open(QIODevice::ReadOnly) || !file.seek(from)) {
		return QString();
	}
	return QString::fromUtf8(file.readAll());
}

// The writer opens test_log.txt in text mode, so a Windows line ends in
// CRLF; the CR is dropped here, and so is the empty piece after the last
// terminator.
[[nodiscard]] QStringList TestLogLinesSince(qint64 from) {
	auto result = TestLogSince(from).split(QChar('\n'));
	for (auto &line : result) {
		if (line.endsWith(QChar('\r'))) {
			line.chop(1);
		}
	}
	if (!result.isEmpty() && result.back().isEmpty()) {
		result.pop_back();
	}
	return result;
}

[[nodiscard]] QString AttemptRowPrefix(const QString &label) {
	return u"NOTE: CLIPBOARD_ROUND_TRIP_ATTEMPT: label=%1 "_q.arg(label);
}

[[nodiscard]] int CountAttemptRows(qint64 from, const QString &label) {
	const auto prefix = AttemptRowPrefix(label);
	const auto lines = TestLogLinesSince(from);
	return int(ranges::count_if(lines, [&](const QString &line) {
		return line.startsWith(prefix);
	}));
}

[[nodiscard]] QString AttemptList(
		const ClipboardRoundTrip &trip,
		Fn<QString(const ClipboardAttempt &)> field) {
	auto parts = QStringList();
	for (const auto &attempt : trip.attempts) {
		parts.push_back(field(attempt));
	}
	return u"[%1]"_q.arg(parts.join(u", "_q));
}

[[nodiscard]] std::vector<ClipboardContent> ContentsOf(
		const ClipboardRoundTrip &trip) {
	auto result = std::vector<ClipboardContent>();
	for (const auto &attempt : trip.attempts) {
		result.push_back(attempt.content);
	}
	return result;
}

[[nodiscard]] std::vector<std::optional<bool>> OwnsOf(
		const ClipboardRoundTrip &trip) {
	auto result = std::vector<std::optional<bool>>();
	for (const auto &attempt : trip.attempts) {
		result.push_back(attempt.owns);
	}
	return result;
}

// Every value a self-test arm is judged on, harness-written only: the
// summary, the fake's counters, the rows counted since the mark, and per
// attempt the classified content, owns, yielded, start time and sentinel.
[[nodiscard]] QString ArmDetails(
		const ClipboardRoundTrip &trip,
		const FakeClipboard &fake,
		int rows) {
	return u"%1; writes=%2 reads=%3 rowsSinceMark=%4 contents=%5 owns=%6 "
		u"yielded=%7 atMs=%8 sentinels=%9"_q.arg(
			ClipboardRoundTripText(trip),
			QString::number(fake.writes),
			QString::number(fake.reads),
			QString::number(rows),
			AttemptList(trip, [](const ClipboardAttempt &attempt) {
				return ContentText(attempt.content, attempt.heldSentinel);
			}),
			AttemptList(trip, [](const ClipboardAttempt &attempt) {
				return OwnsText(attempt.owns);
			}),
			AttemptList(trip, [](const ClipboardAttempt &attempt) {
				return YieldedText(attempt.yielded);
			}),
			AttemptList(trip, [](const ClipboardAttempt &attempt) {
				return QString::number(attempt.atMs);
			}),
			AttemptList(trip, [](const ClipboardAttempt &attempt) {
				return attempt.sentinel;
			}));
}

void CheckFailThreeArm(
		const ClipboardRoundTrip &trip,
		const FakeClipboard &fake,
		qint64 mark) {
	const auto rows = CountAttemptRows(mark, kFailThreeLabel);
	const auto details = ArmDetails(trip, fake, rows);
	const auto made = int(trip.attempts.size());
	Check(
		trip.finished
			&& trip.ok
			&& (trip.refusal == ClipboardRefusal::None)
			&& (trip.succeededAt == kFailThreeAttempts)
			&& (made == kFailThreeAttempts)
			&& (fake.writes == kFailThreeAttempts)
			&& (fake.reads == kFailThreeAttempts)
			&& (rows == kFailThreeAttempts),
		u"clipboard fail three: an accessor that fails three times reads "
		u"its sentinel back at attempt 4, with exactly one logged row per "
		u"attempt"_q,
		details);

	const auto contents = std::vector<ClipboardContent>{
		ClipboardContent::Empty,
		ClipboardContent::HarnessSentinel,
		ClipboardContent::Empty,
		ClipboardContent::Expected,
	};
	const auto owns = std::vector<std::optional<bool>>{
		false,
		false,
		false,
		true,
	};
	const auto heldIsFirst = (made >= 2)
		&& !trip.attempts[0].sentinel.isEmpty()
		&& (trip.attempts[1].heldSentinel == trip.attempts[0].sentinel);
	Check(
		(ContentsOf(trip) == contents)
			&& (OwnsOf(trip) == owns)
			&& heldIsFirst,
		u"clipboard fail three: each failed attempt names what its read "
		u"held, the stale read names the harness sentinel it held, and the "
		u"injected owns reading is recorded per attempt"_q,
		details);

	auto paced = (made == kFailThreeAttempts)
		&& !trip.attempts.front().yielded.has_value();
	for (auto i = 1; i < made; ++i) {
		const auto &attempt = trip.attempts[i];
		const auto &previous = trip.attempts[i - 1];
		paced = paced
			&& attempt.yielded.value_or(false)
			&& (attempt.atMs - previous.atMs >= kSelfTestDelay);
	}
	Check(
		paced,
		u"clipboard fail three: the retry yields to the event loop between "
		u"attempts and waits at least the delay"_q,
		u"delayMs=%1 yielded=%2 atMs=%3"_q.arg(
			QString::number(kSelfTestDelay),
			AttemptList(trip, [](const ClipboardAttempt &attempt) {
				return YieldedText(attempt.yielded);
			}),
			AttemptList(trip, [](const ClipboardAttempt &attempt) {
				return QString::number(attempt.atMs);
			})));

	const auto prefix = u"harness-clipboard:%1:"_q.arg(kFailThreeLabel);
	auto distinct = base::flat_set<QString>();
	auto prefixed = true;
	for (const auto &attempt : trip.attempts) {
		distinct.emplace(attempt.sentinel);
		prefixed = prefixed && attempt.sentinel.startsWith(prefix);
	}
	Check(
		prefixed
			&& (made == kFailThreeAttempts)
			&& (int(distinct.size()) == kFailThreeAttempts),
		u"clipboard fail three: every attempt wrote a sentinel of its own "
		u"carrying the caller's label"_q,
		u"prefix=%1 distinct=%2 sentinels=%3"_q.arg(
			prefix,
			QString::number(distinct.size()),
			AttemptList(trip, [](const ClipboardAttempt &attempt) {
				return attempt.sentinel;
			})));
}

void CheckAlwaysEmptyArm(
		const ClipboardRoundTrip &trip,
		const FakeClipboard &fake,
		qint64 mark) {
	const auto rows = CountAttemptRows(mark, kAlwaysEmptyLabel);
	const auto atLeast = (kClipboardRoundTripAttempts - 1)
		* kClipboardRoundTripDelay;
	Check(
		trip.finished
			&& !trip.ok
			&& (trip.refusal == ClipboardRefusal::ReadEmpty)
			&& (ClipboardRefusalName(trip.refusal) == u"read-empty"_q)
			&& (trip.bound == kClipboardRoundTripAttempts)
			&& (trip.delay == kClipboardRoundTripDelay)
			&& (int(trip.attempts.size()) == kClipboardRoundTripAttempts)
			&& (fake.writes == kClipboardRoundTripAttempts)
			&& (fake.reads == kClipboardRoundTripAttempts)
			&& (rows == kClipboardRoundTripAttempts)
			&& (trip.elapsedMs >= atLeast),
		u"clipboard always empty: with the shipped defaults an accessor "
		u"that never reads back is refused by name as read-empty after "
		u"exactly the bound, with every attempt logged"_q,
		u"%1; defaults=%2x%3ms elapsedAtLeastMs=%4"_q.arg(
			ArmDetails(trip, fake, rows),
			QString::number(kClipboardRoundTripAttempts),
			QString::number(kClipboardRoundTripDelay),
			QString::number(atLeast)));
}

void CheckStaleArm(
		const ClipboardRoundTrip &trip,
		const FakeClipboard &fake,
		const QString &firstSentinel,
		qint64 mark) {
	const auto rows = CountAttemptRows(mark, kStaleLabel);
	const auto made = int(trip.attempts.size());
	Check(
		trip.finished
			&& !trip.ok
			&& (trip.refusal == ClipboardRefusal::ReadStaleSentinel)
			&& (ClipboardRefusalName(trip.refusal)
				== u"read-stale-sentinel"_q)
			&& (made == kStaleAttempts)
			&& !firstSentinel.isEmpty()
			&& (trip.attempts.back().heldSentinel == firstSentinel)
			&& (rows == kStaleAttempts),
		u"clipboard stale: a clipboard still holding an earlier harness "
		u"sentinel is refused by name as read-stale-sentinel, naming that "
		u"sentinel"_q,
		u"%1; expectedHeld=%2"_q.arg(
			ArmDetails(trip, fake, rows),
			firstSentinel));
}

void CheckForeignArm(
		const ClipboardRoundTrip &trip,
		const FakeClipboard &fake,
		qint64 mark) {
	const auto rows = CountAttemptRows(mark, kForeignLabel);
	const auto made = int(trip.attempts.size());
	const auto allForeign = ranges::all_of(
		trip.attempts,
		[](const ClipboardAttempt &attempt) {
			return (attempt.content == ClipboardContent::Foreign)
				&& attempt.heldSentinel.isEmpty();
		});
	Check(
		trip.finished
			&& !trip.ok
			&& (trip.refusal == ClipboardRefusal::ReadForeign)
			&& (ClipboardRefusalName(trip.refusal) == u"read-foreign"_q)
			&& (made == kForeignAttempts)
			&& allForeign
			&& (rows == kForeignAttempts),
		u"clipboard foreign: a clipboard holding text the harness did not "
		u"write is refused by name as read-foreign, with no held sentinel"_q,
		ArmDetails(trip, fake, rows));

	// Only the formatter outputs' own lengths and whether they contain the
	// injected text are printed, never the text.
	const auto summary = ClipboardRoundTripText(trip);
	const auto summaryContains = summary.contains(kForeign);
	auto lengths = QStringList();
	auto containing = 0;
	for (const auto &attempt : trip.attempts) {
		const auto text = ClipboardAttemptText(trip, attempt);
		lengths.push_back(QString::number(text.size()));
		if (text.contains(kForeign)) {
			++containing;
		}
	}
	Check(
		!summaryContains
			&& (containing == 0)
			&& (int(lengths.size()) == kForeignAttempts),
		u"clipboard foreign: neither the summary nor any attempt row of the "
		u"foreign arm carries the injected foreign text"_q,
		u"summaryLength=%1 summaryContains=%2 attemptTexts=%3 "
		u"attemptLengths=[%4] attemptsContaining=%5"_q.arg(
			QString::number(summary.size()),
			summaryContains ? u"1"_q : u"0"_q,
			QString::number(lengths.size()),
			lengths.join(u", "_q),
			QString::number(containing)));
}

void CheckReadClipboardFakes(
		const ClipboardFakes &fakes,
		const QString &firstSentinel) {
	const auto expected = ReadClipboard(
		kExpectedCopy,
		FakeAccessor(u"fake-expected"_q, fakes.expected, false));
	Check(
		expected.matches()
			&& (expected.content == ClipboardContent::Expected)
			&& expected.heldSentinel.isEmpty()
			&& !expected.owns.has_value(),
		u"ReadClipboard: a clipboard holding the expected text reads "
		u"expected"_q,
		ClipboardReadingText(expected));

	const auto empty = ReadClipboard(
		kExpectedCopy,
		FakeAccessor(u"fake-always-empty"_q, fakes.alwaysEmpty, false));
	Check(
		!empty.matches()
			&& (empty.content == ClipboardContent::Empty)
			&& empty.heldSentinel.isEmpty(),
		u"ReadClipboard: an empty clipboard reads empty"_q,
		ClipboardReadingText(empty));

	const auto stale = ReadClipboard(
		kExpectedCopy,
		FakeAccessor(u"fake-stale"_q, fakes.stale, false));
	Check(
		!stale.matches()
			&& (stale.content == ClipboardContent::HarnessSentinel)
			&& !firstSentinel.isEmpty()
			&& (stale.heldSentinel == firstSentinel),
		u"ReadClipboard: a clipboard holding an earlier harness sentinel "
		u"reads harness-sentinel and names it"_q,
		u"%1 expectedHeld=%2"_q.arg(
			ClipboardReadingText(stale),
			firstSentinel));

	const auto foreign = ReadClipboard(
		kExpectedCopy,
		FakeAccessor(u"fake-foreign"_q, fakes.foreign, false));
	const auto text = ClipboardReadingText(foreign);
	const auto contains = text.contains(kForeign);
	Check(
		!foreign.matches()
			&& (foreign.content == ClipboardContent::Foreign)
			&& foreign.heldSentinel.isEmpty()
			&& !contains,
		u"ReadClipboard: a clipboard holding text the harness did not write "
		u"reads foreign, and its text carries none of it"_q,
		u"%1 textContains=%2"_q.arg(text, contains ? u"1"_q : u"0"_q));
}

void CheckLiveArm(const ClipboardRoundTrip &trip) {
	Check(
		trip.ok,
		u"clipboard live: the system clipboard reads a harness sentinel back "
		u"within the bound"_q,
		ClipboardRoundTripText(trip));
	if (!trip.ok || trip.attempts.empty()) {
		return;
	}
	const auto reading = ReadClipboard(trip.attempts.back().sentinel);
	Check(
		reading.matches(),
		u"clipboard live: ReadClipboard classifies the system clipboard as "
		u"the sentinel the round trip wrote"_q,
		ClipboardReadingText(reading));
}

// Only the lock decides; a round trip that read back is never withdrawn, so
// this turns nothing but a would-be FAIL into N/A, and only when locked.
[[nodiscard]] QString LiveArmLockGate(
		const ClipboardRoundTrip &trip,
		const ConsoleLockReading &console) {
	const auto gate = ConsoleLockGate(console);
	if (trip.ok || gate.isEmpty()) {
		return QString();
	}
	return gate
		+ u"; no process on the console's desktop can open the clipboard "
		u"while it is locked, and the live round trip was refused "_q
		+ ClipboardRefusalName(trip.refusal)
		+ u" after "_q
		+ QString::number(trip.attempts.size())
		+ u"/"_q
		+ QString::number(trip.bound)
		+ u" attempts - a scheduling condition: rerun on an unlocked "
		u"console"_q;
}

void CheckRefusalConsoleReading(
		const ClipboardRoundTrip &refused,
		const ClipboardRoundTrip &succeeded) {
	const auto refusedText = ClipboardRoundTripText(refused);
	const auto succeededText = ClipboardRoundTripText(succeeded);
	Check(
		refusedText.contains(u"console at refusal: CONSOLE_LOCK: state="_q)
			&& !succeededText.contains(u"CONSOLE_LOCK:"_q),
		u"clipboard refusal: a refused round trip names the console-lock "
		u"reading taken when it was refused, and one that read back names "
		u"none"_q,
		u"refused={%1} succeeded={%2}"_q.arg(refusedText, succeededText));
}

void CheckForeignTextAbsent(qint64 mark) {
	auto scan = DiscriminatingScan(
		u"clipboard foreign-text scan"_q,
		u"lines carrying the injected foreign text"_q,
		u"the foreign arm's own CLIPBOARD_ROUND_TRIP_ATTEMPT rows"_q);
	const auto control = AttemptRowPrefix(kForeignLabel);
	const auto lines = TestLogLinesSince(mark);
	for (auto i = 0; i != int(lines.size()); ++i) {
		const auto &line = lines[i];
		scan.examined();
		if (line.contains(kForeign)) {
			// The line's index only, never the line.
			scan.matchedSubject(u"line %1"_q.arg(i + 1));
		}
		if (line.startsWith(control)) {
			scan.matchedControl();
		}
	}
	const auto decided = scan.report();
	Check(
		decided
			&& (scan.subjectCount() == 0)
			&& (scan.controlCount() == kForeignAttempts),
		u"clipboard privacy: the injected foreign text appears in no line "
		u"this self-test appended to the test log"_q,
		u"decided=%1 subjects=%2 controls=%3 examined=%4 fromByte=%5"_q.arg(
			decided ? u"1"_q : u"0"_q,
			QString::number(scan.subjectCount()),
			QString::number(scan.controlCount()),
			QString::number(scan.examinedCount()),
			QString::number(mark)));
}

} // namespace

ClipboardAccessor SystemClipboardAccessor() {
	return {
		.name = u"system"_q,
		.write = [](const QString &text) {
			QGuiApplication::clipboard()->setText(text);
		},
		.read = [] {
			return QGuiApplication::clipboard()->text();
		},
		.owns = [] {
			return QGuiApplication::clipboard()->ownsClipboard();
		},
	};
}

QString ClipboardContentName(ClipboardContent content) {
	switch (content) {
	case ClipboardContent::Empty: return u"empty"_q;
	case ClipboardContent::Expected: return u"expected"_q;
	case ClipboardContent::HarnessSentinel: return u"harness-sentinel"_q;
	case ClipboardContent::Foreign: return u"foreign"_q;
	}
	Unexpected("Content in Test::ClipboardContentName.");
}

QString ClipboardRefusalName(ClipboardRefusal refusal) {
	switch (refusal) {
	case ClipboardRefusal::None: return u"none"_q;
	case ClipboardRefusal::InvalidBound: return u"invalid-bound"_q;
	case ClipboardRefusal::ReadEmpty: return u"read-empty"_q;
	case ClipboardRefusal::ReadStaleSentinel:
		return u"read-stale-sentinel"_q;
	case ClipboardRefusal::ReadForeign: return u"read-foreign"_q;
	}
	Unexpected("Refusal in Test::ClipboardRefusalName.");
}

auto AppendClipboardRoundTrip(
		not_null<Runner*> runner,
		ClipboardRoundTripArgs args)
-> std::shared_ptr<const ClipboardRoundTrip> {
	if (!args.accessor.write || !args.accessor.read) {
		args.accessor = SystemClipboardAccessor();
	}
	const auto driver = std::make_shared<RoundTripDriver>();
	driver->args = std::move(args);
	driver->trip = std::make_shared<ClipboardRoundTrip>();
	const auto trip = driver->trip.get();
	trip->label = driver->args.label;
	trip->accessor = driver->args.accessor.name;
	trip->platform = QGuiApplication::platformName();
	trip->bound = driver->args.attempts;
	trip->delay = driver->args.delay;
	const auto perAttempt = std::max(driver->args.delay, crl::time())
		+ kClipboardAttemptAllowance;
	runner->add({
		.name = u"clipboard round trip: %1"_q.arg(trip->label),
		.run = [=] {
			StartRoundTrip(driver.get());
		},
		.until = [=] {
			return driver->trip->finished;
		},
		.then = [=] {
			Note(ClipboardRoundTripText(*driver->trip));
		},
		.timeout = kDefaultStageTimeout
			+ std::max(driver->args.attempts, 1) * perAttempt,
		.timeoutDetails = [=] {
			return ClipboardRoundTripText(*driver->trip);
		},
	});
	runner->onFinish([=] {
		ReleaseRoundTrip(driver.get());
	});
	return driver->trip;
}

QString ClipboardAttemptText(
		const ClipboardRoundTrip &trip,
		const ClipboardAttempt &attempt) {
	return u"CLIPBOARD_ROUND_TRIP_ATTEMPT: label=%1 accessor=%2 platform=%3 "
		u"attempt=%4/%5 sentinel=%6 read=%7 owns=%8 yielded=%9 atMs=%10 "
		u"writeMs=%11 ok=%12"_q.arg(
			trip.label,
			trip.accessor,
			trip.platform,
			QString::number(attempt.index),
			QString::number(trip.bound),
			attempt.sentinel,
			ReadText(attempt),
			OwnsText(attempt.owns),
			YieldedText(attempt.yielded),
			QString::number(attempt.atMs),
			QString::number(attempt.writeMs),
			(attempt.content == ClipboardContent::Expected)
				? u"1"_q
				: u"0"_q);
}

QString ClipboardRoundTripText(const ClipboardRoundTrip &trip) {
	const auto line = u"CLIPBOARD_ROUND_TRIP: label=%1 accessor=%2 "
		u"platform=%3 finished=%4 ok=%5 succeededAt=%6 attemptsMade=%7 "
		u"bound=%8 delayMs=%9 elapsedMs=%10 refusal=%11"_q.arg(
			trip.label,
			trip.accessor,
			trip.platform,
			trip.finished ? u"1"_q : u"0"_q,
			trip.ok ? u"1"_q : u"0"_q,
			QString::number(trip.succeededAt),
			QString::number(trip.attempts.size()),
			QString::number(trip.bound),
			QString::number(trip.delay),
			QString::number(trip.elapsedMs),
			ClipboardRefusalName(trip.refusal));
	const auto reason = !trip.reason.isEmpty()
		? trip.reason
		: !trip.finished
		? u"pending: the round trip has not finished"_q
		: QString();
	return reason.isEmpty() ? line : (line + u" - "_q + reason);
}

ClipboardReading ReadClipboard(
		const QString &expected,
		const ClipboardAccessor &accessor) {
	const auto used = accessor.read ? accessor : SystemClipboardAccessor();
	auto result = ClipboardReading();
	result.accessor = used.name;
	result.platform = QGuiApplication::platformName();
	{
		// The read text never leaves this scope.
		const auto read = used.read();
		result.content = Classify(read, expected, &result.heldSentinel);
	}
	if (used.owns) {
		result.owns = used.owns();
	}
	return result;
}

QString ClipboardReadingText(const ClipboardReading &reading) {
	return u"clipboard: content=%1 owns=%2 accessor=%3 platform=%4"_q.arg(
		ContentText(reading.content, reading.heldSentinel),
		OwnsText(reading.owns),
		reading.accessor,
		reading.platform);
}

void AppendClipboardRoundTripSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<ClipboardSelfTest>();
	const auto fakes = MakeFakes();

	runner->add({
		.name = u"clipboard round trip self-test: mark the test log"_q,
		.run = [=] {
			state->logMark = TestLogSize();
			Note(u"clipboard round trip self-test: rows are counted in the "
				u"test log from byte %1 on"_q.arg(state->logMark));
		},
	});

	const auto failed = AppendClipboardRoundTrip(runner, {
		.label = kFailThreeLabel,
		.accessor = FakeAccessor(u"fake-fail-three"_q, fakes.failThree, true),
		.delay = kSelfTestDelay,
	});
	runner->add({
		.name = u"clipboard round trip self-test: a clipboard that fails "
			u"three times reads back at attempt 4"_q,
		.then = [=] {
			CheckFailThreeArm(*failed, *fakes.failThree, state->logMark);
		},
	});

	const auto empty = AppendClipboardRoundTrip(runner, {
		.label = kAlwaysEmptyLabel,
		.accessor = FakeAccessor(
			u"fake-always-empty"_q,
			fakes.alwaysEmpty,
			false),
	});
	runner->add({
		.name = u"clipboard round trip self-test: an always-failing "
			u"clipboard is refused after exactly the bound"_q,
		.then = [=] {
			CheckAlwaysEmptyArm(*empty, *fakes.alwaysEmpty, state->logMark);
			CheckRefusalConsoleReading(*empty, *failed);
		},
	});

	const auto staleTrip = AppendClipboardRoundTrip(runner, {
		.label = kStaleLabel,
		.accessor = FakeAccessor(u"fake-stale"_q, fakes.stale, false),
		.delay = kSelfTestDelay,
		.attempts = kStaleAttempts,
	});
	const auto foreignTrip = AppendClipboardRoundTrip(runner, {
		.label = kForeignLabel,
		.accessor = FakeAccessor(u"fake-foreign"_q, fakes.foreign, false),
		.delay = kSelfTestDelay,
		.attempts = kForeignAttempts,
	});
	runner->add({
		.name = u"clipboard round trip self-test: a stale sentinel and a "
			u"foreign text are refused by name, and ReadClipboard classifies "
			u"the same reads"_q,
		.then = [=] {
			const auto firstSentinel = failed->attempts.empty()
				? QString()
				: failed->attempts.front().sentinel;
			CheckStaleArm(
				*staleTrip,
				*fakes.stale,
				firstSentinel,
				state->logMark);
			CheckForeignArm(*foreignTrip, *fakes.foreign, state->logMark);
			CheckReadClipboardFakes(fakes, firstSentinel);
		},
	});

	const auto live = AppendClipboardRoundTrip(runner, {
		.label = kLiveLabel,
	});
	runner->add({
		.name = u"clipboard round trip self-test: a live round trip against "
			u"the system clipboard"_q,
		// Runner::beginStage asks once, so |then| Notes the deciding reading.
		.skipReason = [=] {
			state->liveConsole = ReadConsoleLock();
			return LiveArmLockGate(*live, state->liveConsole);
		},
		.then = [=] {
			Note(ConsoleLockText(state->liveConsole));
			CheckLiveArm(*live);
		},
	});

	runner->add({
		.name = u"clipboard round trip self-test: the injected foreign text "
			u"is nowhere in the run's log"_q,
		.then = [=] {
			CheckForeignTextAbsent(state->logMark);
		},
	});
}

} // namespace Test

#endif // _DEBUG
