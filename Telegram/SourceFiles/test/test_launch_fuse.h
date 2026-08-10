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

	friend inline bool operator==(
		const BlockedLaunch &a,
		const BlockedLaunch &b) = default;
};

// Returns true when the call must not reach the operating system, which is
// exactly when Active(). Records and refuses every such call, and fails the
// run for every one that no scenario declared in advance, so a launcher a
// run never asked for can never be reached quietly. Safe to call from
// anywhere: outside a test run it does nothing and returns false.
bool BlockLaunch(const QString &function, const QString &argument);

[[nodiscard]] const std::vector<BlockedLaunch> &BlockedLaunches();

// A scenario that drives a production branch whose own body ends in an
// external open registers that exact argument here in advance.
// BlockLaunch still refuses the launch, still records it in Blocked() and
// still fires launch_blocked; only the verdict becomes a PASS naming the
// expectation, and the registration is CONSUMED there, so reaching the
// same argument twice yields one pass and one failure. An argument no
// scenario registered keeps the existing FAIL: the fuse is not weakened
// by one byte for anything undeclared.
void ExpectBlockedLaunch(const QString &argument);

// A leftover registration means the declared launch never happened, which
// is a finding of its own: this fails naming every leftover argument, and
// passes only when none are live. Call it from the scenario's last stage;
// the runner does not do it.
void CheckNoBlockedLaunchExpectations(const QString &what);

// Asserts the whole record: the entries Blocked() collected are exactly
// |expected|, in order, comparing both function and argument. A wrong
// order, a missing entry or an extra one is a FAIL that logs the expected
// and the recorded sequence side by side; the recorded sequence is also
// noted on the passing path, so the record is quotable either way.
void CheckBlockedLaunchesExactly(
	const std::vector<BlockedLaunch> &expected,
	const QString &what);

// BlockLaunch already fails the run for every undeclared call it blocks, so
// this records a PASS when nothing was blocked beyond the launches a
// scenario declared in advance - an empty record included - and a Note
// listing the whole record otherwise, never a second failure for the same
// launch.
void CheckNoBlockedLaunches(const QString &what);

} // namespace Test
