/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "mtproto/mtp_instance.h"
#include "tl/tl_boxed.h"

namespace Test {

class Runner;

// One attempt to deliver a controlled RPC answer to a registered request.
// Success and prepared routes require a live parser and a buffer that
// decodes completely as Request::ResponseType; they never call
// processCallback on a diagnosis, so a later valid answer can still
// find that parser. Error still goes through processCallback with a
// boxed rpc_error whether or not the id is registered. Stale is the
// intentional canceled/unknown-id success route: it validates the boxed
// result, then calls processCallback even when hasCallback is false.
struct ControlledRpcDelivery {
	enum class Route {
		Rejected,
		Success,
		Error,
		Stale,
	};

	Route route = Route::Rejected;
	bool registeredBefore = false;
	bool registeredAfter = false;
	bool invokedProcessCallback = false;
	QString diagnosis;
};

// Empty, truncated, trailing, or wrong-constructor buffers. Trailing
// words are a harness-strict refusal: production MakeDoneHandler does
// not require from == end after a successful read.
template <typename Result>
[[nodiscard]] QString DiagnoseControlledRpcResult(const mtpBuffer &buffer) {
	static_assert(tl::is_boxed_v<Result>);
	if (buffer.isEmpty()) {
		return u"empty reply buffer"_q;
	}
	auto result = Result();
	auto from = buffer.constData();
	const auto end = from + buffer.size();
	if (!result.read(from, end)) {
		return u"reply does not decode as the expected boxed result"_q;
	} else if (from != end) {
		return u"reply has trailing words after the boxed result leftover=%1"_q.arg(
			int(end - from));
	}
	return QString();
}

template <typename Request>
[[nodiscard]] ControlledRpcDelivery DeliverControlledRpcPrepared(
		not_null<MTP::Instance*> instance,
		mtpRequestId requestId,
		const mtpBuffer &reply) {
	static_assert(tl::is_boxed_v<Request>);
	static_assert(tl::is_boxed_v<typename Request::ResponseType>);

	auto delivery = ControlledRpcDelivery();
	delivery.registeredBefore = instance->hasCallback(requestId);
	delivery.diagnosis = DiagnoseControlledRpcResult<
		typename Request::ResponseType>(reply);
	if (!delivery.diagnosis.isEmpty()) {
		delivery.registeredAfter = instance->hasCallback(requestId);
		return delivery;
	} else if (!delivery.registeredBefore) {
		delivery.diagnosis = u"request is not registered"_q;
		delivery.registeredAfter = false;
		return delivery;
	}
	auto response = MTP::Response();
	response.reply = reply;
	response.requestId = requestId;
	instance->processCallback(response);
	delivery.invokedProcessCallback = true;
	delivery.route = ControlledRpcDelivery::Route::Success;
	delivery.registeredAfter = instance->hasCallback(requestId);
	return delivery;
}

template <typename Request>
[[nodiscard]] ControlledRpcDelivery DeliverControlledRpcSuccess(
		not_null<MTP::Instance*> instance,
		mtpRequestId requestId,
		const typename Request::ResponseType &result) {
	static_assert(tl::is_boxed_v<Request>);
	static_assert(tl::is_boxed_v<typename Request::ResponseType>);

	auto buffer = mtpBuffer();
	result.write(buffer);
	return DeliverControlledRpcPrepared<Request>(
		instance,
		requestId,
		buffer);
}

template <typename Request>
[[nodiscard]] ControlledRpcDelivery DeliverControlledRpcStale(
		not_null<MTP::Instance*> instance,
		mtpRequestId requestId,
		const typename Request::ResponseType &result) {
	static_assert(tl::is_boxed_v<Request>);
	static_assert(tl::is_boxed_v<typename Request::ResponseType>);

	auto buffer = mtpBuffer();
	result.write(buffer);
	auto delivery = ControlledRpcDelivery();
	delivery.registeredBefore = instance->hasCallback(requestId);
	delivery.diagnosis = DiagnoseControlledRpcResult<
		typename Request::ResponseType>(buffer);
	if (!delivery.diagnosis.isEmpty()) {
		delivery.registeredAfter = instance->hasCallback(requestId);
		return delivery;
	}
	auto response = MTP::Response();
	response.reply = buffer;
	response.requestId = requestId;
	instance->processCallback(response);
	delivery.invokedProcessCallback = true;
	delivery.route = ControlledRpcDelivery::Route::Stale;
	delivery.registeredAfter = instance->hasCallback(requestId);
	return delivery;
}

// Boxed rpc_error through processCallback. Does not require a live parser,
// so the retry self-test's unknown-id 500 still reaches the seam.
[[nodiscard]] ControlledRpcDelivery DeliverControlledRpcError(
	not_null<MTP::Instance*> instance,
	mtpRequestId requestId,
	int code,
	const QString &type);

// The helper measuring itself. A bare TL constructor factory omits its
// constructor word; MakeDoneHandler then fails Result::read after it has
// already called senderRequestHandled, and processCallback treats that as
// RESPONSE_PARSE_FAILED and unregisters the request, so a fixture mistake
// looks like an empty-result or stale-callback success. This self-test
// sends harmless MTPmessages_GetMessages requests through an ordinary
// MTP::Sender and delivers synthesized answers only through the helpers
// above. Bare, truncated, trailing and incompatible buffers must be
// rejected without processCallback on one still-registered id, which a
// later valid empty boxed messages.Messages must then reach as .done()
// exactly once; a nonempty boxed answer, a code-400 rpc_error, and a
// cancel-then-stale success are separate sends, each finished in the
// same .run turn that issued them so a live DC cannot consume the parser
// between stages. Every success/error leg fails closed if hasCallback
// was false before delivery. It needs no wallet, no fixture secret, no
// chats list and no funded value; the only thing it asks of the process
// is a ready session. It emits no deliberate failure.
void AppendControlledRpcSelfTest(not_null<Runner*> runner);

} // namespace Test
