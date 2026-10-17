/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

namespace Test {

class Probe;
class Runner;

// Records one "rpc retry code=<code> type=<type> request=<constructor>" row
// for an MTP answer the transport auto-resends without ever calling the
// request's fail handler. |request| is the boxed body constructor id,
// printed as 0x-prefixed lowercase hex; never a request or answer byte,
// never a payload, never a secret. No-op unless Active(), so the call site
// in application code needs no condition around it, and the Release
// branch of this module defines this function alone.
void RecordRpcRetry(int code, const QString &type, uint32 request);

// The record those rows go into. Debug-only: like BlockedLaunches() in
// test_launch_fuse.h it is declared here and deliberately left undefined
// in a Release build, because no production code reads it.
[[nodiscard]] Probe &RpcRetryProbe();

// The seam's own self-test. MTP::Instance::Private::onErrorDefault resends
// every code-500 - and every negative-code - answer after a doubling delay
// and returns true, so rpcErrorOccured returns false and the request's own
// .fail() is never reached: a scenario waiting on a product row sees no
// .done(), no .fail() and no product outcome at all, only its own stage
// timeout, while the application Debug log repeats "RPC Info: error
// received, code 500, type <T>". RecordRpcRetry is what makes that visible
// in the harness log, and RpcRetryProbe() is where a scenario reads it -
// through a mark() taken before the action, never over the whole history.
//
// The self-test issues harmless, idempotent MTPhelp_GetConfig() requests
// through an ordinary MTP::Sender - with its default FailSkipPolicy::Simple
// and never handleAllErrors() or handleFloodErrors(), because a fail
// handler that returns true for a 500 makes rpcErrorOccured return before
// onErrorDefault and the seam would then never run - and delivers
// synthesized rpc_error answers for them through the already public
// MTP::Instance::processCallback. One code 500 must record exactly one row
// naming that request's own constructor id and must leave the request
// registered for the delayed resend; one code 400 must record no row and
// must reach that request's .fail(); and a third leg delivers a 500 for a
// request id this process never sent, so the "an answer that finds no
// pending request" refusal has positive evidence instead of a guard that
// never fires in a healthy run. Every leg reads its rows in the same
// main-thread turn that called processCallback(), which is this
// self-test's own reading that the call reached the seam synchronously.
//
// It needs no wallet, no fixture secret, no chats list and no funded value;
// the only thing it asks of the process is a ready session to send through.
// It appends its own teardown as its last stage, and it emits no deliberate
// failure - every stage is expected to PASS on a healthy harness.
void AppendRpcRetrySelfTest(not_null<Runner*> runner);

} // namespace Test
