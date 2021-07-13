/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_bound_key_creator.h"

#include "mtproto/details/mtproto_serialized_request.h"

namespace MTP::details {

BoundKeyCreator::BoundKeyCreator(DcKeyRequest request, Delegate delegate)
: _request(request)
, _delegate(std::move(delegate)) {
}

void BoundKeyCreator::start(
		DcId dcId,
		int16 protocolDcId,
		not_null<AbstractConnection*> connection,
		not_null<DcOptions*> dcOptions) {
	Expects(!_creator.has_value());

	auto delegate = DcKeyCreator::Delegate();
	delegate.done = _delegate.unboundReady;
	delegate.sentSome = _delegate.sentSome;
	delegate.receivedSome = _delegate.receivedSome;

	_creator.emplace(
		dcId,
		protocolDcId,
		connection,
		dcOptions,
		std::move(delegate),
		_request);
}

void BoundKeyCreator::stop() {
	_creator = std::nullopt;
}

void BoundKeyCreator::bind(AuthKeyPtr &&persistentKey) {
	stop();
	_binder.emplace(std::move(persistentKey));
}

void BoundKeyCreator::restartBinder() {
	if (_binder) {
		_binder.emplace(_binder->persistentKey());
	}
}

bool BoundKeyCreator::readyToBind() const {
	return _binder.has_value();
}

SerializedRequest BoundKeyCreator::prepareBindRequest(
		const AuthKeyPtr &temporaryKey,
		uint64 sessionId) {
	Expects(_binder.has_value());

	return _binder->prepareRequest(temporaryKey, sessionId);
}

DcKeyBindState BoundKeyCreator::handleBindResponse(
		const mtpBuffer &response) {
	Expects(_binder.has_value());

	return _binder->handleResponse(response);
}

AuthKeyPtr BoundKeyCreator::bindPersistentKey() const {
	Expects(_binder.has_value());

	return _binder->persistentKey();
}

bool IsDestroyedTemporaryKeyError(const mtpBuffer &buffer) {
	auto from = buffer.data();
	auto error = MTPRpcError();
	if (!error.read(from, from + buffer.size())) {
		return false;
	}
	return error.match([&](const MTPDrpc_error &data) {
		return (data.verror_code().v == 401)
			&& (data.verror_message().v == "AUTH_KEY_PERM_EMPTY");
	});
}

} // namespace MTP::details
