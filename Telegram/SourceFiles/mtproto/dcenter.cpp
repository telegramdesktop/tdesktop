/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "mtproto/dcenter.h"

#include "mtproto/facade.h"
#include "mtproto/dc_options.h"
#include "mtproto/mtp_instance.h"
#include "storage/localstorage.h"

namespace MTP {
namespace internal {
namespace {

constexpr auto kEnumerateDcTimeout = 8000; // 8 seconds timeout for help_getConfig to work (then move to other dc)

} // namespace

Dcenter::Dcenter(Instance *instance, DcId dcId, AuthKeyPtr &&key)
: _instance(instance)
, _id(dcId)
, _key(std::move(key)) {
	connect(this, SIGNAL(authKeyCreated()), this, SLOT(authKeyWrite()), Qt::QueuedConnection);
}

void Dcenter::authKeyWrite() {
	DEBUG_LOG(("AuthKey Info: MTProtoDC::authKeyWrite() slot, dc %1").arg(_id));
	if (_key) {
		Local::writeMtpData();
	}
}

void Dcenter::setKey(AuthKeyPtr &&key) {
	DEBUG_LOG(("AuthKey Info: MTProtoDC::setKey(%1), emitting authKeyCreated, dc %2").arg(key ? key->keyId() : 0).arg(_id));
	_key = std::move(key);
	_connectionInited = false;
	_instance->setKeyForWrite(_id, _key);
	emit authKeyCreated();
}

QReadWriteLock *Dcenter::keyMutex() const {
	return &keyLock;
}

const AuthKeyPtr &Dcenter::getKey() const {
	return _key;
}

void Dcenter::destroyKey() {
	setKey(AuthKeyPtr());
}

ConfigLoader::ConfigLoader(Instance *instance, RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail) : _instance(instance)
, _doneHandler(onDone)
, _failHandler(onFail) {
	connect(&_enumDCTimer, SIGNAL(timeout()), this, SLOT(enumDC()));
}

void ConfigLoader::load() {
	if (!_instance->isKeysDestroyer()) {
		sendRequest(_instance->mainDcId());
		_enumDCTimer.start(kEnumerateDcTimeout);
	} else {
		auto ids = _instance->dcOptions()->sortedDcIds();
		t_assert(!ids.empty());
		_enumCurrent = ids.front();
		enumDC();
	}
}

mtpRequestId ConfigLoader::sendRequest(ShiftedDcId shiftedDcId) {
	return _instance->send(MTPhelp_GetConfig(), _doneHandler, _failHandler, shiftedDcId);
}

ConfigLoader::~ConfigLoader() {
	if (_enumRequest) {
		_instance->cancel(_enumRequest);
	}
	if (_enumCurrent) {
		_instance->killSession(MTP::configDcId(_enumCurrent));
	}
}

void ConfigLoader::enumDC() {
	if (_enumRequest) {
		_instance->cancel(_enumRequest);
	}

	if (!_enumCurrent) {
		_enumCurrent = _instance->mainDcId();
	} else {
		_instance->killSession(MTP::configDcId(_enumCurrent));
	}
	auto ids = _instance->dcOptions()->sortedDcIds();
	t_assert(!ids.empty());

	auto i = std::find(ids.cbegin(), ids.cend(), _enumCurrent);
	if (i == ids.cend() || (++i) == ids.cend()) {
		_enumCurrent = ids.front();
	} else {
		_enumCurrent = *i;
	}
	_enumRequest = sendRequest(MTP::configDcId(_enumCurrent));

	_enumDCTimer.start(kEnumerateDcTimeout);
}

} // namespace internal
} // namespace MTP
