/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

namespace Test {

class Runner;

// A scenario stage that does not apply used to be hand-rolled: the gate made
// |run| and |then| no-ops and answered |until| true, so the stage wrote no
// verdict row at all. The overlay of
// 2026/09/01/route-share-fetches-through-a-dedicated-dc-shift carried exactly
// that lambda, and its silence is indistinguishable in the log from a check
// that was never written - a reader sees neither a PASS nor a FAIL and cannot
// tell a deliberate skip from a missing measurement, which is the same class
// of unreadable negative the harness refuses everywhere else.
// Test::Stage::skipReason removes the silence: a non-empty reason writes one
// TEST_RESULT: N/A: <stage> - <reason> row, skips |run|, |until| and |then|,
// and moves to the next stage in the turn that began this one, so a false
// gate never waits and never times out.
//
// AppendGatedStageSelfTest is that instrument measuring itself. It registers
// one stage whose gate reads false by construction and one whose gate reads
// true, and the false-gated stage carries a never-ready |until| under a
// one-second timeout: if the skip did not take, that stage fails by timeout
// inside a second instead of hanging the run, which is what makes the single
// check falsifiable rather than vacuous. The check is in the true-gated
// stage's |then| and reads the counters both stages kept - the skipped
// stage's |run|, |until| and |then| must each have been entered zero times
// and the applied stage's |run| exactly once - together with the
// milliseconds elapsed since the opening stage's |then| armed the clock, so
// that interval holds the skipped stage alone and not the arm stage's own
// tick, which is the reading for "the stage completes in the turn that
// begins it".
//
// It needs no session, no chats list, no network, no wallet, no fixture
// secret and no funded value, and it builds no widget, so it asks nothing of
// the process and has nothing to tear down. It emits no deliberate failure:
// on a healthy harness its run carries exactly one TEST_RESULT: N/A row,
// exactly one TEST_RESULT: PASS row and no FAIL.
void AppendGatedStageSelfTest(not_null<Runner*> runner);

} // namespace Test
