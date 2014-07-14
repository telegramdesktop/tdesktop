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
#include "stdafx.h"
#include "mtpDC.h"
#include "mtp.h"

namespace {

	MTProtoDCMap gDCs;
	bool configLoadedOnce = false;
	int32 mainDC = 1;
	int userId = 0;
	mtpDcOptions gDCOptions;

	typedef QMap<int32, mtpAuthKeyPtr> _KeysMapForWrite;
	_KeysMapForWrite _keysMapForWrite;
	QMutex _keysMapForWriteMutex;

	int32 readAuthKeysFields(QIODevice *io) {
		if (!io->isOpen()) io->open(QIODevice::ReadOnly);

		QDataStream stream(io);
		stream.setVersion(QDataStream::Qt_5_1);

		int32 oldFound = 0;

		while (true) {
			quint32 blockId;
			stream >> blockId;
			if (stream.status() == QDataStream::ReadPastEnd) {
				DEBUG_LOG(("MTP Info: keys file read end"));
				break;
			} else if (stream.status() != QDataStream::Ok) {
				LOG(("MTP Error: could not read block id, status: %1 - keys file is corrupted?..").arg(stream.status()));
				break;
			}

			if (blockId == dbiVersion) {
				qint32 keysVersion;
				stream >> keysVersion;
				continue; // should not be in encrypted part, just ignore
			}

			if (blockId != dbiEncrypted && blockId != dbiKey) {
				oldFound = 2;
			}

			switch (blockId) {
			case dbiEncrypted: {
				QByteArray data, decrypted;
				stream >> data;

				if (!MTP::localKey().created()) {
					LOG(("MTP Error: reading encrypted keys without local key!"));
					continue;
				}

				if (data.size() <= 16 || (data.size() & 0x0F)) {
					LOG(("MTP Error: bad encrypted part size: %1").arg(data.size()));
					continue;
				}
				uint32 fullDataLen = data.size() - 16;
				decrypted.resize(fullDataLen);
				const char *dataKey = data.constData(), *encrypted = data.constData() + 16;
				aesDecryptLocal(encrypted, decrypted.data(), fullDataLen, &MTP::localKey(), dataKey);
				uchar sha1Buffer[20];
				if (memcmp(hashSha1(decrypted.constData(), decrypted.size(), sha1Buffer), dataKey, 16)) {
					LOG(("MTP Error: bad decrypt key, data from user-config not decrypted"));
					continue;
				}
				uint32 dataLen = *(const uint32*)decrypted.constData();
				if (dataLen > uint32(decrypted.size()) || dataLen <= fullDataLen - 16 || dataLen < 4) {
					LOG(("MTP Error: bad decrypted part size: %1, fullDataLen: %2, decrypted size: %3").arg(dataLen).arg(fullDataLen).arg(decrypted.size()));
					continue;
				}
				decrypted.resize(dataLen);
				QBuffer decryptedStream(&decrypted);
				decryptedStream.open(QIODevice::ReadOnly);
				decryptedStream.seek(4); // skip size
				readAuthKeysFields(&decryptedStream);
			} break;

			case dbiKey: {
				qint32 dcId;
				quint32 key[64];
				stream >> dcId;
				stream.readRawData((char*)key, 256);
				if (stream.status() == QDataStream::Ok) {
					DEBUG_LOG(("MTP Info: key found, dc %1, key: %2").arg(dcId).arg(mb(key, 256).str()));
					dcId = dcId % _mtp_internal::dcShift;
					mtpAuthKeyPtr keyPtr(new mtpAuthKey());
					keyPtr->setKey(key);
					keyPtr->setDC(dcId);

					MTProtoDCPtr dc(new MTProtoDC(dcId, keyPtr));
					gDCs.insert(dcId, dc);
				}
			} break;

			case dbiUser: {
				quint32 dcId;
				qint32 uid;
				stream >> uid >> dcId;
				if (stream.status() == QDataStream::Ok) {
					DEBUG_LOG(("MTP Info: user found, dc %1, uid %2").arg(dcId).arg(uid));

					userId = uid;
					mainDC = dcId;
				}
			} break;

			case dbiDcOption: {
				quint32 dcId, port;
				QString host, ip;
				stream >> dcId >> host >> ip >> port;

				if (stream.status() == QDataStream::Ok) {
					gDCOptions.insert(dcId, mtpDcOption(dcId, host.toUtf8().constData(), ip.toUtf8().constData(), port));
				}
			} break;

			case dbiConfig1: {
				quint32 maxSize;
				stream >> maxSize;
				if (stream.status() == QDataStream::Ok) {
					cSetMaxGroupCount(maxSize);
				}
			} break;
			}

			if (stream.status() != QDataStream::Ok) {
				LOG(("MTP Error: could not read data, status: %1 - keys file is corrupted?..").arg(stream.status()));
				break;
			}
		}

		return oldFound;
	}

