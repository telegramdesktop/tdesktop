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

[[nodiscard]] std::vector<BlockedLaunch> &Blocked() {
	static auto result = std::vector<BlockedLaunch>();
	return result;
}

} // namespace

bool BlockLaunch(const QString &function, const QString &argument) {
	if (!Active()) {
		return false;
	}
	Blocked().push_back({ function, argument });
	Fire(u"launch_blocked"_q);
	Fail(u"blocked launch: Platform::File::%1"_q.arg(function), argument);
	return true;
}

const std::vector<BlockedLaunch> &BlockedLaunches() {
	return Blocked();
}

void CheckNoBlockedLaunches(const QString &what) {
	if (Blocked().empty()) {
		Pass(what);
		return;
	}
	auto details = QStringList();
	for (const auto &entry : Blocked()) {
		details.push_back(entry.function + u": "_q + entry.argument);
	}
	Note(u"%1: already failed - %2"_q.arg(what, details.join(u"; "_q)));
}

} // namespace Test

#else // _DEBUG

namespace Test {

bool BlockLaunch(const QString &, const QString &) {
	return false;
}

} // namespace Test

#endif // _DEBUG
