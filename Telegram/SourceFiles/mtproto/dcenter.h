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

namespace MTP {
namespace internal {

class Dcenter : public QObject {
	Q_OBJECT

public:
	Dcenter(int32 id, const AuthKeyPtr &key);

	QReadWriteLock *keyMutex() const;
	const AuthKeyPtr &getKey() const;
	void setKey(const AuthKeyPtr &key);
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
	int32 _id;
	AuthKeyPtr _key;
	bool _connectionInited;

};

typedef QSharedPointer<Dcenter> DcenterPtr;
typedef QMap<uint32, DcenterPtr> DcenterMap;

class ConfigLoader : public QObject {
	Q_OBJECT

public:
	ConfigLoader();
	void load();
	void done();

public slots:
	void enumDC();

signals:
	void loaded();

private:
	SingleTimer _enumDCTimer;
	int32 _enumCurrent;
	mtpRequestId _enumRequest;

};

ConfigLoader *configLoader();
void destroyConfigLoader();

DcenterMap &DCMap();
bool configNeeded();
int32 mainDC();
void logoutOtherDCs();
void setDC(int32 dc, bool firstOnly = false);

int32 authed();
void authed(int32 uid);

AuthKeysMap getAuthKeys();
void setAuthKey(int32 dc, AuthKeyPtr key);

void updateDcOptions(const QVector<MTPDcOption> &options);
QReadWriteLock *dcOptionsMutex();

} // namespace internal
} // namespace MTP
