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
#include "localstorage.h"

namespace {

	typedef quint64 FileKey;

	static const char tdfMagic[] = { 'T', 'D', 'F', '$' };
	static const int32 tdfMagicLen = sizeof(tdfMagic);

	QString toFilePart(FileKey val) {
		QString result;
		result.reserve(0x10);
		for (int32 i = 0; i < 0x10; ++i) {
			uchar v = (val & 0x0F);
			result.push_back((v < 0x0A) ? ('0' + v) : ('A' + (v - 0x0A)));
			val >>= 4;
		}
		return result;
	}

	FileKey fromFilePart(const QString &val) {
		FileKey result = 0;
		int32 i = val.size();
		if (i != 0x10) return 0;

		while (i > 0) {
			--i;
			result <<= 4;

			uint16 ch = val.at(i).unicode();
			if (ch >= 'A' && ch <= 'F') {
				result |= (ch - 'A') + 0x0A;
			} else if (ch >= '0' && ch <= '9') {
				result |= (ch - '0');
			} else {
				return 0;
			}
		}
		return result;
	}

	QString _basePath;

	bool _started = false;
	_local_inner::Manager *_manager = 0;

	bool _working() {
		return _manager && !_basePath.isEmpty();
	}

	bool keyAlreadyUsed(QString &name) {
		name += '0';
		if (QFileInfo(name).exists()) return true;
		name[name.size() - 1] = '1';
		return QFileInfo(name).exists();
	}

	FileKey genKey() {
		if (!_working()) return 0;

		FileKey result;
		QString path;
		path.reserve(_basePath.size() + 0x11);
		path += _basePath;
		do {
			result = MTP::nonce<FileKey>();
			path.resize(_basePath.size());
			path += toFilePart(result);
		} while (keyAlreadyUsed(path));

		return result;
	}

	void clearKey(const FileKey &key, bool safe = true) {
		if (!_working()) return;

		QString name;
		name.reserve(_basePath.size() + 0x11);
		name += _basePath;
		name += toFilePart(key);
		name += '0';
		QFile::remove(name);
		if (safe) {
			name[name.size() - 1] = '1';
			QFile::remove(name);
		}
	}

	QByteArray _passKeySalt, _passKeyEncrypted;

	mtpAuthKey _oldKey, _passKey, _localKey;
	void createLocalKey(const QByteArray &pass, QByteArray *salt, mtpAuthKey *result) {
		uchar key[LocalEncryptKeySize] = { 0 };
		int32 iterCount = pass.size() ? LocalEncryptIterCount : LocalEncryptNoPwdIterCount; // dont slow down for no password
		QByteArray newSalt;
		if (!salt) {
			newSalt.resize(LocalEncryptSaltSize);
			memset_rand(newSalt.data(), newSalt.size());
			salt = &newSalt;

			cSetLocalSalt(newSalt);
		}

		PKCS5_PBKDF2_HMAC_SHA1(pass.constData(), pass.size(), (uchar*)salt->data(), salt->size(), iterCount, LocalEncryptKeySize, key);

		result->setKey(key);
	}

	struct FileReadDescriptor {
		FileReadDescriptor() : version(0) {
		}
		int32 version;
		QByteArray data;
		QBuffer buffer;
		QDataStream stream;
		~FileReadDescriptor() {
			if (version) {
				stream.setDevice(0);
				if (buffer.isOpen()) buffer.close();
				buffer.setBuffer(0);
			}
		}
	};

	struct EncryptedDescriptor {
		EncryptedDescriptor() {
		}
		EncryptedDescriptor(uint32 size) {
			uint32 fullSize = sizeof(uint32) + size;
			if (fullSize & 0x0F) fullSize += 0x10 - (fullSize & 0x0F);
			data.reserve(fullSize);

			data.resize(sizeof(uint32));
			buffer.setBuffer(&data);
			buffer.open(QIODevice::WriteOnly);
			buffer.seek(sizeof(uint32));
			stream.setDevice(&buffer);
			stream.setVersion(QDataStream::Qt_5_1);
		}
		QByteArray data;
		QBuffer buffer;
		QDataStream stream;
		void finish() {
			if (stream.device()) stream.setDevice(0);
			if (buffer.isOpen()) buffer.close();
			buffer.setBuffer(0);
		}
		~EncryptedDescriptor() {
			finish();
		}
	};

