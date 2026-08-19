/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Test {

// One hand-off to the operating system that the fuse refused.
struct BlockedLaunch {
	QString function;
	QString argument;
};

// Returns true when the call must not reach the operating system, which is
// exactly when Active(). Records the attempt and fails the run, so a run
// that reaches a launcher can never pass quietly. Safe to call from
// anywhere: outside a test run it does nothing and returns false.
bool BlockLaunch(const QString &function, const QString &argument);

[[nodiscard]] const std::vector<BlockedLaunch> &BlockedLaunches();

// BlockLaunch already fails the run for every call it blocks, so this
// records a PASS when nothing was blocked and a Note listing the blocked
// calls otherwise, never a second failure for the same launch.
void CheckNoBlockedLaunches(const QString &what);

} // namespace Test
