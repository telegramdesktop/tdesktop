/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_rpc_retry.h"

#ifdef _DEBUG

#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/sender.h"
#include "test/test_agent.h"
#include "test/test_log.h"
#include "test/test_probe.h"
#include "test/test_runner.h"

#include <QtCore/QCoreApplication>

namespace Test {
namespace {

// The delayed resend of a first 500 is scheduled one second out, so a
// healthy connection answers well inside this cap; a slow or offline host
// degrades the observation to a Note instead of failing the run, because
// the network is not what this self-test measures. The cap stays well
// under kDefaultStageTimeout, so that stage can never be the run's timeout.
constexpr auto kResendAnswerCap = crl::time(5000);

// The distance from a real request id to one this process never allocated.
// The derived id is re-checked against hasCallback(), never assumed free.
constexpr auto kOrphanIdDistance = 1000000;

} // namespace

Probe &RpcRetryProbe() {
	static auto result = Probe(u"mtp"_q);
	return result;
}

void RecordRpcRetry(int code, const QString &type, uint32 request) {
	if (!Active()) {
		return;
	}
	// One multi-argument arg(), never a chain: a '%' inside a server-sent
	// |type| would otherwise be re-substituted by the next arg() call.
	RpcRetryProbe().record(
		u"rpc retry code=%1 type=%2 request=0x%3"_q.arg(
			QString::number(code),
			type,
			QString::number(request, 16).rightJustified(8, QChar('0'))));
}

void AppendRpcRetrySelfTest(not_null<Runner*> runner) {
	struct State {
		MTP::Instance *instance = nullptr;
		std::unique_ptr<MTP::Sender> sender;
		std::vector<QString> rows500;
		std::vector<QString> rows400;
		mtpRequestId id500 = 0;
		mtpRequestId id400 = 0;
		mtpRequestId orphanId = 0;
		crl::time waitStartedAt = 0;
		int mark500 = 0;
		int mark400 = 0;
		int markNone = 0;
		int failCode500 = 0;
		int failCode400 = 0;
		bool pending500Before = false;
		bool pending500After = false;
		bool pending400Before = false;
		bool pending400After = false;
		bool pendingNoneBefore = false;
		bool done500 = false;
		bool mainThread = false;
	};
	// Leaked on purpose, the way the harness's other self-tests leak theirs:
	// the stages outlive this call. The teardown stage releases the Sender,
	// after which the State holds nothing but QStrings and PODs.
	const auto state = new State();

	runner->waitForSessionReady();

	runner->add({
		.name = u"rpc retry self-test: a code-500 answer records one "
			"retry row"_q,
		.run = [=] {
			state->instance = &Core::App().domain().active().mtp();
			state->sender = std::make_unique<MTP::Sender>(state->instance);
			state->id500 = state->sender->request(
				MTPhelp_GetConfig()
			).done([=] {
				state->done500 = true;
			}).fail([=](const MTP::Error &error) {
				state->failCode500 = error.code();
			}).send();
			state->pending500Before
				= state->instance->hasCallback(state->id500);
			state->mark500 = RpcRetryProbe().mark();

			auto response = MTP::Response();
			response.requestId = state->id500;
			MTPRpcError(MTP_rpc_error(
				MTP_int(500),
				MTP_string("SELFTEST_UNAVAILABLE"))
			).write(response.reply);
			state->instance->processCallback(response);

			// Read in the same main-thread turn that delivered the answer:
			// that reading is this call's thread-affinity evidence, not an
			// assumption about the transport's own path.
			state->rows500 = RpcRetryProbe().rowsSince(state->mark500);
			state->pending500After
				= state->instance->hasCallback(state->id500);
			state->mainThread = (QThread::currentThread()
				== QCoreApplication::instance()->thread());
		},
		.then = [=] {
			const auto reading = u"requestId=%1 pendingBefore=%2 "
				"pendingAfter=%3 rowsInWindow=%4 mainThread=%5"_q.arg(
					QString::number(state->id500),
					state->pending500Before ? u"1"_q : u"0"_q,
					state->pending500After ? u"1"_q : u"0"_q,
					QString::number(int(state->rows500.size())),
					state->mainThread ? u"1"_q : u"0"_q);
			Check(
				state->pending500Before,
				u"rpc retry self-test: the synthesized 500 found its "
				"request pending"_q,
				reading);
			if (!state->pending500Before) {
				// The answer found no parser, so processCallback recorded
				// nothing and every reading below would pass vacuously.
				return;
			}
			RpcRetryProbe().checkCountSince(
				state->mark500,
				u"rpc retry "_q,
				1,
				u"rpc retry self-test: exactly one retry row in the "
				"window"_q);
			// Rebuilt from mtpc_help_getConfig here instead of shared with
			// the seam, so a change to the row's format is caught by this
			// check rather than cancelled out by it.
			RpcRetryProbe().checkSawSince(
				state->mark500,
				u"rpc retry code=500 type=SELFTEST_UNAVAILABLE "
				"request=0x%1"_q.arg(
					QString::number(uint32(mtpc_help_getConfig), 16)
						.rightJustified(8, QChar('0'))),
				u"rpc retry self-test: the row names code 500 and the "
				"request's own constructor id"_q);
			Check(
				state->pending500After,
				u"rpc retry self-test: the 500'd request stays registered "
				"for the delayed resend"_q,
				reading);
		},
	});

	runner->add({
		.name = u"rpc retry self-test: a non-500 answer records no retry "
			"row and reaches .fail()"_q,
		.run = [=] {
			state->id400 = state->sender->request(
				MTPhelp_GetConfig()
			).fail([=](const MTP::Error &error) {
				state->failCode400 = error.code();
			}).send();
			state->pending400Before
				= state->instance->hasCallback(state->id400);
			state->mark400 = RpcRetryProbe().mark();

			auto response = MTP::Response();
			response.requestId = state->id400;
			MTPRpcError(MTP_rpc_error(
				MTP_int(400),
				MTP_string("SELFTEST_BAD_REQUEST"))
			).write(response.reply);
			state->instance->processCallback(response);

			state->rows400 = RpcRetryProbe().rowsSince(state->mark400);
			state->pending400After
				= state->instance->hasCallback(state->id400);
		},
		.then = [=] {
			const auto reading = u"requestId=%1 pendingBefore=%2 "
				"pendingAfter=%3 rowsInWindow=%4 failCode=%5"_q.arg(
					QString::number(state->id400),
					state->pending400Before ? u"1"_q : u"0"_q,
					state->pending400After ? u"1"_q : u"0"_q,
					QString::number(int(state->rows400.size())),
					QString::number(state->failCode400));
			Check(
				state->pending400Before,
				u"rpc retry self-test: the synthesized 400 found its "
				"request pending"_q,
				reading);
			if (!state->pending400Before) {
				return;
			}
			RpcRetryProbe().checkNoneSince(
				state->mark400,
				u"rpc retry "_q,
				u"rpc retry self-test: a non-500 answer records no retry "
				"row"_q);
			Check(
				(state->failCode400 == 400) && !state->pending400After,
				u"rpc retry self-test: the synthesized 400 reached the "
				"request's .fail() and unregistered it"_q,
				reading);
		},
	});

	runner->add({
		.name = u"rpc retry self-test: an answer that finds no pending "
			"request records nothing"_q,
		.run = [=] {
			state->orphanId = state->id400 + kOrphanIdDistance;
			state->markNone = RpcRetryProbe().mark();
			state->pendingNoneBefore
				= state->instance->hasCallback(state->orphanId);

			auto response = MTP::Response();
			response.requestId = state->orphanId;
			MTPRpcError(MTP_rpc_error(
				MTP_int(500),
				MTP_string("SELFTEST_ORPHAN"))
			).write(response.reply);
			state->instance->processCallback(response);
		},
		.then = [=] {
			Check(
				!state->pendingNoneBefore,
				u"rpc retry self-test: the orphan request id was really "
				"unknown to the instance"_q,
				u"orphanId=%1 pendingBefore=%2"_q.arg(
					QString::number(state->orphanId),
					state->pendingNoneBefore ? u"1"_q : u"0"_q));
			RpcRetryProbe().checkNoneSince(
				state->markNone,
				u"rpc retry "_q,
				u"rpc retry self-test: an answer for an unknown request id "
				"records no retry row, which is why the pendingBefore "
				"guard on the other legs is not decorative"_q);
		},
	});

	runner->add({
		.name = u"rpc retry self-test: the resent request eventually "
			"answers"_q,
		.run = [=] {
			state->waitStartedAt = crl::now();
		},
		.until = [=] {
			return state->done500
				|| (state->failCode500 != 0)
				|| (crl::now() - state->waitStartedAt > kResendAnswerCap);
		},
		.then = [=] {
			auto answered = u"none"_q;
			if (state->done500) {
				answered = u"done"_q;
			} else if (state->failCode500) {
				answered = u"fail"_q;
			}
			Note(u"rpc retry self-test: the delayed resend the seam left "
				"untouched: answered=%1 code=%2 elapsedMs=%3"_q.arg(
					answered,
					QString::number(state->failCode500),
					QString::number(crl::now() - state->waitStartedAt)));
		},
		.timeout = kDefaultStageTimeout,
	});

	runner->add({
		.name = u"rpc retry self-test: teardown"_q,
		.run = [=] {
			state->sender = nullptr;
			state->instance = nullptr;
			Note(u"rpc retry self-test: a cancelled delayed request leaves "
				"its _delayedRequests entry behind, and checkDelayedRequests "
				"discharges it with one benign \"MTP Error: could not find "
				"request dc for delayed resend\" line in the application "
				"log - pre-existing MTP behaviour, not a defect this seam "
				"introduces"_q);
		},
	});
}

} // namespace Test

#else // _DEBUG

namespace Test {

void RecordRpcRetry(int, const QString &, uint32) {
}

} // namespace Test

#endif // _DEBUG