	struct FileWriteDescriptor {
		FileWriteDescriptor(const FileKey &key, bool safe = true) : dataSize(0) {
			init(toFilePart(key), safe);
		}
		FileWriteDescriptor(const QString &name, bool safe = true) : dataSize(0) {
			init(name, safe);
		}
		void init(const QString &name, bool safe) {
			if (!_working()) return;

			// detect order of read attempts and file version
			QString toTry[2];
			toTry[0] = _basePath + name + '0';
			if (safe) {
				toTry[1] = _basePath + name + '1';
				QFileInfo toTry0(toTry[0]);
				QFileInfo toTry1(toTry[1]);
				if (toTry0.exists()) {
					if (toTry1.exists()) {
						QDateTime mod0 = toTry0.lastModified(), mod1 = toTry1.lastModified();
						if (mod0 > mod1) {
							qSwap(toTry[0], toTry[1]);
						}
					} else {
						qSwap(toTry[0], toTry[1]);
					}
					toDelete = toTry[1];
				} else if (toTry1.exists()) {
					toDelete = toTry[1];
				}
			}

			file.setFileName(toTry[0]);
			if (file.open(QIODevice::WriteOnly)) {
				file.write(tdfMagic, tdfMagicLen);
				qint32 version = AppVersion;
				file.write((const char*)&version, sizeof(version));

				stream.setDevice(&file);
				stream.setVersion(QDataStream::Qt_5_1);
			}
		}
		bool writeData(const QByteArray &data) {
			if (!file.isOpen()) return false;

			stream << data;
			quint32 len = data.isNull() ? 0xffffffff : data.size();
			if (QSysInfo::ByteOrder != QSysInfo::BigEndian) {
				len = qbswap(len);
			}
			md5.feed(&len, sizeof(len));
			md5.feed(data.constData(), data.size());
			dataSize += sizeof(len) + data.size();

			return true;
		}
		QByteArray prepareEncrypted(EncryptedDescriptor &data, const mtpAuthKey &key = _localKey) {
			data.finish();
			QByteArray &toEncrypt(data.data);

			// prepare for encryption
			uint32 size = toEncrypt.size(), fullSize = size;
			if (fullSize & 0x0F) {
				fullSize += 0x10 - (fullSize & 0x0F);
				toEncrypt.resize(fullSize);
				memset_rand(toEncrypt.data() + size, fullSize - size);
			}
			*(uint32*)toEncrypt.data() = size;
			QByteArray encrypted(0x10 + fullSize, Qt::Uninitialized); // 128bit of sha1 - key128, sizeof(data), data
			hashSha1(toEncrypt.constData(), toEncrypt.size(), encrypted.data());
			aesEncryptLocal(toEncrypt.constData(), encrypted.data() + 0x10, fullSize, &key, encrypted.constData());

			return encrypted;
		}
		bool writeEncrypted(EncryptedDescriptor &data, const mtpAuthKey &key = _localKey) {
			return writeData(prepareEncrypted(data, key));
		}
		void finish() {
			if (!file.isOpen()) return;

			stream.setDevice(0);

			md5.feed(&dataSize, sizeof(dataSize));
			qint32 version = AppVersion;
			md5.feed(&version, sizeof(version));
			md5.feed(tdfMagic, tdfMagicLen);
			file.write((const char*)md5.result(), 0x10);
			file.close();

			if (!toDelete.isEmpty()) {
				QFile::remove(toDelete);
			}
		}
		QFile file;
		QDataStream stream;

		QString toDelete;

		HashMd5 md5;
		int32 dataSize;

		~FileWriteDescriptor() {
			finish();
		}
	};