	int32 readAuthKeys(QFile &file) {
		QDataStream stream(&file);
		stream.setVersion(QDataStream::Qt_5_1);

		int32 oldFound = 0;
		quint32 blockId;
		stream >> blockId;
		if (stream.status() == QDataStream::ReadPastEnd) {
			DEBUG_LOG(("MTP Info: keys file read end"));
			return oldFound;
		} else if (stream.status() != QDataStream::Ok) {
			LOG(("MTP Error: could not read block id, status: %1 - keys file is corrupted?..").arg(stream.status()));
			return oldFound;
		}

		if (blockId == dbiVersion) {
			qint32 keysVersion;
			stream >> keysVersion;
			if (keysVersion > AppVersion) return oldFound;

			stream >> blockId;
			if (stream.status() == QDataStream::ReadPastEnd) {
				DEBUG_LOG(("MTP Info: keys file read end"));
				return oldFound;
			} else if (stream.status() != QDataStream::Ok) {
				LOG(("MTP Error: could not read block id, status: %1 - keys file is corrupted?..").arg(stream.status()));
				return oldFound;
			}
			if (blockId != dbiEncrypted) {
				oldFound = (blockId != dbiKey) ? 2 : 1;
			}
		} else {
			oldFound = 2;
		}

		file.reset();
		oldFound = qMax(oldFound, readAuthKeysFields(&file));

		return oldFound;
	}

	void writeAuthKeys();
	void readAuthKeys() {
		QFile keysFile(cWorkingDir() + cDataFile());
		if (keysFile.open(QIODevice::ReadOnly)) {
			DEBUG_LOG(("MTP Info: keys file opened for reading"));
			int32 oldFound = readAuthKeys(keysFile);

			if (gDCOptions.isEmpty() || (mainDC && gDCOptions.find(mainDC) == gDCOptions.cend())) { // load first dc info
				gDCOptions.insert(1, mtpDcOption(1, "", cFirstDCIp(), cFirstDCPort()));
				userId = 0;
				mainDC = 0;
				DEBUG_LOG(("MTP Info: first DC connect options: %1:%2").arg(cFirstDCIp()).arg(cFirstDCPort()));
			} else {
				configLoadedOnce = true;
				DEBUG_LOG(("MTP Info: config loaded, dc option count: %1").arg(gDCOptions.size()));
			}

			if (oldFound > 0) {
				writeAuthKeys();
				if (oldFound > 1) {
					App::writeUserConfig();
				}
				DEBUG_LOG(("MTP Info: rewritten old data / config to new data and config"));
			}
		} else {
			DEBUG_LOG(("MTP Info: could not open keys file for reading"));
			gDCOptions.insert(1, mtpDcOption(1, "", cFirstDCIp(), cFirstDCPort()));
			DEBUG_LOG(("MTP Info: first DC connect options: %1:%2").arg(cFirstDCIp()).arg(cFirstDCPort()));
		}
	}

