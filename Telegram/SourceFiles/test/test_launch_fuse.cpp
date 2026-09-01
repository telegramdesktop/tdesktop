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

[[nodiscard]] bool TakeExpectation(const QString &argument) {
	for (auto i = begin(Expected()); i != end(Expected()); ++i) {
		if (*i == argument) {
			Expected().erase(i);
			return true;
		}
	}
	return false;
}

[[nodiscard]] QString EntriesText(const std::vector<BlockedLaunch> &list) {
	auto parts = QStringList();
	for (const auto &entry : list) {
		parts.push_back(entry.function + u": "_q + entry.argument);
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
	if (TakeExpectation(argument)) {
		++ConsumedExpectations;
		Pass(u"expected blocked launch: Platform::File::%1 - %2"_q
			.arg(function, argument));
		return true;
	}
	Fail(u"blocked launch: Platform::File::%1"_q.arg(function), argument);
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

void CheckNoBlockedLaunchExpectations(const QString &what) {
	const auto &live = Expected();
	Check(
		live.empty(),
		what,
		live.empty()
			? QString()
			: u"declared but never blocked: %1"_q.arg(
				QStringList(live.begin(), live.end()).join(u"; "_q)));
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

} // namespace Test

#else // _DEBUG

namespace Test {

bool BlockLaunch(const QString &, const QString &) {
	return false;
}

void ExpectBlockedLaunch(const QString &) {
}

void CheckNoBlockedLaunchExpectations(const QString &) {
}

void CheckBlockedLaunchesExactly(
		const std::vector<BlockedLaunch> &,
		const QString &) {
}

} // namespace Test

#endif // _DEBUG
