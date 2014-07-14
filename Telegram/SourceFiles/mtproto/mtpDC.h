/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
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

	bool needConnectionInit() {
		QMutexLocker lock(&initLock);
		if (_connectionInited || _connectionInitSent) return false;
		_connectionInitSent = true;
		return true;
	}

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

private slots:

	void authKeyWrite();

private:

	mutable QReadWriteLock keyLock;
	mutable QMutex initLock;
	int32 _id;
	mtpAuthKeyPtr _key;
	bool _connectionInited;
	bool _connectionInitSent;
};

typedef QSharedPointer<MTProtoDC> MTProtoDCPtr;
typedef QMap<uint32, MTProtoDCPtr> MTProtoDCMap;

struct mtpDcOption {
	mtpDcOption(int _id, const string &_host, const string &_ip, int _port) : id(_id), host(_host), ip(_ip), port(_port) {
	}

	int id;
	string host;
	string ip;
	int port;
};
typedef QMap<int, mtpDcOption> mtpDcOptions;

class MTProtoConfigLoader : public QObject {
	Q_OBJECT

public:

	void load();

signals:

	void loaded();

};

MTProtoConfigLoader *mtpConfigLoader();

const mtpDcOptions &mtpDCOptions();
MTProtoDCMap &mtpDCMap();
bool mtpNeedConfig();
int32 mtpMainDC();
void mtpLogoutOtherDCs();
void mtpSetDC(int32 dc);
uint32 mtpMaxChatSize();

void mtpWriteAuthKeys();
void mtpLoadData();
int32 mtpAuthed();
void mtpAuthed(int32 uid);

void mtpWriteConfig(QDataStream &stream);
bool mtpReadConfigElem(int32 blockId, QDataStream &stream);
