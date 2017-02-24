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
#pragma once

#include "core/single_timer.h"
#include "mtproto/rpc_sender.h"
#include "mtproto/auth_key.h"

namespace MTP {

class Instance;

namespace internal {

class Dcenter : public QObject {
	Q_OBJECT

public:
	Dcenter(Instance *instance, DcId dcId, AuthKeyPtr &&key);

	QReadWriteLock *keyMutex() const;
	const AuthKeyPtr &getKey() const;
	void setKey(AuthKeyPtr &&key);
	void destroyKey();

	bool connectionInited() const {
		QMutexLocker lock(&initLock);
		bool res = _connectionInited;
		return res;
	}
	void setConnectionInited(bool connectionInited = true) {
		QMutexLocker lock(&initLock);
		_connectionInited = connectionInited;
	}

signals:
	void authKeyCreated();
	void layerWasInited(bool was);

private slots:
	void authKeyWrite();

private:
	mutable QReadWriteLock keyLock;
	mutable QMutex initLock;
	Instance *_instance = nullptr;
	DcId _id = 0;
	AuthKeyPtr _key;
	bool _connectionInited = false;

};

using DcenterPtr = std::shared_ptr<Dcenter>;
using DcenterMap = std::map<DcId, DcenterPtr>;

class ConfigLoader : public QObject {
	Q_OBJECT

public:
	ConfigLoader(Instance *instance, RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail);
	~ConfigLoader();

	void load();

public slots:
	void enumDC();

private:
	mtpRequestId sendRequest(ShiftedDcId shiftedDcId);

	Instance *_instance = nullptr;
	SingleTimer _enumDCTimer;
	DcId _enumCurrent = 0;
	mtpRequestId _enumRequest = 0;

	RPCDoneHandlerPtr _doneHandler;
	RPCFailHandlerPtr _failHandler;

};

} // namespace internal
} // namespace MTP
