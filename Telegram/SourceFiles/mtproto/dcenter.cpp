/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/dcenter.h"

#include "mtproto/facade.h"
#include "mtproto/mtproto_auth_key.h"
#include "mtproto/dc_options.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/special_config_request.h"
#include "storage/localstorage.h"

namespace MTP {
namespace internal {
namespace {

constexpr auto kEnumerateDcTimeout = 8000; // 8 seconds timeout for help_getConfig to work (then move to other dc)
constexpr auto kSpecialRequestTimeoutMs = 6000; // 4 seconds timeout for it to work in a specially requested dc.

} // namespace

Dcenter::Dcenter(DcId dcId, AuthKeyPtr &&key)
: _id(dcId)
, _key(std::move(key)) {
}

DcId Dcenter::id() const {
	return _id;
}

AuthKeyPtr Dcenter::getKey() const {
	QReadLocker lock(&_mutex);
	return _key;
}

void Dcenter::destroyCdnKey(uint64 keyId) {
	destroyKey(keyId);
}

bool Dcenter::destroyConfirmedForgottenKey(uint64 keyId) {
	return destroyKey(keyId);
}

bool Dcenter::destroyKey(uint64 keyId) {
	Expects(!_creatingKey || !_key);

	QWriteLocker lock(&_mutex);
	if (_key->keyId() != keyId) {
		return false;
	}
	_key = nullptr;
	_connectionInited = false;
	lock.unlock();

	emit authKeyChanged();
	return true;
}

bool Dcenter::connectionInited() const {
	QReadLocker lock(&_mutex);
	return _connectionInited;
}

void Dcenter::setConnectionInited(bool connectionInited) {
	QWriteLocker lock(&_mutex);
	_connectionInited = connectionInited;
}

bool Dcenter::acquireKeyCreation() {
	QReadLocker lock(&_mutex);
	if (_key != nullptr) {
		return false;
	}
	auto expected = false;
	return _creatingKey.compare_exchange_strong(expected, true);
}

void Dcenter::releaseKeyCreationOnFail() {
	Expects(_creatingKey);
	Expects(_key == nullptr);

	_creatingKey = false;
}

void Dcenter::releaseKeyCreationOnDone(AuthKeyPtr &&key) {
	Expects(_creatingKey);
	Expects(_key == nullptr);

	QWriteLocker lock(&_mutex);
	DEBUG_LOG(("AuthKey Info: Dcenter::releaseKeyCreationOnDone(%1), emitting authKeyChanged, dc %2").arg(key ? key->keyId() : 0).arg(_id));
	_key = std::move(key);
	_connectionInited = false;
	_creatingKey = false;
	lock.unlock();

	emit authKeyChanged();
}

} // namespace internal
} // namespace MTP
