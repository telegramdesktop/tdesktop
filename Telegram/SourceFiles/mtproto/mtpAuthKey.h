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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#pragma once

class mtpAuthKey {
public:

	mtpAuthKey() : _isset(false), _dc(0) {
	}

	bool created() const {
		return _isset;
	}

	void setKey(const void *from) {
		memcpy(_key, from, 256);
		uchar sha1Buffer[20];
		_keyId = *(uint64*)(hashSha1(_key, 256, sha1Buffer) + 3);
		_isset = true;
	}

	void setDC(uint32 dc) {
		_dc = dc;
	}

	uint32 getDC() const {
		if (!_isset) throw mtpErrorKeyNotReady("getDC()");
		return _dc;
	}

	uint64 keyId() const {
		if (!_isset) throw mtpErrorKeyNotReady("keyId()");
		return _keyId;
	}

	void prepareAES(const MTPint128 &msgKey, MTPint256 &aesKey, MTPint256 &aesIV, bool send = true) const {
		if (!_isset) throw mtpErrorKeyNotReady(QString("prepareAES(.., %1)").arg(logBool(send)));

		uint32 x = send ? 0 : 8;

		uchar data_a[16 + 32], sha1_a[20];
		memcpy(data_a, &msgKey, 16);
		memcpy(data_a + 16, _key + x, 32);
		hashSha1(data_a, 16 + 32, sha1_a);

		uchar data_b[16 + 16 + 16], sha1_b[20];
		memcpy(data_b, _key + 32 + x, 16);
		memcpy(data_b + 16, &msgKey, 16);
		memcpy(data_b + 32, _key + 48 + x, 16);
		hashSha1(data_b, 16 + 16 + 16, sha1_b);

		uchar data_c[32 + 16], sha1_c[20];
		memcpy(data_c, _key + 64 + x, 32);
		memcpy(data_c + 32, &msgKey, 16);
		hashSha1(data_c, 32 + 16, sha1_c);

		uchar data_d[16 + 32], sha1_d[20];
		memcpy(data_d, &msgKey, 16);
		memcpy(data_d + 16, _key + 96 + x, 32);
		hashSha1(data_d, 16 + 32, sha1_d);

		uchar *key((uchar*)&aesKey), *iv((uchar*)&aesIV);
		memcpy(key, sha1_a, 8);
		memcpy(key + 8, sha1_b + 8, 12);
		memcpy(key + 8 + 12, sha1_c + 4, 12);
		memcpy(iv, sha1_a + 8, 12);
		memcpy(iv + 12, sha1_b, 8);
		memcpy(iv + 12 + 8, sha1_c + 16, 4);
		memcpy(iv + 12 + 8 + 4, sha1_d, 8);
	}

	void write(QDataStream &to) const {
		if (!_isset) throw mtpErrorKeyNotReady("write(..)");
		to.writeRawData(_key, 256);
	}

	static const uint64 RecreateKeyId = 0xFFFFFFFFFFFFFFFFL;

	friend bool operator==(const mtpAuthKey &a, const mtpAuthKey &b);

private:

	char _key[256];
	uint64 _keyId;
	bool _isset;
	uint32 _dc;

};

inline bool operator==(const mtpAuthKey &a, const mtpAuthKey &b) {
	return !memcmp(a._key, b._key, 256);
}

typedef QSharedPointer<mtpAuthKey> mtpAuthKeyPtr;
typedef QVector<mtpAuthKeyPtr> mtpKeysMap;

void aesEncrypt(const void *src, void *dst, uint32 len, void *key, void *iv);
void aesDecrypt(const void *src, void *dst, uint32 len, void *key, void *iv);

inline void aesEncrypt(const void *src, void *dst, uint32 len, const mtpAuthKeyPtr &authKey, const MTPint128 &msgKey) {
	MTPint256 aesKey, aesIV;
	authKey->prepareAES(msgKey, aesKey, aesIV);

	return aesEncrypt(src, dst, len, &aesKey, &aesIV);
}

inline void aesEncryptLocal(const void *src, void *dst, uint32 len, const mtpAuthKey *authKey, const void *key128) {
	MTPint256 aesKey, aesIV;
	authKey->prepareAES(*(const MTPint128*)key128, aesKey, aesIV, false);

	return aesEncrypt(src, dst, len, &aesKey, &aesIV);
}

inline void aesDecrypt(const void *src, void *dst, uint32 len, const mtpAuthKeyPtr &authKey, const MTPint128 &msgKey) {
	MTPint256 aesKey, aesIV;
	authKey->prepareAES(msgKey, aesKey, aesIV, false);

	return aesDecrypt(src, dst, len, &aesKey, &aesIV);
}

inline void aesDecryptLocal(const void *src, void *dst, uint32 len, const mtpAuthKey *authKey, const void *key128) {
	MTPint256 aesKey, aesIV;
	authKey->prepareAES(*(const MTPint128*)key128, aesKey, aesIV, false);

	return aesDecrypt(src, dst, len, &aesKey, &aesIV);
}
