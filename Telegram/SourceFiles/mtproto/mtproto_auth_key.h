/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"
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
		Temporary,
		ReadFromFile,
		Local,
	};
	AuthKey(Type type, DcId dcId, const Data &data);
	explicit AuthKey(const Data &data);

	AuthKey(const AuthKey &other) = delete;
	AuthKey &operator=(const AuthKey &other) = delete;

	[[nodiscard]] Type type() const;
	[[nodiscard]] int dcId() const;
	[[nodiscard]] KeyId keyId() const;

	void prepareAES_oldmtp(const MTPint128 &msgKey, MTPint256 &aesKey, MTPint256 &aesIV, bool send) const;
	void prepareAES(const MTPint128 &msgKey, MTPint256 &aesKey, MTPint256 &aesIV, bool send) const;

	[[nodiscard]] const void *partForMsgKey(bool send) const;

	void write(QDataStream &to) const;
	[[nodiscard]] bytes::const_span data() const;
	[[nodiscard]] bool equals(const std::shared_ptr<AuthKey> &other) const;

	[[nodiscard]] crl::time creationTime() const; // > 0 if known.
	[[nodiscard]] TimeId expiresAt() const;
	void setExpiresAt(TimeId expiresAt);

	static void FillData(Data &authKey, bytes::const_span computedAuthKey);

private:
	void countKeyId();

	Type _type = Type::Generated;
	DcId _dcId = 0;
	Data _key = { { gsl::byte{} } };
	KeyId _keyId = 0;
	crl::time _creationTime = 0;
	TimeId _expiresAt = 0;

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
void aesCtrEncrypt(bytes::span data, const void *key, CTRState *state);

} // namespace MTP
