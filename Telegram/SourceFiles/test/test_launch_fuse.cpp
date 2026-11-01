/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_launch_fuse.h"

#ifdef _DEBUG

#include "test/test_agent.h"
#include "test/test_log.h"
#include "test/test_probe.h"
#include "test/test_runner.h"

#include <QtCore/QUrl>

namespace Test {
namespace {

auto ConsumedExpectations = 0;

[[nodiscard]] std::vector<BlockedLaunch> &Blocked() {
	static auto result = std::vector<BlockedLaunch>();
	return result;
}

[[nodiscard]] std::vector<QString> &Expected() {
	static auto result = std::vector<QString>();
	return result;
}

[[nodiscard]] std::vector<QString> &PrivateExpected() {
	static auto result = std::vector<QString>();
	return result;
}

// Kept for the whole run, so a repeat or a later record never prints it whole.
[[nodiscard]] base::flat_map<QString, QString> &Covered() {
	static auto result = base::flat_map<QString, QString>();
	return result;
}

[[nodiscard]] bool TakeExpectation(
		std::vector<QString> &list,
		const QString &argument) {
	for (auto i = begin(list); i != end(list); ++i) {
		if (*i == argument) {
			list.erase(i);
			return true;
		}
	}
	return false;
}

[[nodiscard]] QString SchemeHost(const QString &argument) {
	const auto url = QUrl(argument);
	const auto host = url.host(QUrl::FullyEncoded);
	return (url.isValid() && !url.scheme().isEmpty() && !host.isEmpty())
		? (url.scheme() + u"://"_q + host)
		: u"(not a url)"_q;
}

[[nodiscard]] QString Printed(const QString &argument) {
	const auto i = Covered().find(argument);
	return (i != Covered().end()) ? i->second : argument;
}

[[nodiscard]] QString FailedLaunchText(const QString &argument) {
	if (Covered().contains(argument) || PrivateExpected().empty()) {
		return Printed(argument);
	}
	auto live = QStringList();
	for (const auto &expected : PrivateExpected()) {
		live.push_back(Printed(expected));
	}
	const auto withheld = u"withheld "_q + SchemeHost(argument);
	Covered().emplace(argument, withheld);
	return withheld
		+ u" while private expectations are live: "_q
		+ live.join(u"; "_q);
}

[[nodiscard]] QString EntriesText(const std::vector<BlockedLaunch> &list) {
	auto parts = QStringList();
	for (const auto &entry : list) {
		parts.push_back(entry.function + u": "_q + Printed(entry.argument));
	}
	return parts.isEmpty() ? u"(none)"_q : parts.join(u"; "_q);
}

} // namespace

bool BlockLaunch(const QString &function, const QString &argument) {
	if (!Active()) {
		return false;
	}
	Blocked().push_back({ function, argument });
	Fire(u"launch_blocked"_q);
	if (TakeExpectation(Expected(), argument)
		|| TakeExpectation(PrivateExpected(), argument)) {
		++ConsumedExpectations;
		Pass(u"expected blocked launch: Platform::File::%1 - %2"_q
			.arg(function, Printed(argument)));
		return true;
	}
	Fail(
		u"blocked launch: Platform::File::%1"_q.arg(function),
		FailedLaunchText(argument));
	return true;
}

const std::vector<BlockedLaunch> &BlockedLaunches() {
	return Blocked();
}

void ExpectBlockedLaunch(const QString &argument) {
	Expected().push_back(argument);
	Note(u"launch fuse: expecting one blocked launch of \"%1\" - it is "
		"the behaviour under test, not an escape"_q.arg(argument));
}

void ExpectPrivateBlockedLaunch(
		const QString &argument,
		const QString &label) {
	PrivateExpected().push_back(argument);
	const auto printed = u"private:%1 %2"_q.arg(label, SchemeHost(argument));
	Covered()[argument] = printed;
	Note(u"launch fuse: expecting one blocked launch of %1, matched exactly "
		"and printed by its label only - it is the behaviour under test, "
		"not an escape"_q.arg(printed));
}

void CheckNoBlockedLaunchExpectations(const QString &what) {
	auto live = QStringList(Expected().begin(), Expected().end());
	for (const auto &argument : PrivateExpected()) {
		live.push_back(Printed(argument));
	}
	Check(
		live.isEmpty(),
		what,
		live.isEmpty()
			? QString()
			: u"declared but never blocked: %1"_q.arg(live.join(u"; "_q)));
}

void CheckBlockedLaunchesExactly(
		const std::vector<BlockedLaunch> &expected,
		const QString &what) {
	const auto &actual = Blocked();
	const auto same = (actual == expected);
	Note(u"blocked launch record: [%1]"_q.arg(EntriesText(actual)));
	Check(
		same,
		what,
		same
			? QString()
			: u"expected [%1] but recorded [%2]"_q.arg(
				EntriesText(expected),
				EntriesText(actual)));
}

void CheckNoBlockedLaunches(const QString &what) {
	if (int(Blocked().size()) == ConsumedExpectations) {
		Pass(what);
		return;
	}
	Note(u"%1: already failed - %2"_q.arg(what, EntriesText(Blocked())));
}

namespace {

const auto kPrivateUrl
	= u"https://fuse-private.example/withdraw/alpha"
	"?token=synthetic-alpha-secret"_q;
const auto kPrivateLabel = u"fuse-self-test-private"_q;
const auto kPrivateFile = u"/synthetic/fuse-self-test/echo-private.bin"_q;
const auto kFileLabel = u"fuse-self-test-file"_q;
const auto kLeftoverUrl
	= u"https://fuse-expected.example/withdraw/bravo"
	"?token=synthetic-bravo-secret"_q;
const auto kLeftoverLabel = u"fuse-self-test-leftover"_q;
const auto kMismatchUrl
	= u"https://fuse-launched.example/withdraw/charlie"
	"?token=synthetic-charlie-secret"_q;
const auto kPublicUrl
	= u"https://fuse-public.example/open/delta?q=synthetic-delta-public"_q;
const auto kRecordWhat
	= u"launch fuse self-test: the record holds every real argument"_q;
const auto kNoneWhat
	= u"launch fuse self-test: no launch beyond the declared ones"_q;
const auto kLeftoverWhat
	= u"launch fuse self-test: a private expectation left over at the end"_q;

struct FuseSelfTest {
	qint64 from = 0;
	std::vector<BlockedLaunch> record;
};

struct Needle {
	QString name;
	QString text;
};

struct ExpectedRow {
	QString name;
	QString row;
};

[[nodiscard]] QString LogPath() {
	return EvidenceDir() + u"test_log.txt"_q;
}

// A Windows row ends in CRLF; one CR is chopped so rows compare the same.
[[nodiscard]] QStringList TestLogLinesSince(qint64 from) {
	auto file = QFile(LogPath());
	if (!file.open(QIODevice::ReadOnly) || !file.seek(from)) {
		return QStringList();
	}
	auto result = QString::fromUtf8(file.readAll()).split(QChar('\n'));
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

[[nodiscard]] bool Launch(
		FuseSelfTest &state,
		const QString &function,
		const QString &argument) {
	state.record.push_back({ function, argument });
	return BlockLaunch(function, argument);
}

[[nodiscard]] std::vector<Needle> PrivateNeedles() {
	return {
		{ u"matched argument"_q, kPrivateUrl },
		{ u"matched path"_q, u"/withdraw/alpha"_q },
		{ u"matched query"_q, u"token=synthetic-alpha-secret"_q },
		{ u"file argument"_q, kPrivateFile },
		{ u"file name"_q, u"echo-private.bin"_q },
		{ u"leftover argument"_q, kLeftoverUrl },
		{ u"leftover path"_q, u"/withdraw/bravo"_q },
		{ u"leftover query"_q, u"token=synthetic-bravo-secret"_q },
		{ u"mismatch argument"_q, kMismatchUrl },
		{ u"mismatch path"_q, u"/withdraw/charlie"_q },
		{ u"mismatch query"_q, u"token=synthetic-charlie-secret"_q },
	};
}

// Hand-transcribed, so a wrong fuse formatter cannot agree with itself.
[[nodiscard]] std::vector<ExpectedRow> ExpectedRows() {
	return {
		{
			u"the matched URL's note"_q,
			u"NOTE: launch fuse: expecting one blocked launch of "
			"private:fuse-self-test-private https://fuse-private.example, "
			"matched exactly and printed by its label only - it is the "
			"behaviour under test, not an escape"_q,
		},
		{
			u"the file's note"_q,
			u"NOTE: launch fuse: expecting one blocked launch of "
			"private:fuse-self-test-file (not a url), matched exactly and "
			"printed by its label only - it is the behaviour under test, "
			"not an escape"_q,
		},
		{
			u"the matched URL's PASS"_q,
			u"TEST_RESULT: PASS: expected blocked launch: "
			"Platform::File::UnsafeOpenUrl - "
			"private:fuse-self-test-private https://fuse-private.example"_q,
		},
		{
			u"the file's PASS"_q,
			u"TEST_RESULT: PASS: expected blocked launch: "
			"Platform::File::UnsafeLaunch - "
			"private:fuse-self-test-file (not a url)"_q,
		},
		{
			u"the leftover's note"_q,
			u"NOTE: launch fuse: expecting one blocked launch of "
			"private:fuse-self-test-leftover https://fuse-expected.example, "
			"matched exactly and printed by its label only - it is the "
			"behaviour under test, not an escape"_q,
		},
		{
			u"the mismatch FAIL"_q,
			u"TEST_RESULT: FAIL: blocked launch: "
			"Platform::File::UnsafeOpenUrl - "
			"withheld https://fuse-launched.example while private "
			"expectations are live: "
			"private:fuse-self-test-leftover https://fuse-expected.example"_q,
		},
		{
			u"the public note"_q,
			u"NOTE: launch fuse: expecting one blocked launch of "
			"\"https://fuse-public.example/open/delta"
			"?q=synthetic-delta-public\" - it is the behaviour under test, "
			"not an escape"_q,
		},
		{
			u"the public PASS"_q,
			u"TEST_RESULT: PASS: expected blocked launch: "
			"Platform::File::UnsafeOpenUrl - "
			"https://fuse-public.example/open/delta"
			"?q=synthetic-delta-public"_q,
		},
		{
			u"the record note"_q,
			u"NOTE: blocked launch record: ["
			"UnsafeOpenUrl: "
			"private:fuse-self-test-private https://fuse-private.example; "
			"UnsafeLaunch: private:fuse-self-test-file (not a url); "
			"UnsafeOpenUrl: withheld https://fuse-launched.example; "
			"UnsafeOpenUrl: https://fuse-public.example/open/delta"
			"?q=synthetic-delta-public]"_q,
		},
		{
			u"the record PASS"_q,
			u"TEST_RESULT: PASS: "_q + kRecordWhat,
		},
		{
			u"the record's \"already failed\" note"_q,
			u"NOTE: "_q
				+ kNoneWhat
				+ u": already failed - "
				"UnsafeOpenUrl: "
				"private:fuse-self-test-private https://fuse-private.example; "
				"UnsafeLaunch: private:fuse-self-test-file (not a url); "
				"UnsafeOpenUrl: withheld https://fuse-launched.example; "
				"UnsafeOpenUrl: https://fuse-public.example/open/delta"
				"?q=synthetic-delta-public"_q,
		},
		{
			u"the leftover FAIL"_q,
			u"TEST_RESULT: FAIL: "_q
				+ kLeftoverWhat
				+ u" - declared but never blocked: "
				"private:fuse-self-test-leftover "
				"https://fuse-expected.example"_q,
		},
	};
}

void CheckSingleLaunch(
		const FuseSelfTest &state,
		const QString &what,
		bool blocked,
		int failures,
		int expectedNewFailures) {
	const auto recordExact = (BlockedLaunches() == state.record);
	const auto newFailures = FailureCount() - failures;
	Check(
		blocked && recordExact && (newFailures == expectedNewFailures),
		what,
		u"blocked=%1 recordExact=%2 newFailures=%3"_q.arg(
			QString::number(blocked ? 1 : 0),
			QString::number(recordExact ? 1 : 0),
			QString::number(newFailures)));
}

void CheckNoPrivateRow(const QStringList &rows) {
	auto scan = DiscriminatingScan(
		u"launch fuse self-test scan"_q,
		u"private path, query or argument"_q,
		u"public argument"_q);
	const auto needles = PrivateNeedles();
	for (auto i = 0; i != int(rows.size()); ++i) {
		const auto &row = rows[i];
		const auto index = QString::number(i + 1);
		scan.examined();
		for (const auto &needle : needles) {
			if (row.contains(needle.text)) {
				// The row's index and the needle's name, never its text.
				scan.matchedSubject(u"row %1 %2"_q.arg(index, needle.name));
			}
		}
		if (row.contains(kPublicUrl)) {
			scan.matchedControl(u"row %1"_q.arg(index));
		}
	}
	scan.report();
	Check(
		(scan.subjectCount() == 0),
		u"launch fuse self-test: no row prints a private path, query or "
		"argument"_q,
		u"subjects=%1 controls=%2 rows=%3"_q.arg(
			QString::number(scan.subjectCount()),
			QString::number(scan.controlCount()),
			QString::number(rows.size())));
}

void CheckExactRows(const QStringList &rows) {
	for (const auto &[name, row] : ExpectedRows()) {
		const auto found = rows.count(row);
		Check(
			(found == 1),
			u"launch fuse self-test: %1 is printed exactly once"_q.arg(name),
			u"found=%1"_q.arg(QString::number(found)));
	}
}

void SelfPrivateMatch(FuseSelfTest &state) {
	state.from = QFileInfo(LogPath()).size();
	state.record = BlockedLaunches();
	const auto failures = FailureCount();
	ExpectPrivateBlockedLaunch(kPrivateUrl, kPrivateLabel);
	ExpectPrivateBlockedLaunch(kPrivateFile, kFileLabel);
	const auto urlBlocked = Launch(state, u"UnsafeOpenUrl"_q, kPrivateUrl);
	const auto fileBlocked = Launch(state, u"UnsafeLaunch"_q, kPrivateFile);
	const auto recordExact = (BlockedLaunches() == state.record);
	const auto newFailures = FailureCount() - failures;
	Check(
		urlBlocked && fileBlocked && recordExact && (newFailures == 0),
		u"launch fuse self-test: each private expectation passes on its "
		"exact launch and the record keeps both real arguments"_q,
		u"urlBlocked=%1 fileBlocked=%2 recordExact=%3 newFailures=%4"_q.arg(
			QString::number(urlBlocked ? 1 : 0),
			QString::number(fileBlocked ? 1 : 0),
			QString::number(recordExact ? 1 : 0),
			QString::number(newFailures)));
}

void SelfMismatch(FuseSelfTest &state) {
	ExpectPrivateBlockedLaunch(kLeftoverUrl, kLeftoverLabel);
	const auto failures = FailureCount();
	const auto blocked = Launch(state, u"UnsafeOpenUrl"_q, kMismatchUrl);
	CheckSingleLaunch(
		state,
		u"launch fuse self-test: a mismatching launch under a live private "
		"expectation fails once"_q,
		blocked,
		failures,
		1);
}

void SelfPublicControl(FuseSelfTest &state) {
	const auto failures = FailureCount();
	ExpectBlockedLaunch(kPublicUrl);
	const auto blocked = Launch(state, u"UnsafeOpenUrl"_q, kPublicUrl);
	CheckSingleLaunch(
		state,
		u"launch fuse self-test: the public expectation passes on its "
		"launch"_q,
		blocked,
		failures,
		0);
}

void SelfRecordAndScan(FuseSelfTest &state) {
	const auto failures = FailureCount();
	CheckBlockedLaunchesExactly(state.record, kRecordWhat);
	CheckNoBlockedLaunches(kNoneWhat);
	const auto beforeLeftover = FailureCount();
	CheckNoBlockedLaunchExpectations(kLeftoverWhat);
	Check(
		(beforeLeftover == failures) && (FailureCount() == failures + 1),
		u"launch fuse self-test: of the record and leftover checks only the "
		"leftover fails"_q,
		u"recordFailures=%1 leftoverFailures=%2"_q.arg(
			QString::number(beforeLeftover - failures),
			QString::number(FailureCount() - beforeLeftover)));

	const auto till = QFileInfo(LogPath()).size();
	const auto rows = TestLogLinesSince(state.from);
	Note(u"launch fuse self-test: scanned test_log.txt bytes [%1, %2), "
		"%3 rows"_q.arg(
			QString::number(state.from),
			QString::number(till),
			QString::number(rows.size())));
	CheckNoPrivateRow(rows);
	CheckExactRows(rows);
}

} // namespace

void AppendLaunchFuseSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<FuseSelfTest>();
	const auto stages = std::vector<std::pair<QString, Fn<void()>>>{
		{
			u"launch fuse self-test: private expectations and their exact "
			"launches"_q,
			[=] { SelfPrivateMatch(*state); },
		},
		{
			u"launch fuse self-test: a mismatching launch under a live "
			"private expectation"_q,
			[=] { SelfMismatch(*state); },
		},
		{
			u"launch fuse self-test: a public expectation and its launch, "
			"the control"_q,
			[=] { SelfPublicControl(*state); },
		},
		{
			u"launch fuse self-test: the record, the leftover and the "
			"test-log scan"_q,
			[=] { SelfRecordAndScan(*state); },
		},
	};
	for (const auto &[name, then] : stages) {
		runner->add({
			.name = name,
			.then = then,
			.timeout = kDefaultStageTimeout,
		});
	}
}

} // namespace Test

#else // _DEBUG

namespace Test {

bool BlockLaunch(const QString &, const QString &) {
	return false;
}

void ExpectBlockedLaunch(const QString &) {
}

void ExpectPrivateBlockedLaunch(const QString &, const QString &) {
}

void CheckNoBlockedLaunchExpectations(const QString &) {
}

void CheckBlockedLaunchesExactly(
		const std::vector<BlockedLaunch> &,
		const QString &) {
}

void AppendLaunchFuseSelfTest(not_null<Runner*>) {
}

} // namespace Test

#endif // _DEBUG