	typedef QVector<mtpAuthKeyPtr> _KeysToWrite;
	void writeAuthKeys() {
		_KeysToWrite keysToWrite;
		{
			QMutexLocker lock(&_keysMapForWriteMutex);
			for (_KeysMapForWrite::const_iterator i = _keysMapForWrite.cbegin(), e = _keysMapForWrite.cend(); i != e; ++i) {
				keysToWrite.push_back(i.value());
			}
		}

		QFile keysFile(cWorkingDir() + cDataFile());
		if (keysFile.open(QIODevice::WriteOnly)) {
			DEBUG_LOG(("MTP Info: writing keys data for encrypt"));
			QByteArray toEncrypt;
			toEncrypt.reserve(65536);
			toEncrypt.resize(4);
			{
				QBuffer buffer(&toEncrypt);
				buffer.open(QIODevice::Append);

				QDataStream stream(&buffer);
				stream.setVersion(QDataStream::Qt_5_1);

				for (_KeysToWrite::const_iterator i = keysToWrite.cbegin(), e = keysToWrite.cend(); i != e; ++i) {
					stream << quint32(dbiKey) << quint32((*i)->getDC());
					(*i)->write(stream);
				}

				if (stream.status() != QDataStream::Ok) {
					LOG(("MTP Error: could not write keys to memory buf, status: %1").arg(stream.status()));
				}
			}
			*(uint32*)(toEncrypt.data()) = toEncrypt.size();

			uint32 size = toEncrypt.size(), fullSize = size;
			if (fullSize & 0x0F) {
				fullSize += 0x10 - (fullSize & 0x0F);
				toEncrypt.resize(fullSize);
				memset_rand(toEncrypt.data() + size, fullSize - size);
			}
			QByteArray encrypted(16 + fullSize, Qt::Uninitialized); // 128bit of sha1 - key128, sizeof(data), data
			hashSha1(toEncrypt.constData(), toEncrypt.size(), encrypted.data());
			aesEncryptLocal(toEncrypt.constData(), encrypted.data() + 16, fullSize, &MTP::localKey(), encrypted.constData());

			DEBUG_LOG(("MTP Info: keys file opened for writing %1 keys").arg(keysToWrite.size()));
			QDataStream keysStream(&keysFile);
			keysStream.setVersion(QDataStream::Qt_5_1);
			keysStream << quint32(dbiVersion) << qint32(AppVersion);

			keysStream << quint32(dbiEncrypted) << encrypted; // write all encrypted data

			if (keysStream.status() != QDataStream::Ok) {
				LOG(("MTP Error: could not write keys, status: %1").arg(keysStream.status()));
			}
		} else {
			LOG(("MTP Error: could not open keys file for writing"));
		}
	}

	class _KeysReader {
	public:
		_KeysReader() {
			readAuthKeys();
		}
	};

}

void mtpLoadData() {
	static _KeysReader keysReader;
}

int32 mtpAuthed() {
	return userId;
}

void mtpAuthed(int32 uid) {
	if (userId != uid && mainDC) {
		userId = uid;
		App::writeUserConfig();
	}
}

MTProtoDCMap &mtpDCMap() {
	return gDCs;
}

const mtpDcOptions &mtpDCOptions() {
	return gDCOptions;
}

bool mtpNeedConfig() {
	return !configLoadedOnce;
}

int32 mtpMainDC() {
	return mainDC;
}

void mtpLogoutOtherDCs() {
	QList<int32> dcs;
	{
		QMutexLocker lock(&_keysMapForWriteMutex);
		dcs = _keysMapForWrite.keys();
	}
	for (int32 i = 0, cnt = dcs.size(); i != cnt; ++i) {
		if (dcs[i] != MTP::maindc()) {
			MTP::send(MTPauth_LogOut(), RPCResponseHandler(), dcs[i]);
		}
	}
}

void mtpSetDC(int32 dc) {
	if (dc != mainDC) {
		mainDC = dc;
		if (userId) {
			App::writeUserConfig();
		}
	}
}

MTProtoDC::MTProtoDC(int32 id, const mtpAuthKeyPtr &key) : _id(id), _key(key), _connectionInited(false), _connectionInitSent(false) {
	connect(this, SIGNAL(authKeyCreated()), this, SLOT(authKeyWrite()), Qt::QueuedConnection);

	QMutexLocker lock(&_keysMapForWriteMutex);
	if (_key) {
		_keysMapForWrite[_id] = _key;
	} else {
		_keysMapForWrite.remove(_id);
	}
}

void MTProtoDC::authKeyWrite() {
	DEBUG_LOG(("AuthKey Info: MTProtoDC::authKeyWrite() slot, dc %1").arg(_id));
	if (_key) {
		writeAuthKeys();
	}
}

