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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

class MTProtoDC : public QObject {
	Q_OBJECT

public:

	MTProtoDC(int32 id, const mtpAuthKeyPtr &key);

	QReadWriteLock *keyMutex() const;
	const mtpAuthKeyPtr &getKey() const;
	void setKey(const mtpAuthKeyPtr &key);
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
	mtpAuthKeyPtr _key;
	bool _connectionInited;
};

typedef QSharedPointer<MTProtoDC> MTProtoDCPtr;
typedef QMap<uint32, MTProtoDCPtr> MTProtoDCMap;

class MTProtoConfigLoader : public QObject {
	Q_OBJECT

public:

	MTProtoConfigLoader();
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

MTProtoConfigLoader *mtpConfigLoader();
void mtpDestroyConfigLoader();

MTProtoDCMap &mtpDCMap();
bool mtpNeedConfig();
int32 mtpMainDC();
void mtpLogoutOtherDCs();
void mtpSetDC(int32 dc, bool firstOnly = false);
uint32 mtpMaxChatSize();

int32 mtpAuthed();
void mtpAuthed(int32 uid);

mtpKeysMap mtpGetKeys();
void mtpSetKey(int32 dc, mtpAuthKeyPtr key);

void mtpUpdateDcOptions(const QVector<MTPDcOption> &options);
QReadWriteLock *mtpDcOptionsMutex();
