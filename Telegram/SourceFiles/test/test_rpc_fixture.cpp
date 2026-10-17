/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_rpc_fixture.h"

#ifdef _DEBUG

#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "mtproto/sender.h"
#include "test/test_agent.h"
#include "test/test_log.h"
#include "test/test_runner.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QThread>

namespace Test {
namespace {

[[nodiscard]] QString RouteName(ControlledRpcDelivery::Route route) {
	switch (route) {
	case ControlledRpcDelivery::Route::Rejected:
		return u"rejected"_q;
	case ControlledRpcDelivery::Route::Success:
		return u"success"_q;
	case ControlledRpcDelivery::Route::Error:
		return u"error"_q;
	case ControlledRpcDelivery::Route::Stale:
		return u"stale"_q;
	}
	return u"unknown"_q;
}

[[nodiscard]] QString DeliveryDetails(const ControlledRpcDelivery &delivery) {
	return u"route=%1 registeredBefore=%2 registeredAfter=%3 "
		"invoked=%4 diagnosis=[%5]"_q.arg(
			RouteName(delivery.route),
			delivery.registeredBefore ? u"1"_q : u"0"_q,
			delivery.registeredAfter ? u"1"_q : u"0"_q,
			delivery.invokedProcessCallback ? u"1"_q : u"0"_q,
			delivery.diagnosis);
}

[[nodiscard]] MTPmessages_Messages EmptyMessages() {
	return MTP_messages_messages(
		MTP_vector<MTPMessage>(0),
		MTP_vector<MTPForumTopic>(0),
		MTP_vector<MTPChat>(0),
		MTP_vector<MTPUser>(0));
}

[[nodiscard]] MTPmessages_Messages NonemptyMessages() {
	return MTP_messages_messages(
		MTP_vector<MTPMessage>(
			1,
			MTP_messageEmpty(
				MTP_flags(0),
				MTP_int(1),
				MTP_peerUser(MTP_long(1)))),
		MTP_vector<MTPForumTopic>(0),
		MTP_vector<MTPChat>(0),
		MTP_vector<MTPUser>(0));
}

[[nodiscard]] mtpBuffer BareEmptyMessagesBuffer() {
	auto buffer = mtpBuffer();
	MTP_messages_messages(
		MTP_vector<MTPMessage>(0),
		MTP_vector<MTPForumTopic>(0),
		MTP_vector<MTPChat>(0),
		MTP_vector<MTPUser>(0)
	).write(buffer);
	return buffer;
}

[[nodiscard]] mtpBuffer TruncatedEmptyMessagesBuffer() {
	auto buffer = mtpBuffer();
	EmptyMessages().write(buffer);
	if (buffer.size() > 1) {
		buffer.resize(buffer.size() - 1);
	}
	return buffer;
}

[[nodiscard]] mtpBuffer TrailingEmptyMessagesBuffer() {
	auto buffer = mtpBuffer();
	EmptyMessages().write(buffer);
	buffer.push_back(0);
	return buffer;
}

[[nodiscard]] mtpBuffer IncompatibleBoolBuffer() {
	auto buffer = mtpBuffer();
	MTPBool(MTP_boolTrue()).write(buffer);
	return buffer;
}

} // namespace

ControlledRpcDelivery DeliverControlledRpcError(
		not_null<MTP::Instance*> instance,
		mtpRequestId requestId,
		int code,
		const QString &type) {
	auto delivery = ControlledRpcDelivery();
	delivery.registeredBefore = instance->hasCallback(requestId);
	auto response = MTP::Response();
	response.requestId = requestId;
	MTPRpcError(MTP_rpc_error(
		MTP_int(code),
		MTP_string(type)
	)).write(response.reply);
	delivery.diagnosis = DiagnoseControlledRpcResult<MTPRpcError>(
		response.reply);
	if (!delivery.diagnosis.isEmpty()) {
		delivery.registeredAfter = instance->hasCallback(requestId);
		return delivery;
	}
	instance->processCallback(response);
	delivery.invokedProcessCallback = true;
	delivery.route = ControlledRpcDelivery::Route::Error;
	delivery.registeredAfter = instance->hasCallback(requestId);
	return delivery;
}

void AppendControlledRpcSelfTest(not_null<Runner*> runner) {
	struct State {
		MTP::Instance *instance = nullptr;
		std::unique_ptr<MTP::Sender> sender;
		ControlledRpcDelivery bare;
		ControlledRpcDelivery truncated;
		ControlledRpcDelivery trailing;
		ControlledRpcDelivery incompatible;
		ControlledRpcDelivery emptySuccess;
		ControlledRpcDelivery nonemptySuccess;
		ControlledRpcDelivery error;
		ControlledRpcDelivery stale;
		mtpRequestId idEmpty = 0;
		mtpRequestId idNonempty = 0;
		mtpRequestId idError = 0;
		mtpRequestId idStale = 0;
		mtpTypeId emptyType = 0;
		mtpTypeId nonemptyInnerType = 0;
		int emptyMessageCount = -1;
		int nonemptyMessageCount = -1;
		int doneEmpty = 0;
		int doneNonempty = 0;
		int doneError = 0;
		int doneStale = 0;
		int failEmpty = 0;
		int failNonempty = 0;
		int failError = 0;
		int failStale = 0;
		int failCodeError = 0;
		bool pendingBeforeCancel = false;
		bool mainThread = false;
	};
	const auto state = new State();

	runner->onFinish([=] {
		state->sender = nullptr;
		state->instance = nullptr;
	});
	runner->waitForSessionReady();

	runner->add({
		.name = u"controlled rpc self-test: reject malformed buffers then "
			"deliver a valid empty boxed result"_q,
		.run = [=] {
			state->instance = &Core::App().domain().active().mtp();
			state->sender = std::make_unique<MTP::Sender>(state->instance);
			state->idEmpty = state->sender->request(
				MTPmessages_GetMessages(MTP_vector<MTPInputMessage>(0))
			).done([=](const MTPmessages_Messages &result) {
				++state->doneEmpty;
				state->emptyType = result.type();
				result.match([&](const MTPDmessages_messages &data) {
					state->emptyMessageCount = int(data.vmessages().v.size());
				}, [&](const auto &) {
					state->emptyMessageCount = -2;
				});
			}).fail([=] {
				++state->failEmpty;
			}).send();
			state->bare = DeliverControlledRpcPrepared<MTPmessages_GetMessages>(
				state->instance,
				state->idEmpty,
				BareEmptyMessagesBuffer());
			state->truncated = DeliverControlledRpcPrepared<
				MTPmessages_GetMessages>(
					state->instance,
					state->idEmpty,
					TruncatedEmptyMessagesBuffer());
			state->trailing = DeliverControlledRpcPrepared<
				MTPmessages_GetMessages>(
					state->instance,
					state->idEmpty,
					TrailingEmptyMessagesBuffer());
			state->incompatible = DeliverControlledRpcPrepared<
				MTPmessages_GetMessages>(
					state->instance,
					state->idEmpty,
					IncompatibleBoolBuffer());
			state->emptySuccess = DeliverControlledRpcSuccess<
				MTPmessages_GetMessages>(
					state->instance,
					state->idEmpty,
					EmptyMessages());
			state->mainThread = (QThread::currentThread()
				== QCoreApplication::instance()->thread());
		},
		.then = [=] {
			const auto pending = state->bare.registeredBefore;
			const auto reading = u"requestId=%1 mainThread=%2 done=%3 "
				"fail=%4 emptyCount=%5 emptyType=0x%6"_q.arg(
					QString::number(state->idEmpty),
					state->mainThread ? u"1"_q : u"0"_q,
					QString::number(state->doneEmpty),
					QString::number(state->failEmpty),
					QString::number(state->emptyMessageCount),
					QString::number(uint32(state->emptyType), 16))
				+ u" bare=[%1] truncated=[%2] trailing=[%3] "
					"incompatible=[%4] success=[%5]"_q.arg(
						DeliveryDetails(state->bare),
						DeliveryDetails(state->truncated),
						DeliveryDetails(state->trailing),
						DeliveryDetails(state->incompatible),
						DeliveryDetails(state->emptySuccess));
			Check(
				pending,
				u"controlled rpc self-test: the empty getMessages request "
				"was registered before any fixture delivery"_q,
				reading);
			if (!pending) {
				return;
			}
			const auto rejected = [](const ControlledRpcDelivery &delivery) {
				return (delivery.route == ControlledRpcDelivery::Route::Rejected)
					&& !delivery.invokedProcessCallback
					&& delivery.registeredAfter
					&& !delivery.diagnosis.isEmpty();
			};
			Check(
				rejected(state->bare)
					&& state->bare.diagnosis.contains(
						u"does not decode"_q),
				u"controlled rpc self-test: a bare messages.messages write "
				"is rejected before processCallback"_q,
				reading);
			Check(
				rejected(state->truncated)
					&& !state->truncated.diagnosis.contains(u"trailing"_q)
					&& !state->truncated.diagnosis.contains(u"empty reply"_q),
				u"controlled rpc self-test: a truncated boxed "
				"messages.Messages buffer is rejected before "
				"processCallback"_q,
				reading);
			Check(
				rejected(state->trailing)
					&& state->trailing.diagnosis.contains(u"trailing words"_q),
				u"controlled rpc self-test: a boxed messages.Messages "
				"buffer with trailing words is rejected before "
				"processCallback"_q,
				reading);
			Check(
				rejected(state->incompatible)
					&& state->incompatible.diagnosis.contains(
						u"does not decode"_q),
				u"controlled rpc self-test: an unrelated boxed MTPBool "
				"buffer is rejected before processCallback"_q,
				reading);
			Check(
				(state->emptySuccess.route
						== ControlledRpcDelivery::Route::Success)
					&& state->emptySuccess.invokedProcessCallback
					&& !state->emptySuccess.registeredAfter
					&& (state->doneEmpty == 1)
					&& (state->failEmpty == 0)
					&& (state->emptyMessageCount == 0)
					&& (state->emptyType == mtpc_messages_messages),
				u"controlled rpc self-test: a valid empty boxed "
				"messages.Messages reaches .done() once after the "
				"rejected buffers left the parser registered"_q,
				reading);
		},
	});

	runner->add({
		.name = u"controlled rpc self-test: a nonempty boxed "
			"messages.Messages reaches .done() once"_q,
		.run = [=] {
			state->idNonempty = state->sender->request(
				MTPmessages_GetMessages(MTP_vector<MTPInputMessage>(0))
			).done([=](const MTPmessages_Messages &result) {
				++state->doneNonempty;
				result.match([&](const MTPDmessages_messages &data) {
					state->nonemptyMessageCount = int(
						data.vmessages().v.size());
					if (!data.vmessages().v.isEmpty()) {
						state->nonemptyInnerType
							= data.vmessages().v.front().type();
					}
				}, [&](const auto &) {
					state->nonemptyMessageCount = -2;
				});
			}).fail([=] {
				++state->failNonempty;
			}).send();
			state->nonemptySuccess = DeliverControlledRpcSuccess<
				MTPmessages_GetMessages>(
					state->instance,
					state->idNonempty,
					NonemptyMessages());
		},
		.then = [=] {
			const auto reading = u"requestId=%1 done=%2 fail=%3 count=%4 "
				"innerType=0x%5 delivery=[%6]"_q.arg(
					QString::number(state->idNonempty),
					QString::number(state->doneNonempty),
					QString::number(state->failNonempty),
					QString::number(state->nonemptyMessageCount),
					QString::number(uint32(state->nonemptyInnerType), 16),
					DeliveryDetails(state->nonemptySuccess));
			Check(
				state->nonemptySuccess.registeredBefore,
				u"controlled rpc self-test: the nonempty getMessages "
				"request was registered before delivery"_q,
				reading);
			if (!state->nonemptySuccess.registeredBefore) {
				return;
			}
			Check(
				(state->nonemptySuccess.route
						== ControlledRpcDelivery::Route::Success)
					&& state->nonemptySuccess.invokedProcessCallback
					&& !state->nonemptySuccess.registeredAfter
					&& (state->doneNonempty == 1)
					&& (state->failNonempty == 0)
					&& (state->nonemptyMessageCount == 1)
					&& (state->nonemptyInnerType == mtpc_messageEmpty),
				u"controlled rpc self-test: a nonempty boxed "
				"messages.Messages reaches .done() once with one "
				"messageEmpty"_q,
				reading);
		},
	});

	runner->add({
		.name = u"controlled rpc self-test: a boxed rpc_error reaches "
			".fail()"_q,
		.run = [=] {
			state->idError = state->sender->request(
				MTPmessages_GetMessages(MTP_vector<MTPInputMessage>(0))
			).done([=] {
				++state->doneError;
			}).fail([=](const MTP::Error &error) {
				++state->failError;
				state->failCodeError = error.code();
			}).send();
			state->error = DeliverControlledRpcError(
				state->instance,
				state->idError,
				400,
				u"SELFTEST_FIXTURE_BAD_REQUEST"_q);
		},
		.then = [=] {
			const auto reading = u"requestId=%1 done=%2 fail=%3 failCode=%4 "
				"delivery=[%5]"_q.arg(
					QString::number(state->idError),
					QString::number(state->doneError),
					QString::number(state->failError),
					QString::number(state->failCodeError),
					DeliveryDetails(state->error));
			Check(
				state->error.registeredBefore,
				u"controlled rpc self-test: the error getMessages request "
				"was registered before delivery"_q,
				reading);
			if (!state->error.registeredBefore) {
				return;
			}
			Check(
				(state->error.route == ControlledRpcDelivery::Route::Error)
					&& state->error.invokedProcessCallback
					&& !state->error.registeredAfter
					&& (state->doneError == 0)
					&& (state->failError == 1)
					&& (state->failCodeError == 400),
				u"controlled rpc self-test: a boxed rpc_error reaches "
				".fail() with code 400 and unregisters the request"_q,
				reading);
		},
	});

	runner->add({
		.name = u"controlled rpc self-test: a canceled request's stale "
			"success invokes no done or fail callback"_q,
		.run = [=] {
			state->idStale = state->sender->request(
				MTPmessages_GetMessages(MTP_vector<MTPInputMessage>(0))
			).done([=] {
				++state->doneStale;
			}).fail([=] {
				++state->failStale;
			}).send();
			state->pendingBeforeCancel
				= state->instance->hasCallback(state->idStale);
			state->sender->request(state->idStale).cancel();
			state->stale = DeliverControlledRpcStale<MTPmessages_GetMessages>(
				state->instance,
				state->idStale,
				EmptyMessages());
		},
		.then = [=] {
			const auto reading = u"requestId=%1 pendingBeforeCancel=%2 "
				"done=%3 fail=%4 delivery=[%5]"_q.arg(
					QString::number(state->idStale),
					state->pendingBeforeCancel ? u"1"_q : u"0"_q,
					QString::number(state->doneStale),
					QString::number(state->failStale),
					DeliveryDetails(state->stale));
			Check(
				state->pendingBeforeCancel && !state->stale.registeredBefore,
				u"controlled rpc self-test: cancel removed the parser "
				"before stale delivery"_q,
				reading);
			Check(
				(state->stale.route == ControlledRpcDelivery::Route::Stale)
					&& state->stale.invokedProcessCallback
					&& (state->doneStale == 0)
					&& (state->failStale == 0)
					&& !state->bare.invokedProcessCallback,
				u"controlled rpc self-test: stale processCallback invokes "
				"no done or fail callback and is distinct from a rejected "
				"malformed buffer"_q,
				reading);
		},
	});

	runner->add({
		.name = u"controlled rpc self-test: teardown"_q,
		.run = [=] {
			state->sender = nullptr;
			state->instance = nullptr;
			Note(u"controlled rpc self-test: onFinish also drops the "
				"Sender if a later stage is skipped"_q);
		},
	});
}

} // namespace Test

#endif // _DEBUG
