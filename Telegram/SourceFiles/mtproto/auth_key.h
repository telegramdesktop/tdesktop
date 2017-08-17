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

#include <array>
#include <memory>

namespace MTP {

class AuthKey {
public:
	static constexpr auto kSize = 256; // 2048 bits.
	using Data = std::array<gsl::byte, kSize>;
	using KeyId = uint64;

	enum class Type {
		Generated,
		ReadFromFile,
		Local,
	};
	AuthKey(Type type, DcId dcId, const Data &data) : _type(type), _dcId(dcId), _key(data) {
		countKeyId();
	}
	AuthKey(const Data &data) : _type(Type::Local), _key(data) {
		countKeyId();
	}

	AuthKey(const AuthKey &other) = delete;
	AuthKey &operator=(const AuthKey &other) = delete;

	Type type() const {
		return _type;
	}

	int dcId() const {
		return _dcId;
	}

	KeyId keyId() const {
		return _keyId;
	}

	void prepareAES_oldmtp(const MTPint128 &msgKey, MTPint256 &aesKey, MTPint256 &aesIV, bool send) const;
	void prepareAES(const MTPint128 &msgKey, MTPint256 &aesKey, MTPint256 &aesIV, bool send) const;

	const void *partForMsgKey(bool send) const {
		return _key.data() + 88 + (send ? 0 : 8);
	}

	void write(QDataStream &to) const {
		to.writeRawData(reinterpret_cast<const char*>(_key.data()), _key.size());
	}

	bool equals(const std::shared_ptr<AuthKey> &other) const {
		return other ? (_key == other->_key) : false;
	}

	static void FillData(Data &authKey, base::const_byte_span computedAuthKey) {
		auto computedAuthKeySize = computedAuthKey.size();
		Assert(computedAuthKeySize <= kSize);
		auto authKeyBytes = gsl::make_span(authKey);
		if (computedAuthKeySize < kSize) {
			base::set_bytes(authKeyBytes.subspan(0, kSize - computedAuthKeySize), gsl::byte());
			base::copy_bytes(authKeyBytes.subspan(kSize - computedAuthKeySize), computedAuthKey);
		} else {
			base::copy_bytes(authKeyBytes, computedAuthKey);
		}
	}

private:
	void countKeyId() {
		auto sha1 = hashSha1(_key.data(), _key.size());

		// Lower 64 bits = 8 bytes of 20 byte SHA1 hash.
		_keyId = *reinterpret_cast<KeyId*>(sha1.data() + 12);
	}

	Type _type = Type::Generated;
	DcId _dcId = 0;
	Data _key = { { gsl::byte{} } };
	KeyId _keyId = 0;

};

using AuthKeyPtr = std::shared_ptr<AuthKey>;
using AuthKeysList = std::vector<AuthKeyPtr>;

void aesIgeEncryptRaw(const void *src, void *dst, uint32 len, const void *key, const void *iv);
void aesIgeDecryptRaw(const void *src, void *dst, uint32 len, const void *key, const void *iv);

inline void aesIgeEncrypt_oldmtp(const void *src, void *dst, uint32 len, const AuthKeyPtr &authKey, const MTPint128 &msgKey) {
	MTPint256 aesKey, aesIV;
	authKey->prepareAES_oldmtp(msgKey, aesKey, aesIV, true);

	return aesIgeEncryptRaw(src, dst, len, static_cast<const void*>(&aesKey), static_cast<const void*>(&aesIV));
}

inline void aesIgeEncrypt(const void *src, void *dst, uint32 len, const AuthKeyPtr &authKey, const MTPint128 &msgKey) {
	MTPint256 aesKey, aesIV;
	authKey->prepareAES(msgKey, aesKey, aesIV, true);

	return aesIgeEncryptRaw(src, dst, len, static_cast<const void*>(&aesKey), static_cast<const void*>(&aesIV));
}

inline void aesEncryptLocal(const void *src, void *dst, uint32 len, const AuthKeyPtr &authKey, const void *key128) {
	MTPint256 aesKey, aesIV;
	authKey->prepareAES_oldmtp(*(const MTPint128*)key128, aesKey, aesIV, false);

	return aesIgeEncryptRaw(src, dst, len, static_cast<const void*>(&aesKey), static_cast<const void*>(&aesIV));
}

inline void aesIgeDecrypt_oldmtp(const void *src, void *dst, uint32 len, const AuthKeyPtr &authKey, const MTPint128 &msgKey) {
	MTPint256 aesKey, aesIV;
	authKey->prepareAES_oldmtp(msgKey, aesKey, aesIV, false);

	return aesIgeDecryptRaw(src, dst, len, static_cast<const void*>(&aesKey), static_cast<const void*>(&aesIV));
}

inline void aesIgeDecrypt(const void *src, void *dst, uint32 len, const AuthKeyPtr &authKey, const MTPint128 &msgKey) {
	MTPint256 aesKey, aesIV;
	authKey->prepareAES(msgKey, aesKey, aesIV, false);

	return aesIgeDecryptRaw(src, dst, len, static_cast<const void*>(&aesKey), static_cast<const void*>(&aesIV));
}

inline void aesDecryptLocal(const void *src, void *dst, uint32 len, const AuthKeyPtr &authKey, const void *key128) {
	MTPint256 aesKey, aesIV;
	authKey->prepareAES_oldmtp(*(const MTPint128*)key128, aesKey, aesIV, false);

	return aesIgeDecryptRaw(src, dst, len, static_cast<const void*>(&aesKey), static_cast<const void*>(&aesIV));
}

// ctr used inplace, encrypt the data and leave it at the same place
struct CTRState {
	static constexpr int KeySize = 32;
	static constexpr int IvecSize = 16;
	static constexpr int EcountSize = 16;

	uchar ivec[IvecSize] = { 0 };
	uint32 num = 0;
	uchar ecount[EcountSize] = { 0 };
};
void aesCtrEncrypt(void *data, uint32 len, const void *key, CTRState *state);

} // namespace MTP