	bool readFile(FileReadDescriptor &result, const QString &name, bool safe = true) {
		if (!_working()) return false;
		
		// detect order of read attempts
		QString toTry[2];
		toTry[0] = _basePath + name + '0';
		if (safe) {
			QFileInfo toTry0(toTry[0]);
			if (toTry0.exists()) {
				toTry[1] = _basePath + name + '1';
				QFileInfo toTry1(toTry[1]);
				if (toTry1.exists()) {
					QDateTime mod0 = toTry0.lastModified(), mod1 = toTry1.lastModified();
					if (mod0 < mod1) {
						qSwap(toTry[0], toTry[1]);
					}
				} else {
					toTry[1] = QString();
				}
			} else {
				toTry[0][toTry[0].size() - 1] = '1';
			}
		}
		for (int32 i = 0; i < 2; ++i) {
			QString fname(toTry[i]);
			if (fname.isEmpty()) break;

			QFile f(fname);
			if (!f.open(QIODevice::ReadOnly)) {
				DEBUG_LOG(("App Info: failed to open '%1' for reading").arg(name));
				continue;
			}

			// check magic
			char magic[tdfMagicLen];
			if (f.read(magic, tdfMagicLen) != tdfMagicLen) {
				DEBUG_LOG(("App Info: failed to read magic from '%1'").arg(name));
				continue;
			}
			if (memcmp(magic, tdfMagic, tdfMagicLen)) {
				DEBUG_LOG(("App Info: bad magic %1 in '%2'").arg(mb(magic, tdfMagicLen).str()).arg(name));
				continue;
			}

			// read app version
			qint32 version;
			if (f.read((char*)&version, sizeof(version)) != sizeof(version)) {
				DEBUG_LOG(("App Info: failed to read version from '%1'").arg(name));
				continue;
			}
			if (version > AppVersion) {
				DEBUG_LOG(("App Info: version too big %1 for '%2', my version %3").arg(version).arg(name).arg(AppVersion));
				continue;
			}

			// read data
			QByteArray bytes = f.read(f.size());
			int32 dataSize = bytes.size() - 16;
			if (dataSize < 0) {
				DEBUG_LOG(("App Info: bad file '%1', could not read sign part").arg(name));
				continue;
			}

			// check signature
			HashMd5 md5;
			md5.feed(bytes.constData(), dataSize);
			md5.feed(&dataSize, sizeof(dataSize));
			md5.feed(&version, sizeof(version));
			md5.feed(magic, tdfMagicLen);
			if (memcmp(md5.result(), bytes.constData() + dataSize, 16)) {
				DEBUG_LOG(("App Info: bad file '%1', signature did not match").arg(name));
				continue;
			}

			bytes.resize(dataSize);
			result.data = bytes;
			bytes = QByteArray();

			result.version = version;
			result.buffer.setBuffer(&result.data);
			result.buffer.open(QIODevice::ReadOnly);
			result.stream.setDevice(&result.buffer);
			result.stream.setVersion(QDataStream::Qt_5_1);

			if ((i == 0 && !toTry[1].isEmpty()) || i == 1) {
				QFile::remove(toTry[1 - i]);
			}

			return true;
		}
		return false;
	}

	bool decryptLocal(EncryptedDescriptor &result, const QByteArray &encrypted, const mtpAuthKey &key = _localKey) {
		if (encrypted.size() <= 16 || (encrypted.size() & 0x0F)) {
			LOG(("App Error: bad encrypted part size: %1").arg(encrypted.size()));
			return false;
		}
		uint32 fullLen = encrypted.size() - 16;

		QByteArray decrypted;
		decrypted.resize(fullLen);
		const char *encryptedKey = encrypted.constData(), *encryptedData = encrypted.constData() + 16;
		aesDecryptLocal(encryptedData, decrypted.data(), fullLen, &key, encryptedKey);
		uchar sha1Buffer[20];
		if (memcmp(hashSha1(decrypted.constData(), decrypted.size(), sha1Buffer), encryptedKey, 16)) {
			LOG(("App Error: bad decrypt key, data not decrypted"));
			return false;
		}

		uint32 dataLen = *(const uint32*)decrypted.constData();
		if (dataLen > uint32(decrypted.size()) || dataLen <= fullLen - 16 || dataLen < sizeof(uint32)) {
			LOG(("App Error: bad decrypted part size: %1, fullLen: %2, decrypted size: %3").arg(dataLen).arg(fullLen).arg(decrypted.size()));
			return false;
		}

		decrypted.resize(dataLen);
		result.data = decrypted;
		decrypted = QByteArray();

		result.buffer.setBuffer(&result.data);
		result.buffer.open(QIODevice::ReadOnly);
		result.buffer.seek(sizeof(uint32)); // skip len
		result.stream.setDevice(&result.buffer);
		result.stream.setVersion(QDataStream::Qt_5_1);

		return true;
	}
	
