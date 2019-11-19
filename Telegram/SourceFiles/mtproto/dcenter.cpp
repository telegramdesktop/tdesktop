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
, _persistentKey(std::move(key)) {
}

DcId Dcenter::id() const {
	return _id;
}

AuthKeyPtr Dcenter::getTemporaryKey() const {
	QReadLocker lock(&_mutex);
	return _temporaryKey;
}

AuthKeyPtr Dcenter::getPersistentKey() const {
	QReadLocker lock(&_mutex);
	return _persistentKey;
}

bool Dcenter::destroyTemporaryKey(uint64 keyId) {
	QWriteLocker lock(&_mutex);
	if (!_temporaryKey || _temporaryKey->keyId() != keyId) {
		return false;
	}
	_temporaryKey = nullptr;
	_connectionInited = false;
	return true;
}

bool Dcenter::destroyConfirmedForgottenKey(uint64 keyId) {
	QWriteLocker lock(&_mutex);
	if (!_persistentKey || _persistentKey->keyId() != keyId) {
		return false;
	}
	_temporaryKey = nullptr;
	_persistentKey = nullptr;
	_connectionInited = false;
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
	if (_temporaryKey != nullptr) {
		return false;
	}
	auto expected = false;
	return _creatingKey.compare_exchange_strong(expected, true);
}

void Dcenter::releaseKeyCreationOnFail() {
	Expects(_creatingKey);
	Expects(_temporaryKey == nullptr);

	_creatingKey = false;
}

void Dcenter::releaseKeyCreationOnDone(
		const AuthKeyPtr &temporaryKey,
		const AuthKeyPtr &persistentKey) {
	Expects(_creatingKey);
	Expects(_temporaryKey == nullptr);

	QWriteLocker lock(&_mutex);
	DEBUG_LOG(("AuthKey Info: Dcenter::releaseKeyCreationOnDone(%1, %2), "
		"emitting authKeyChanged, dc %3"
		).arg(temporaryKey ? temporaryKey->keyId() : 0
		).arg(persistentKey ? persistentKey->keyId() : 0
		).arg(_id));
	_temporaryKey = temporaryKey;
	if (persistentKey) {
		_persistentKey = persistentKey;
	}
	_connectionInited = false;
	_creatingKey = false;
}

} // namespace internal
} // namespace MTP