void MTProtoDC::setKey(const mtpAuthKeyPtr &key) {
	DEBUG_LOG(("AuthKey Info: MTProtoDC::setKey(%1), emitting authKeyCreated, dc %2").arg(key ? key->keyId() : 0).arg(_id));
	_key = key;
	emit authKeyCreated();

	QMutexLocker lock(&_keysMapForWriteMutex);
	if (_key) {
		_keysMapForWrite[_id] = _key;
	} else {
		_keysMapForWrite.remove(_id);
	}
}

QReadWriteLock *MTProtoDC::keyMutex() const {
	return &keyLock;
}

const mtpAuthKeyPtr &MTProtoDC::getKey() const {
	return _key;
}

void MTProtoDC::destroyKey() {
	setKey(mtpAuthKeyPtr());

	QMutexLocker lock(&_keysMapForWriteMutex);
	_keysMapForWrite.remove(_id);
}

namespace {
	MTProtoConfigLoader configLoader;
	bool loadingConfig = false;
	void configLoaded(const MTPConfig &result) {
		loadingConfig = false;

		const MTPDconfig &data(result.c_config());

		DEBUG_LOG(("MTP Info: got config, chat_size_max: %1, date: %2, test_mode: %3, this_dc: %4, dc_options.length: %5").arg(data.vchat_size_max.v).arg(data.vdate.v).arg(data.vtest_mode.v).arg(data.vthis_dc.v).arg(data.vdc_options.c_vector().v.size()));

		QSet<int32> already;
		const QVector<MTPDcOption> &options(data.vdc_options.c_vector().v);
		for (QVector<MTPDcOption>::const_iterator i = options.cbegin(), e = options.cend(); i != e; ++i) {
			const MTPDdcOption &optData(i->c_dcOption());
			if (already.constFind(optData.vid.v) == already.cend()) {
				already.insert(optData.vid.v);
				gDCOptions.insert(optData.vid.v, mtpDcOption(optData.vid.v, optData.vhostname.c_string().v, optData.vip_address.c_string().v, optData.vport.v));
			}
		}
		cSetMaxGroupCount(data.vchat_size_max.v);

		configLoadedOnce = true;
		App::writeUserConfig();

		emit mtpConfigLoader()->loaded();
	}
	bool configFailed(const RPCError &err) {
		loadingConfig = false;
		LOG(("MTP Error: failed to get config!"));
		return false;
	}
};

void MTProtoConfigLoader::load() {
	if (loadingConfig) return;
	loadingConfig = true;

	MTPhelp_GetConfig request;
	MTP::send(request, rpcDone(configLoaded), rpcFail(configFailed));
}

MTProtoConfigLoader *mtpConfigLoader() {
	return &configLoader;
}

void mtpWriteConfig(QDataStream &stream) {
	if (userId) {
		stream << quint32(dbiUser) << qint32(userId) << quint32(mainDC);
	}
	if (configLoadedOnce) {
		for (mtpDcOptions::const_iterator i = gDCOptions.cbegin(), e = gDCOptions.cend(); i != e; ++i) {
			stream << quint32(dbiDcOption) << i->id << QString(i->host.c_str()) << QString(i->ip.c_str()) << i->port;
		}
		stream << quint32(dbiConfig1) << qint32(cMaxGroupCount());
	}
}

bool mtpReadConfigElem(int32 blockId, QDataStream &stream) {
	switch (blockId) {
	case dbiUser: {
		quint32 dcId;
		qint32 uid;
		stream >> uid >> dcId;
		if (stream.status() == QDataStream::Ok) {
			DEBUG_LOG(("MTP Info: user found, dc %1, uid %2").arg(dcId).arg(uid));

			userId = uid;
			mainDC = dcId;
			return true;
		}
	} break;

	case dbiDcOption: {
		quint32 dcId, port;
		QString host, ip;
		stream >> dcId >> host >> ip >> port;

		if (stream.status() == QDataStream::Ok) {
			gDCOptions.insert(dcId, mtpDcOption(dcId, host.toUtf8().constData(), ip.toUtf8().constData(), port));
			return true;
		}
	} break;

	case dbiConfig1: {
		quint32 maxSize;
		stream >> maxSize;
		if (stream.status() == QDataStream::Ok) {
			cSetMaxGroupCount(maxSize);
			return true;
		}
	} break;
	}

	return false;
}