	bool readEncryptedFile(FileReadDescriptor &result, const QString &name, bool safe = true) {
		if (!readFile(result, name, safe)) {
			return false;
		}
		QByteArray encrypted;
		result.stream >> encrypted;

		EncryptedDescriptor data;
		if (!decryptLocal(data, encrypted)) {
			result.stream.setDevice(0);
			if (result.buffer.isOpen()) result.buffer.close();
			result.buffer.setBuffer(0);
			result.data = QByteArray();
			result.version = 0;
			return false;
		}

		result.stream.setDevice(0);
		if (result.buffer.isOpen()) result.buffer.close();
		result.buffer.setBuffer(0);
		result.data = data.data;
		result.buffer.setBuffer(&result.data);
		result.buffer.open(QIODevice::ReadOnly);
		result.buffer.seek(data.buffer.pos());
		result.stream.setDevice(&result.buffer);
		result.stream.setVersion(QDataStream::Qt_5_1);

		return true;
	}

	enum { // Local Storage Keys
		lskUserMap = 0,
		lskDraft, // data: PeerId peer
		lskDraftPosition, // data: PeerId peer
		lskStorage, // data: StorageKey location
	};

	typedef QMap<PeerId, FileKey> DraftsMap;
	DraftsMap _draftsMap, _draftsPositionsMap;
	typedef QMap<PeerId, bool> DraftsNotReadMap;
	DraftsNotReadMap _draftsNotReadMap;

	typedef QPair<FileKey, qint32> FileDesc; // file, size
	typedef QMap<StorageKey, FileDesc> StorageMap;
	StorageMap _storageMap;
	int32 _storageFilesSize = 0;

	bool _mapChanged = false;

	Local::ReadMapState _readMap(const QByteArray &pass) {
		uint64 ms = getms();
		QByteArray dataNameUtf8 = cDataFile().toUtf8();
		uint64 dataNameHash[2];
		hashMd5(dataNameUtf8.constData(), dataNameUtf8.size(), dataNameHash);
		_basePath = cWorkingDir() + qsl("tdata/") + toFilePart(dataNameHash[0]) + QChar('/');

		FileReadDescriptor mapData;
		if (!readFile(mapData, qsl("map"))) {
			return Local::ReadMapFailed;
		}

		QByteArray salt, keyEncrypted, mapEncrypted;
		mapData.stream >> salt >> keyEncrypted >> mapEncrypted;
		if (mapData.stream.status() != QDataStream::Ok) {
			LOG(("App Error: could not read salt / key from map file - corrupted?..").arg(mapData.stream.status()));
			return Local::ReadMapFailed;
		}
		if (salt.size() != LocalEncryptSaltSize) {
			LOG(("App Error: bad salt in map file, size: %1").arg(salt.size()));
			return Local::ReadMapFailed;
		}
		createLocalKey(pass, &salt, &_passKey);

		EncryptedDescriptor keyData, map;
		if (!decryptLocal(keyData, keyEncrypted, _passKey)) {
			LOG(("App Error: could not decrypt pass-protected key from map file, maybe bad password.."));
			return Local::ReadMapPassNeeded;
		}
		uchar key[LocalEncryptKeySize] = { 0 };
		if (keyData.stream.readRawData((char*)key, LocalEncryptKeySize) != LocalEncryptKeySize || !keyData.stream.atEnd()) {
			LOG(("App Error: could not read pass-protected key from map file"));
			return Local::ReadMapFailed;
		}
		_localKey.setKey(key);

		_passKeyEncrypted = keyEncrypted;
		_passKeySalt = salt;

		if (!decryptLocal(map, mapEncrypted)) {
			LOG(("App Error: could not decrypt map."));
			return Local::ReadMapFailed;
		}

		DraftsMap draftsMap, draftsPositionsMap;
		DraftsNotReadMap draftsNotReadMap;
		StorageMap storageMap;
		qint64 storageFilesSize = 0;
		while (!map.stream.atEnd()) {
			quint32 keyType;
			map.stream >> keyType;
			switch (keyType) {
			case lskDraft: {
				quint32 count = 0;
				map.stream >> count;
				for (quint32 i = 0; i < count; ++i) {
					FileKey key;
					quint64 p;
					map.stream >> key >> p;
					draftsMap.insert(p, key);
					draftsNotReadMap.insert(p, true);
				}
			} break;
			case lskDraftPosition: {
				quint32 count = 0;
				map.stream >> count;
				for (quint32 i = 0; i < count; ++i) {
					FileKey key;
					quint64 p;
					map.stream >> key >> p;
					draftsPositionsMap.insert(p, key);
				}
			} break;
			case lskStorage: {
				quint32 count = 0;
				map.stream >> count;
				for (quint32 i = 0; i < count; ++i) {
					FileKey key;
					quint64 first, second;
					qint32 size;
					map.stream >> key >> first >> second >> size;
					storageMap.insert(StorageKey(first, second), FileDesc(key, size));
					storageFilesSize += size;
				}
			} break;
			default:
				LOG(("App Error: unknown key type in encrypted map: %1").arg(keyType));
				return Local::ReadMapFailed;
			}
			if (map.stream.status() != QDataStream::Ok) {
				LOG(("App Error: reading encrypted map bad status: %1").arg(map.stream.status()));
				return Local::ReadMapFailed;
			}
		}
		_draftsMap = draftsMap;
		_draftsPositionsMap = draftsPositionsMap;
		_draftsNotReadMap = draftsNotReadMap;
		_storageMap = storageMap;
		_storageFilesSize = storageFilesSize;
		_mapChanged = false;
		LOG(("Map read time: %1").arg(getms() - ms));
		return Local::ReadMapDone;
	}

