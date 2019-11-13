/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/auth_key.h"

extern "C" {
#include <openssl/aes.h>
#include <openssl/modes.h>
} // extern "C"

namespace MTP {

AuthKey::AuthKey(Type type, DcId dcId, const Data &data)
: _type(type)
, _dcId(dcId)
, _key(data) {
	countKeyId();
}

AuthKey::AuthKey(const Data &data) : _type(Type::Local), _key(data) {
	countKeyId();
}

AuthKey::Type AuthKey::type() const {
	return _type;
}

int AuthKey::dcId() const {
	return _dcId;
}

AuthKey::KeyId AuthKey::keyId() const {
	return _keyId;
}

void AuthKey::prepareAES_oldmtp(const MTPint128 &msgKey, MTPint256 &aesKey, MTPint256 &aesIV, bool send) const {
	uint32 x = send ? 0 : 8;

	uchar data_a[16 + 32], sha1_a[20];
	memcpy(data_a, &msgKey, 16);
	memcpy(data_a + 16, _key.data() + x, 32);
	hashSha1(data_a, 16 + 32, sha1_a);

	uchar data_b[16 + 16 + 16], sha1_b[20];
	memcpy(data_b, _key.data() + 32 + x, 16);
	memcpy(data_b + 16, &msgKey, 16);
	memcpy(data_b + 32, _key.data() + 48 + x, 16);
	hashSha1(data_b, 16 + 16 + 16, sha1_b);

	uchar data_c[32 + 16], sha1_c[20];
	memcpy(data_c, _key.data() + 64 + x, 32);
	memcpy(data_c + 32, &msgKey, 16);
	hashSha1(data_c, 32 + 16, sha1_c);

	uchar data_d[16 + 32], sha1_d[20];
	memcpy(data_d, &msgKey, 16);
	memcpy(data_d + 16, _key.data() + 96 + x, 32);
	hashSha1(data_d, 16 + 32, sha1_d);

	auto key = reinterpret_cast<uchar*>(&aesKey);
	auto iv = reinterpret_cast<uchar*>(&aesIV);
	memcpy(key, sha1_a, 8);
	memcpy(key + 8, sha1_b + 8, 12);
	memcpy(key + 8 + 12, sha1_c + 4, 12);
	memcpy(iv, sha1_a + 8, 12);
	memcpy(iv + 12, sha1_b, 8);
	memcpy(iv + 12 + 8, sha1_c + 16, 4);
	memcpy(iv + 12 + 8 + 4, sha1_d, 8);
}

void AuthKey::prepareAES(const MTPint128 &msgKey, MTPint256 &aesKey, MTPint256 &aesIV, bool send) const {
	uint32 x = send ? 0 : 8;

	uchar data_a[16 + 36], sha256_a[32];
	memcpy(data_a, &msgKey, 16);
	memcpy(data_a + 16, _key.data() + x, 36);
	hashSha256(data_a, 16 + 36, sha256_a);

	uchar data_b[36 + 16], sha256_b[32];
	memcpy(data_b, _key.data() + 40 + x, 36);
	memcpy(data_b + 36, &msgKey, 16);
	hashSha256(data_b, 36 + 16, sha256_b);

	auto key = reinterpret_cast<uchar*>(&aesKey);
	auto iv = reinterpret_cast<uchar*>(&aesIV);
	memcpy(key, sha256_a, 8);
	memcpy(key + 8, sha256_b + 8, 16);
	memcpy(key + 8 + 16, sha256_a + 24, 8);
	memcpy(iv, sha256_b, 8);
	memcpy(iv + 8, sha256_a + 8, 16);
	memcpy(iv + 8 + 16, sha256_b + 24, 8);
}

const void *AuthKey::partForMsgKey(bool send) const {
	return _key.data() + 88 + (send ? 0 : 8);
}

void AuthKey::write(QDataStream &to) const {
	to.writeRawData(reinterpret_cast<const char*>(_key.data()), _key.size());
}

bytes::const_span AuthKey::data() const {
	return _key;
}

bool AuthKey::equals(const std::shared_ptr<AuthKey> &other) const {
	return other ? (_key == other->_key) : false;
}

void AuthKey::FillData(Data &authKey, bytes::const_span computedAuthKey) {
	auto computedAuthKeySize = computedAuthKey.size();
	Assert(computedAuthKeySize <= kSize);
	auto authKeyBytes = gsl::make_span(authKey);
	if (computedAuthKeySize < kSize) {
		bytes::set_with_const(authKeyBytes.subspan(0, kSize - computedAuthKeySize), gsl::byte());
		bytes::copy(authKeyBytes.subspan(kSize - computedAuthKeySize), computedAuthKey);
	} else {
		bytes::copy(authKeyBytes, computedAuthKey);
	}
}

void AuthKey::countKeyId() {
	auto sha1 = hashSha1(_key.data(), _key.size());

	// Lower 64 bits = 8 bytes of 20 byte SHA1 hash.
	_keyId = *reinterpret_cast<KeyId*>(sha1.data() + 12);
}

void aesIgeEncryptRaw(const void *src, void *dst, uint32 len, const void *key, const void *iv) {
	uchar aes_key[32], aes_iv[32];
	memcpy(aes_key, key, 32);
	memcpy(aes_iv, iv, 32);

	AES_KEY aes;
	AES_set_encrypt_key(aes_key, 256, &aes);
	AES_ige_encrypt(static_cast<const uchar*>(src), static_cast<uchar*>(dst), len, &aes, aes_iv, AES_ENCRYPT);
}

void aesIgeDecryptRaw(const void *src, void *dst, uint32 len, const void *key, const void *iv) {
	uchar aes_key[32], aes_iv[32];
	memcpy(aes_key, key, 32);
	memcpy(aes_iv, iv, 32);

	AES_KEY aes;
	AES_set_decrypt_key(aes_key, 256, &aes);
	AES_ige_encrypt(static_cast<const uchar*>(src), static_cast<uchar*>(dst), len, &aes, aes_iv, AES_DECRYPT);
}

void aesCtrEncrypt(bytes::span data, const void *key, CTRState *state) {
	AES_KEY aes;
	AES_set_encrypt_key(static_cast<const uchar*>(key), 256, &aes);

	static_assert(CTRState::IvecSize == AES_BLOCK_SIZE, "Wrong size of ctr ivec!");
	static_assert(CTRState::EcountSize == AES_BLOCK_SIZE, "Wrong size of ctr ecount!");

	CRYPTO_ctr128_encrypt(
		reinterpret_cast<const uchar*>(data.data()),
		reinterpret_cast<uchar*>(data.data()),
		data.size(),
		&aes,
		state->ivec,
		state->ecount,
		&state->num,
		(block128_f)AES_encrypt);
}

} // namespace MTP