	enum WriteMapWhen {
		WriteMapNow,
		WriteMapFast,
		WriteMapSoon,
	};
	void _writeMap(WriteMapWhen when = WriteMapSoon) {
		if (when != WriteMapNow) {
			_manager->writeMap(when == WriteMapFast);
			return;
		}
		_manager->writingMap();
		if (!_mapChanged) return;
		if (_basePath.isEmpty()) {
			LOG(("App Error: _basePath is empty in writeMap()"));
			return;
		}

		QDir().mkpath(_basePath);

		FileWriteDescriptor map(qsl("map"));
		if (_passKeySalt.isEmpty() || _passKeyEncrypted.isEmpty()) {
			uchar local5Key[LocalEncryptKeySize] = { 0 };
			QByteArray pass(LocalEncryptKeySize, Qt::Uninitialized), salt(LocalEncryptSaltSize, Qt::Uninitialized);
			memset_rand(pass.data(), pass.size());
			memset_rand(salt.data(), salt.size());
			createLocalKey(pass, &salt, &_localKey);

			_passKeySalt.resize(LocalEncryptSaltSize);
			memset_rand(_passKeySalt.data(), _passKeySalt.size());
			createLocalKey(QByteArray(), &_passKeySalt, &_passKey);

			EncryptedDescriptor passKeyData(LocalEncryptKeySize);
			_localKey.write(passKeyData.stream);
			_passKeyEncrypted = map.prepareEncrypted(passKeyData, _passKey);
		}
		map.writeData(_passKeySalt);
		map.writeData(_passKeyEncrypted);

		uint32 mapSize = 0;
		if (!_draftsMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _draftsMap.size() * sizeof(quint64) * 2;
		if (!_draftsPositionsMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _draftsPositionsMap.size() * sizeof(quint64) * 2;
		if (!_storageMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _storageMap.size() * (sizeof(quint64) * 3 + sizeof(qint32));
		EncryptedDescriptor mapData(mapSize);
		if (!_draftsMap.isEmpty()) {
			mapData.stream << quint32(lskDraft) << quint32(_draftsMap.size());
			for (DraftsMap::const_iterator i = _draftsMap.cbegin(), e = _draftsMap.cend(); i != e; ++i) {
				mapData.stream << quint64(i.value()) << quint64(i.key());
			}
		}
		if (!_draftsPositionsMap.isEmpty()) {
			mapData.stream << quint32(lskDraftPosition) << quint32(_draftsPositionsMap.size());
			for (DraftsMap::const_iterator i = _draftsPositionsMap.cbegin(), e = _draftsPositionsMap.cend(); i != e; ++i) {
				mapData.stream << quint64(i.value()) << quint64(i.key());
			}
		}
		if (!_storageMap.isEmpty()) {
			mapData.stream << quint32(lskStorage) << quint32(_storageMap.size());
			for (StorageMap::const_iterator i = _storageMap.cbegin(), e = _storageMap.cend(); i != e; ++i) {
				mapData.stream << quint64(i.value().first) << quint64(i.key().first) << quint64(i.key().second) << qint32(i.value().second);
			}
		}
		map.writeEncrypted(mapData);

		map.finish();

		_mapChanged = false;
	}

}

namespace _local_inner {

	Manager::Manager() {
		_mapWriteTimer.setSingleShot(true);
		connect(&_mapWriteTimer, SIGNAL(timeout()), this, SLOT(mapWriteTimeout()));
	}

	void Manager::writeMap(bool fast) {
		if (!_mapWriteTimer.isActive() || fast) {
			_mapWriteTimer.start(fast ? 1 : WriteMapTimeout);
		} else if (_mapWriteTimer.remainingTime() <= 0) {
			mapWriteTimeout();
		}
	}

	void Manager::writingMap() {
		_mapWriteTimer.stop();
	}

	void Manager::mapWriteTimeout() {
		_writeMap(WriteMapNow);
	}

	void Manager::finish() {
		if (_mapWriteTimer.isActive()) {
			mapWriteTimeout();
			_mapWriteTimer.stop();
		}
	}

}

namespace Local {

	mtpAuthKey &oldKey() {
		return _oldKey;
	}

	void createOldKey(QByteArray *salt) {
		createLocalKey(QByteArray(), salt, &_oldKey);
	}

	void start() {
		if (!_started) {
			_started = true;
			_manager = new _local_inner::Manager();
		}
	}

	void stop() {
		if (_manager) {
			_writeMap(WriteMapNow);
			_manager->finish();
			_manager->deleteLater();
			_manager = 0;
		}
	}

	ReadMapState readMap(const QByteArray &pass) {
		ReadMapState result = _readMap(pass);
		if (result == ReadMapFailed) {
			_mapChanged = true;
			_writeMap(WriteMapNow);
		}
		return result;
	}

	void writeDraft(const PeerId &peer, const QString &text) {
		if (!_working()) return;

		if (text.isEmpty()) {
			DraftsMap::iterator i = _draftsMap.find(peer);
			if (i != _draftsMap.cend()) {
				clearKey(i.value());
				_draftsMap.erase(i);
				_mapChanged = true;
				_writeMap();
			}

			_draftsNotReadMap.remove(peer);
		} else {
			DraftsMap::const_iterator i = _draftsMap.constFind(peer);
			if (i == _draftsMap.cend()) {
				i = _draftsMap.insert(peer, genKey());
				_mapChanged = true;
				_writeMap(WriteMapFast);
			}
			QString to = _basePath + toFilePart(i.value());
			EncryptedDescriptor data(sizeof(quint64) + sizeof(quint32) + text.size() * sizeof(QChar));
			data.stream << quint64(peer) << text;
			FileWriteDescriptor file(i.value());
			file.writeEncrypted(data);

			_draftsNotReadMap.remove(peer);
		}
	}

	QString readDraft(const PeerId &peer) {
		if (!_draftsNotReadMap.remove(peer)) return QString();

		DraftsMap::iterator j = _draftsMap.find(peer);
		if (j == _draftsMap.cend()) {
			return QString();
		}
		FileReadDescriptor draft;
		if (!readEncryptedFile(draft, toFilePart(j.value()))) {
			clearKey(j.value());
			_draftsMap.erase(j);
			return QString();
		}

		quint64 draftPeer;
		QString draftText;
		draft.stream >> draftPeer >> draftText;
		return (draftPeer == peer) ? draftText : QString();
	}

	void writeDraftPositions(const PeerId &peer, const MessageCursor &cur) {
		if (!_working()) return;

		if (cur.position == 0 && cur.anchor == 0 && cur.scroll == 0) {
			DraftsMap::iterator i = _draftsPositionsMap.find(peer);
			if (i != _draftsPositionsMap.cend()) {
				clearKey(i.value());
				_draftsPositionsMap.erase(i);
				_mapChanged = true;
				_writeMap();
			}
		} else {
			DraftsMap::const_iterator i = _draftsPositionsMap.constFind(peer);
			if (i == _draftsPositionsMap.cend()) {
				i = _draftsPositionsMap.insert(peer, genKey());
				_mapChanged = true;
				_writeMap(WriteMapFast);
			}
			QString to = _basePath + toFilePart(i.value());
			EncryptedDescriptor data(sizeof(quint64) + sizeof(qint32) * 3);
			data.stream << quint64(peer) << qint32(cur.position) << qint32(cur.anchor) << qint32(cur.scroll);
			FileWriteDescriptor file(i.value());
			file.writeEncrypted(data);
		}
	}

	MessageCursor readDraftPositions(const PeerId &peer) {
		DraftsMap::iterator j = _draftsPositionsMap.find(peer);
		if (j == _draftsPositionsMap.cend()) {
			return MessageCursor();
		}
		FileReadDescriptor draft;
		if (!readEncryptedFile(draft, toFilePart(j.value()))) {
			clearKey(j.value());
			_draftsPositionsMap.erase(j);
			return MessageCursor();
		}

		quint64 draftPeer;
		qint32 curPosition, curAnchor, curScroll;
		draft.stream >> draftPeer >> curPosition >> curAnchor >> curScroll;

		return (draftPeer == peer) ? MessageCursor(curPosition, curAnchor, curScroll) : MessageCursor();
	}

	bool hasDraftPositions(const PeerId &peer) {
		return (_draftsPositionsMap.constFind(peer) != _draftsPositionsMap.cend());
	}

	qint32 _storageImageSize(qint32 rawlen) {
		// fulllen + storagekey + type + len + data
		qint32 result = sizeof(uint32) + sizeof(quint64) * 2 + sizeof(quint32) + sizeof(quint32) + rawlen;
		if (result & 0x0F) result += 0x10 - (result & 0x0F);
		result += tdfMagicLen + sizeof(qint32) + sizeof(quint32) + 0x10 + 0x10; // magic + version + len of encrypted + part of sha1 + md5
		return result;
	}

	void writeImage(const StorageKey &location, const ImagePtr &image) {
		if (image->isNull() || !image->loaded()) return;
		if (_storageMap.constFind(location) != _storageMap.cend()) return;

		QByteArray fmt = image->savedFormat();
		mtpTypeId format = 0;
		if (fmt == "JPG") {
			format = mtpc_storage_fileJpeg;
		} else if (fmt == "PNG") {
			format = mtpc_storage_filePng;
		} else if (fmt == "GIF") {
			format = mtpc_storage_fileGif;
		}
		if (format) {
			image->forget();
			writeImage(location, StorageImageSaved(format, image->savedData()), false);
		}
	}

	void writeImage(const StorageKey &location, const StorageImageSaved &image, bool overwrite) {
		if (!_working()) return;

		qint32 size = _storageImageSize(image.data.size());
		StorageMap::const_iterator i = _storageMap.constFind(location);
		if (i == _storageMap.cend()) {
			i = _storageMap.insert(location, FileDesc(genKey(), size));
			_storageFilesSize += size;
			_mapChanged = true;
			_writeMap();
		} else if (!overwrite) {
			return;
		}
		EncryptedDescriptor data(sizeof(quint64) * 2 + sizeof(quint32) + sizeof(quint32) + image.data.size());
		data.stream << quint64(location.first) << quint64(location.second) << quint32(image.type) << image.data;
		FileWriteDescriptor file(i.value().first, false);
		file.writeEncrypted(data);
		if (i.value().second != size) {
			_storageFilesSize += size;
			_storageFilesSize -= i.value().second;
			_storageMap[location].second = size;
		}
	}

	StorageImageSaved readImage(const StorageKey &location) {
		StorageMap::iterator j = _storageMap.find(location);
		if (j == _storageMap.cend()) {
			return StorageImageSaved();
		}
		FileReadDescriptor draft;
		if (!readEncryptedFile(draft, toFilePart(j.value().first), false)) {
			clearKey(j.value().first, false);
			_storageFilesSize -= j.value().second;
			_storageMap.erase(j);
			return StorageImageSaved();
		}

		QByteArray imageData;
		quint64 locFirst, locSecond;
		quint32 imageType;
		draft.stream >> locFirst >> locSecond >> imageType >> imageData;

		return (locFirst == location.first && locSecond == location.second) ? StorageImageSaved(imageType, imageData) : StorageImageSaved();
	}

	int32 hasImages() {
		return _storageMap.size();
	}

	qint64 storageFilesSize() {
		return _storageFilesSize;
	}

	struct ClearManagerData {
		QThread *thread;
		StorageMap images;
		QMutex mutex;
		QList<int> tasks;
		bool working;
	};

	ClearManager::ClearManager() : data(new ClearManagerData()) {
		data->thread = new QThread();
		data->working = true;
	}

	bool ClearManager::addTask(int task) {
		QMutexLocker lock(&data->mutex);
		if (!data->working) return false;

		if (!data->tasks.isEmpty() && (data->tasks.at(0) == ClearManagerAll)) return true;
		if (task == ClearManagerAll) {
			data->tasks.clear();
		} else {
			if (task & ClearManagerImages) {
				if (data->images.isEmpty()) {
					data->images = _storageMap;
				} else {
					for (StorageMap::const_iterator i = _storageMap.cbegin(), e = _storageMap.cend(); i != e; ++i) {
						StorageKey k = i.key();
						while (data->images.constFind(k) != data->images.cend()) {
							++k.second;
						}
						data->images.insert(k, i.value());
					}
				}
				_storageMap.clear();
				_storageFilesSize = 0;
				_mapChanged = true;
				_writeMap();
			}
			for (int32 i = 0, l = data->tasks.size(); i < l; ++i) {
				if (data->tasks.at(i) == task) return true;
			}
		}
		data->tasks.push_back(task);
		return true;
	}

	bool ClearManager::hasTask(ClearManagerTask task) {
		QMutexLocker lock(&data->mutex);
		if (data->tasks.isEmpty()) return false;
		if (data->tasks.at(0) == ClearManagerAll) return true;
		for (int32 i = 0, l = data->tasks.size(); i < l; ++i) {
			if (data->tasks.at(i) == task) return true;
		}
		return false;
	}

	void ClearManager::start() {
		moveToThread(data->thread);
		connect(data->thread, SIGNAL(started()), this, SLOT(onStart()));
		data->thread->start();
	}

	ClearManager::~ClearManager() {
		data->thread->deleteLater();
		delete data;
	}

	void ClearManager::onStart() {
		while (true) {
			int task = 0;
			bool result = false;
			StorageMap images;
			{
				QMutexLocker lock(&data->mutex);
				if (data->tasks.isEmpty()) {
					data->working = false;
					break;
				}
				task = data->tasks.at(0);
				images = data->images;
			}
			switch (task) {
			case ClearManagerAll:
				result = (QDir(cTempDir()).removeRecursively() && QDir(_basePath).removeRecursively());
			break;
			case ClearManagerDownloads:
				result = QDir(cTempDir()).removeRecursively();
			break;
			case ClearManagerImages:
				for (StorageMap::const_iterator i = images.cbegin(), e = images.cend(); i != e; ++i) {
					clearKey(i.value().first, false);
				}
				result = true;
			break;
			}
			{
				QMutexLocker lock(&data->mutex);
				if (data->tasks.at(0) == task) {
					data->tasks.pop_front();
					if (data->tasks.isEmpty()) {
						data->working = false;
					}
				}
				if (result) {
					emit succeed(task, data->working ? 0 : this);
				} else {
					emit failed(task, data->working ? 0 : this);
				}
				if (!data->working) break;
			}
		}
	}

}
