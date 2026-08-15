/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#define OPENSSL_SUPPRESS_DEPRECATED

#include "webauthn/cable_core.h"

#include <openssl/aes.h>
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Platform::WebAuthn::Cable {
namespace {

constexpr auto kMaxSequence = uint32_t((1 << 24) - 1);
constexpr auto kPaddingGranularity = size_t(32);
constexpr auto kMaxCborDepth = 16;

constexpr auto kAssignedTunnelDomains = std::array{
	"cable.ua5v.com",
	"cable.auth.com",
};

void AppendUnsigned(Bytes &out, uint8_t major, uint64_t value) {
	const auto type = uint8_t(major << 5);
	if (value < 24) {
		out.push_back(type | uint8_t(value));
	} else if (value <= 0xFF) {
		out.push_back(type | 24);
		out.push_back(uint8_t(value));
	} else if (value <= 0xFFFF) {
		out.push_back(type | 25);
		out.push_back(uint8_t(value >> 8));
		out.push_back(uint8_t(value));
	} else if (value <= 0xFFFFFFFF) {
		out.push_back(type | 26);
		for (auto i = 3; i >= 0; --i) {
			out.push_back(uint8_t(value >> (8 * i)));
		}
	} else {
		out.push_back(type | 27);
		for (auto i = 7; i >= 0; --i) {
			out.push_back(uint8_t(value >> (8 * i)));
		}
	}
}

void EncodeTo(Bytes &out, const CborValue &value) {
	if (value.isInt()) {
		const auto v = value.toInt();
		if (v >= 0) {
			AppendUnsigned(out, 0, uint64_t(v));
		} else {
			AppendUnsigned(out, 1, uint64_t(-1 - v));
		}
	} else if (value.isBytes()) {
		AppendUnsigned(out, 2, value.bytes().size());
		out.insert(out.end(), value.bytes().begin(), value.bytes().end());
	} else if (value.isString()) {
		AppendUnsigned(out, 3, value.string().size());
		out.insert(out.end(), value.string().begin(), value.string().end());
	} else if (value.isArray()) {
		AppendUnsigned(out, 4, value.array().size());
		for (const auto &entry : value.array()) {
			EncodeTo(out, entry);
		}
	} else if (value.isMap()) {
		AppendUnsigned(out, 5, value.map().size());
		for (const auto &[key, entry] : value.map()) {
			EncodeTo(out, key);
			EncodeTo(out, entry);
		}
	} else if (value.isBool()) {
		out.push_back(value.toBool() ? 0xF5 : 0xF4);
	}
}

struct Reader {
	const uint8_t *data = nullptr;
	size_t size = 0;
	size_t position = 0;

	[[nodiscard]] bool read(uint8_t &out) {
		if (position >= size) {
			return false;
		}
		out = data[position++];
		return true;
	}
	[[nodiscard]] bool readBig(size_t count, uint64_t &out) {
		if (size - position < count) {
			return false;
		}
		out = 0;
		for (auto i = size_t(0); i != count; ++i) {
			out = (out << 8) | data[position++];
		}
		return true;
	}
};

std::optional<CborValue> DecodeOne(Reader &reader, int depth) {
	if (depth > kMaxCborDepth) {
		return std::nullopt;
	}
	auto initial = uint8_t(0);
	if (!reader.read(initial)) {
		return std::nullopt;
	}
	const auto major = uint8_t(initial >> 5);
	const auto additional = uint8_t(initial & 0x1F);
	auto argument = uint64_t(0);
	if (additional < 24) {
		argument = additional;
	} else if (additional == 24) {
		if (!reader.readBig(1, argument)) {
			return std::nullopt;
		}
	} else if (additional == 25) {
		if (!reader.readBig(2, argument)) {
			return std::nullopt;
		}
	} else if (additional == 26) {
		if (!reader.readBig(4, argument)) {
			return std::nullopt;
		}
	} else if (additional == 27) {
		if (!reader.readBig(8, argument)) {
			return std::nullopt;
		}
	} else {
		return std::nullopt;
	}
	switch (major) {
	case 0:
		if (argument > uint64_t(INT64_MAX)) {
			return std::nullopt;
		}
		return CborValue(int64_t(argument));
	case 1:
		if (argument > uint64_t(INT64_MAX)) {
			return std::nullopt;
		}
		return CborValue(int64_t(-1) - int64_t(argument));
	case 2:
	case 3: {
		if (argument > reader.size - reader.position) {
			return std::nullopt;
		}
		const auto from = reader.data + reader.position;
		reader.position += size_t(argument);
		if (major == 2) {
			return CborValue(Bytes(from, from + argument));
		}
		return CborValue(std::string(
			reinterpret_cast<const char*>(from),
			size_t(argument)));
	}
	case 4: {
		auto result = CborArray();
		for (auto i = uint64_t(0); i != argument; ++i) {
			auto entry = DecodeOne(reader, depth + 1);
			if (!entry) {
				return std::nullopt;
			}
			result.push_back(std::move(*entry));
		}
		return CborValue(std::move(result));
	}
	case 5: {
		auto result = CborMap();
		for (auto i = uint64_t(0); i != argument; ++i) {
			auto key = DecodeOne(reader, depth + 1);
			if (!key) {
				return std::nullopt;
			}
			auto entry = DecodeOne(reader, depth + 1);
			if (!entry) {
				return std::nullopt;
			}
			result.emplace_back(std::move(*key), std::move(*entry));
		}
		return CborValue(std::move(result));
	}
	case 7:
		if (initial == 0xF4) {
			return CborValue(false);
		} else if (initial == 0xF5) {
			return CborValue(true);
		}
		return std::nullopt;
	}
	return std::nullopt;
}

[[nodiscard]] const CborValue *FindText(
		const CborMap &map,
		const char *key) {
	for (const auto &[k, v] : map) {
		if (k.isString() && k.string() == key) {
			return &v;
		}
	}
	return nullptr;
}

[[nodiscard]] std::optional<CborValue> DecodePaddedCborMap(ByteSpan input) {
	if (input.size() >= 2) {
		const auto padding = size_t(input[input.size() - 2])
			| (size_t(input[input.size() - 1]) << 8);
		if (padding + 2 <= input.size()) {
			auto result = CborDecode(
				input.first(input.size() - padding - 2));
			if (result && result->isMap()) {
				return result;
			}
		}
	}
	if (!input.empty()) {
		const auto padding = size_t(input.back());
		if (padding + 1 <= input.size()) {
			auto result = CborDecode(
				input.first(input.size() - padding - 1));
			if (result && result->isMap()) {
				return result;
			}
		}
	}
	return std::nullopt;
}

} // namespace

const CborValue *CborValue::find(int64_t key) const {
	if (!isMap()) {
		return nullptr;
	}
	for (const auto &[k, v] : map()) {
		if (k.isInt() && k.toInt() == key) {
			return &v;
		}
	}
	return nullptr;
}

Bytes CborEncode(const CborValue &value) {
	auto result = Bytes();
	EncodeTo(result, value);
	return result;
}

std::optional<CborValue> CborDecode(ByteSpan data) {
	auto reader = Reader{ data.data(), data.size() };
	auto result = DecodeOne(reader, 0);
	if (!result || reader.position != reader.size) {
		return std::nullopt;
	}
	return result;
}

void EcKeyDeleter::operator()(EC_KEY *key) const {
	EC_KEY_free(key);
}

bool RandomBytes(uint8_t *out, size_t size) {
	return RAND_bytes(out, int(size)) == 1;
}

std::array<uint8_t, 20> Sha1Digest(ByteSpan data) {
	auto result = std::array<uint8_t, 20>();
	SHA1(data.data(), data.size(), result.data());
	return result;
}

std::array<uint8_t, 32> Sha256Digest(ByteSpan data) {
	auto result = std::array<uint8_t, 32>();
	SHA256(data.data(), data.size(), result.data());
	return result;
}

std::array<uint8_t, 32> HmacSha256(ByteSpan key, ByteSpan data) {
	auto result = std::array<uint8_t, 32>();
	auto length = unsigned(0);
	HMAC(
		EVP_sha256(),
		key.data(),
		int(key.size()),
		data.data(),
		data.size(),
		result.data(),
		&length);
	return result;
}

void HkdfSha256(
		uint8_t *out,
		size_t outLength,
		ByteSpan secret,
		ByteSpan salt,
		ByteSpan info) {
	const auto context = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
	if (!context) {
		std::memset(out, 0, outLength);
		return;
	}
	auto length = outLength;
	static const auto kEmpty = uint8_t(0);
	const auto keyData = secret.empty() ? &kEmpty : secret.data();
	const auto ok = (EVP_PKEY_derive_init(context) > 0)
		&& (EVP_PKEY_CTX_set_hkdf_md(context, EVP_sha256()) > 0)
		&& (salt.empty()
			|| (EVP_PKEY_CTX_set1_hkdf_salt(
				context,
				salt.data(),
				int(salt.size())) > 0))
		&& (EVP_PKEY_CTX_set1_hkdf_key(
			context,
			keyData,
			int(secret.size())) > 0)
		&& (info.empty()
			|| (EVP_PKEY_CTX_add1_hkdf_info(
				context,
				info.data(),
				int(info.size())) > 0))
		&& (EVP_PKEY_derive(context, out, &length) > 0);
	if (!ok) {
		std::memset(out, 0, outLength);
	}
	EVP_PKEY_CTX_free(context);
}

EcKeyPtr GenerateP256Key() {
	auto key = EcKeyPtr(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
	if (!key || EC_KEY_generate_key(key.get()) != 1) {
		return nullptr;
	}
	return key;
}

Bytes PublicKeyX962(EC_KEY *key, bool compressed) {
	const auto group = EC_KEY_get0_group(key);
	const auto point = EC_KEY_get0_public_key(key);
	const auto form = compressed
		? POINT_CONVERSION_COMPRESSED
		: POINT_CONVERSION_UNCOMPRESSED;
	auto result = Bytes(
		compressed ? kCompressedPublicKeySize : kP256X962Length);
	const auto written = EC_POINT_point2oct(
		group,
		point,
		form,
		result.data(),
		result.size(),
		nullptr);
	if (written != result.size()) {
		return {};
	}
	return result;
}

std::optional<std::array<uint8_t, 32>> EcdhSharedX(
		EC_KEY *privateKey,
		ByteSpan peerX962) {
	const auto group = EC_KEY_get0_group(privateKey);
	const auto point = EC_POINT_new(group);
	if (!point) {
		return std::nullopt;
	}
	auto result = std::array<uint8_t, 32>();
	const auto ok = (EC_POINT_oct2point(
		group,
		point,
		peerX962.data(),
		peerX962.size(),
		nullptr) == 1)
		&& (ECDH_compute_key(
			result.data(),
			result.size(),
			point,
			privateKey,
			nullptr) == int(result.size()));
	EC_POINT_free(point);
	if (!ok) {
		return std::nullopt;
	}
	return result;
}

Bytes AesGcmSeal(
		ByteSpan key32,
		ByteSpan nonce12,
		ByteSpan plaintext,
		ByteSpan additionalData) {
	auto result = Bytes(plaintext.size() + 16);
	const auto context = EVP_CIPHER_CTX_new();
	if (!context) {
		return {};
	}
	auto outLength = 0;
	auto fullLength = 0;
	auto ok = (EVP_EncryptInit_ex(
		context,
		EVP_aes_256_gcm(),
		nullptr,
		nullptr,
		nullptr) == 1)
		&& (EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_SET_IVLEN,
			int(nonce12.size()),
			nullptr) == 1)
		&& (EVP_EncryptInit_ex(
			context,
			nullptr,
			nullptr,
			key32.data(),
			nonce12.data()) == 1);
	if (ok && !additionalData.empty()) {
		ok = (EVP_EncryptUpdate(
			context,
			nullptr,
			&outLength,
			additionalData.data(),
			int(additionalData.size())) == 1);
	}
	if (ok && !plaintext.empty()) {
		ok = (EVP_EncryptUpdate(
			context,
			result.data(),
			&outLength,
			plaintext.data(),
			int(plaintext.size())) == 1);
		fullLength = outLength;
	}
	ok = ok
		&& (EVP_EncryptFinal_ex(
			context,
			result.data() + fullLength,
			&outLength) == 1)
		&& (EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_GET_TAG,
			16,
			result.data() + plaintext.size()) == 1);
	EVP_CIPHER_CTX_free(context);
	if (!ok) {
		return {};
	}
	return result;
}

std::optional<Bytes> AesGcmOpen(
		ByteSpan key32,
		ByteSpan nonce12,
		ByteSpan ciphertext,
		ByteSpan additionalData) {
	if (ciphertext.size() < 16) {
		return std::nullopt;
	}
	const auto plaintextLength = ciphertext.size() - 16;
	auto result = Bytes(plaintextLength);
	auto tag = std::array<uint8_t, 16>();
	std::copy(
		ciphertext.begin() + plaintextLength,
		ciphertext.end(),
		tag.begin());
	const auto context = EVP_CIPHER_CTX_new();
	if (!context) {
		return std::nullopt;
	}
	auto outLength = 0;
	auto fullLength = 0;
	auto ok = (EVP_DecryptInit_ex(
		context,
		EVP_aes_256_gcm(),
		nullptr,
		nullptr,
		nullptr) == 1)
		&& (EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_SET_IVLEN,
			int(nonce12.size()),
			nullptr) == 1)
		&& (EVP_DecryptInit_ex(
			context,
			nullptr,
			nullptr,
			key32.data(),
			nonce12.data()) == 1);
	if (ok && !additionalData.empty()) {
		ok = (EVP_DecryptUpdate(
			context,
			nullptr,
			&outLength,
			additionalData.data(),
			int(additionalData.size())) == 1);
	}
	if (ok && plaintextLength > 0) {
		ok = (EVP_DecryptUpdate(
			context,
			result.data(),
			&outLength,
			ciphertext.data(),
			int(plaintextLength)) == 1);
		fullLength = outLength;
	}
	ok = ok
		&& (EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_SET_TAG,
			16,
			tag.data()) == 1)
		&& (EVP_DecryptFinal_ex(
			context,
			result.data() + fullLength,
			&outLength) == 1);
	EVP_CIPHER_CTX_free(context);
	if (!ok) {
		return std::nullopt;
	}
	return result;
}

void Noise::init(HandshakeType type) {
	const auto name = (type == HandshakeType::KNpsk0)
		? "Noise_KNpsk0_P256_AESGCM_SHA256"
		: "Noise_NKpsk0_P256_AESGCM_SHA256";
	_chainingKey.fill(0);
	std::memcpy(_chainingKey.data(), name, std::strlen(name));
	_h = _chainingKey;
	_symmetricKey.fill(0);
	_symmetricNonce = 0;
}

void Noise::mixHash(ByteSpan in) {
	auto data = Bytes();
	data.reserve(_h.size() + in.size());
	data.insert(data.end(), _h.begin(), _h.end());
	data.insert(data.end(), in.begin(), in.end());
	_h = Sha256Digest(data);
}

void Noise::mixHashPoint(ByteSpan pointX962) {
	mixHash(pointX962);
}

void Noise::mixKey(ByteSpan ikm) {
	auto output = std::array<uint8_t, 64>();
	HkdfSha256(output.data(), output.size(), ikm, _chainingKey, {});
	std::memcpy(_chainingKey.data(), output.data(), 32);
	initializeKey(ByteSpan(output.data() + 32, 32));
}

void Noise::mixKeyAndHash(ByteSpan ikm) {
	auto output = std::array<uint8_t, 96>();
	HkdfSha256(output.data(), output.size(), ikm, _chainingKey, {});
	std::memcpy(_chainingKey.data(), output.data(), 32);
	mixHash(ByteSpan(output.data() + 32, 32));
	initializeKey(ByteSpan(output.data() + 64, 32));
}

Bytes Noise::encryptAndHash(ByteSpan plaintext) {
	// Counter in the first 4 nonce bytes, big-endian, matching Chromium.
	auto nonce = std::array<uint8_t, 12>();
	nonce.fill(0);
	nonce[0] = uint8_t(_symmetricNonce >> 24);
	nonce[1] = uint8_t(_symmetricNonce >> 16);
	nonce[2] = uint8_t(_symmetricNonce >> 8);
	nonce[3] = uint8_t(_symmetricNonce);
	++_symmetricNonce;
	auto ciphertext = AesGcmSeal(_symmetricKey, nonce, plaintext, _h);
	mixHash(ciphertext);
	return ciphertext;
}

std::optional<Bytes> Noise::decryptAndHash(ByteSpan ciphertext) {
	auto nonce = std::array<uint8_t, 12>();
	nonce.fill(0);
	nonce[0] = uint8_t(_symmetricNonce >> 24);
	nonce[1] = uint8_t(_symmetricNonce >> 16);
	nonce[2] = uint8_t(_symmetricNonce >> 8);
	nonce[3] = uint8_t(_symmetricNonce);
	++_symmetricNonce;
	auto plaintext = AesGcmOpen(_symmetricKey, nonce, ciphertext, _h);
	if (plaintext) {
		mixHash(ciphertext);
	}
	return plaintext;
}

auto Noise::trafficKeys() const
-> std::pair<std::array<uint8_t, 32>, std::array<uint8_t, 32>> {
	auto output = std::array<uint8_t, 64>();
	HkdfSha256(output.data(), output.size(), {}, _chainingKey, {});
	auto first = std::array<uint8_t, 32>();
	auto second = std::array<uint8_t, 32>();
	std::memcpy(first.data(), output.data(), 32);
	std::memcpy(second.data(), output.data() + 32, 32);
	return { first, second };
}

void Noise::initializeKey(ByteSpan key32) {
	std::memcpy(_symmetricKey.data(), key32.data(), 32);
	_symmetricNonce = 0;
}

Crypter::Crypter(ByteSpan readKey32, ByteSpan writeKey32) {
	std::memcpy(_readKey.data(), readKey32.data(), 32);
	std::memcpy(_writeKey.data(), writeKey32.data(), 32);
}

std::optional<Bytes> Crypter::encrypt(ByteSpan plaintext) {
	if (_writeSequence > kMaxSequence) {
		return std::nullopt;
	}
	const auto paddedSize = (plaintext.size() + 1 + kPaddingGranularity - 1)
		& ~(kPaddingGranularity - 1);
	auto padded = Bytes(paddedSize, 0);
	std::copy(plaintext.begin(), plaintext.end(), padded.begin());
	padded[paddedSize - 1] = uint8_t(paddedSize - plaintext.size() - 1);

	auto nonce = std::array<uint8_t, 12>();
	nonce.fill(0);
	const auto counter = _writeSequence++;
	nonce[8] = uint8_t(counter >> 24);
	nonce[9] = uint8_t(counter >> 16);
	nonce[10] = uint8_t(counter >> 8);
	nonce[11] = uint8_t(counter);
	auto result = AesGcmSeal(_writeKey, nonce, padded, {});
	if (result.empty()) {
		return std::nullopt;
	}
	return result;
}

std::optional<Bytes> Crypter::decrypt(ByteSpan ciphertext) {
	if (_readSequence > kMaxSequence) {
		return std::nullopt;
	}
	auto nonce = std::array<uint8_t, 12>();
	nonce.fill(0);
	nonce[8] = uint8_t(_readSequence >> 24);
	nonce[9] = uint8_t(_readSequence >> 16);
	nonce[10] = uint8_t(_readSequence >> 8);
	nonce[11] = uint8_t(_readSequence);
	auto plaintext = AesGcmOpen(_readKey, nonce, ciphertext, {});
	if (!plaintext) {
		return std::nullopt;
	}
	++_readSequence;
	if (plaintext->empty()) {
		return std::nullopt;
	}
	const auto padding = size_t(plaintext->back());
	if (padding + 1 > plaintext->size()) {
		return std::nullopt;
	}
	plaintext->resize(plaintext->size() - padding - 1);
	return plaintext;
}

HandshakeInitiator::HandshakeInitiator(ByteSpan psk32, EC_KEY *localIdentity)
: _localIdentity(localIdentity) {
	std::memcpy(_psk.data(), psk32.data(), 32);
}

Bytes HandshakeInitiator::buildInitialMessage() {
	_noise.init(Noise::HandshakeType::KNpsk0);
	const auto prologue = std::array<uint8_t, 1>{ 1 };
	_noise.mixHash(prologue);
	_noise.mixHashPoint(PublicKeyX962(_localIdentity, false));
	_noise.mixKeyAndHash(_psk);

	_ephemeralKey = GenerateP256Key();
	if (!_ephemeralKey) {
		return {};
	}
	const auto ephemeralPublic = PublicKeyX962(_ephemeralKey.get(), false);
	_noise.mixHash(ephemeralPublic);
	_noise.mixKey(ephemeralPublic);

	const auto ciphertext = _noise.encryptAndHash({});
	auto message = Bytes();
	message.reserve(ephemeralPublic.size() + ciphertext.size());
	message.insert(
		message.end(),
		ephemeralPublic.begin(),
		ephemeralPublic.end());
	message.insert(message.end(), ciphertext.begin(), ciphertext.end());
	return message;
}

std::optional<HandshakeResult> HandshakeInitiator::processResponse(
		ByteSpan response) {
	if (response.size() != kHandshakeResponseSize || !_ephemeralKey) {
		return std::nullopt;
	}
	const auto peerPointBytes = response.first(kP256X962Length);
	const auto ciphertext = response.subspan(kP256X962Length);

	const auto sharedEe = EcdhSharedX(_ephemeralKey.get(), peerPointBytes);
	if (!sharedEe) {
		return std::nullopt;
	}
	_noise.mixHash(peerPointBytes);
	_noise.mixKey(peerPointBytes);
	_noise.mixKey(*sharedEe);

	const auto sharedSe = EcdhSharedX(_localIdentity, peerPointBytes);
	if (!sharedSe) {
		return std::nullopt;
	}
	_noise.mixKey(*sharedSe);

	const auto plaintext = _noise.decryptAndHash(ciphertext);
	if (!plaintext || !plaintext->empty()) {
		return std::nullopt;
	}
	const auto [writeKey, readKey] = _noise.trafficKeys();
	auto result = HandshakeResult();
	result.crypter = std::make_unique<Crypter>(readKey, writeKey);
	result.handshakeHash = _noise.handshakeHash();
	return result;
}

std::optional<HandshakeResult> RespondToHandshake(
		ByteSpan psk32,
		ByteSpan peerIdentityX962,
		ByteSpan message,
		Bytes *outResponse) {
	if (message.size() < kP256X962Length) {
		return std::nullopt;
	}
	const auto peerPointBytes = message.first(kP256X962Length);
	const auto ciphertext = message.subspan(kP256X962Length);

	auto noise = Noise();
	noise.init(Noise::HandshakeType::KNpsk0);
	const auto prologue = std::array<uint8_t, 1>{ 1 };
	noise.mixHash(prologue);
	noise.mixHash(peerIdentityX962);
	noise.mixKeyAndHash(psk32);

	noise.mixHash(peerPointBytes);
	noise.mixKey(peerPointBytes);

	const auto ephemeralKey = GenerateP256Key();
	if (!ephemeralKey) {
		return std::nullopt;
	}
	const auto plaintext = noise.decryptAndHash(ciphertext);
	if (!plaintext || !plaintext->empty()) {
		return std::nullopt;
	}
	const auto ephemeralPublic = PublicKeyX962(ephemeralKey.get(), false);
	noise.mixHash(ephemeralPublic);
	noise.mixKey(ephemeralPublic);

	const auto sharedEe = EcdhSharedX(ephemeralKey.get(), peerPointBytes);
	if (!sharedEe) {
		return std::nullopt;
	}
	noise.mixKey(*sharedEe);

	const auto sharedSe = EcdhSharedX(ephemeralKey.get(), peerIdentityX962);
	if (!sharedSe) {
		return std::nullopt;
	}
	noise.mixKey(*sharedSe);

	const auto myCiphertext = noise.encryptAndHash({});
	outResponse->insert(
		outResponse->end(),
		ephemeralPublic.begin(),
		ephemeralPublic.end());
	outResponse->insert(
		outResponse->end(),
		myCiphertext.begin(),
		myCiphertext.end());

	const auto [readKey, writeKey] = noise.trafficKeys();
	auto result = HandshakeResult();
	result.crypter = std::make_unique<Crypter>(readKey, writeKey);
	result.handshakeHash = noise.handshakeHash();
	return result;
}

QRKey MakeQRKey() {
	auto result = QRKey();
	result.identity = GenerateP256Key();
	if (!RandomBytes(result.secret.data(), result.secret.size())) {
		result.identity = nullptr;
	}
	return result;
}

std::string EncodeQRContents(
		const QRKey &key,
		bool makeCredentialHint,
		int64_t nowUnixSeconds) {
	auto map = CborMap();
	map.emplace_back(0, PublicKeyX962(key.identity.get(), true));
	map.emplace_back(1, ByteSpan(key.secret));
	map.emplace_back(2, int64_t(kAssignedTunnelDomains.size()));
	map.emplace_back(3, nowUnixSeconds);
	map.emplace_back(4, false);
	map.emplace_back(5, makeCredentialHint ? "mc" : "ga");
	return "FIDO:/" + BytesToDigits(CborEncode(CborValue(std::move(map))));
}

std::string BytesToDigits(ByteSpan bytes) {
	constexpr auto kWidths = std::array{ 0, 3, 5, 8, 10, 13, 15, 17 };
	auto result = std::string();
	result.reserve(((bytes.size() + 6) / 7) * 17);
	while (!bytes.empty()) {
		const auto take = std::min(bytes.size(), size_t(7));
		auto value = uint64_t(0);
		for (auto i = size_t(0); i != take; ++i) {
			value |= uint64_t(bytes[i]) << (8 * i);
		}
		char buffer[24];
		std::snprintf(
			buffer,
			sizeof(buffer),
			"%0*llu",
			kWidths[take],
			static_cast<unsigned long long>(value));
		result += buffer;
		bytes = bytes.subspan(take);
	}
	return result;
}

std::optional<Bytes> DigitsToBytes(const std::string &digits) {
	auto result = Bytes();
	result.reserve(((digits.size() + 16) / 17) * 7);
	auto in = std::string_view(digits);
	const auto parse = [](std::string_view chunk, uint64_t &value) {
		value = 0;
		for (const auto ch : chunk) {
			if (ch < '0' || ch > '9') {
				return false;
			}
			if (value > (UINT64_MAX - uint64_t(ch - '0')) / 10) {
				return false;
			}
			value = value * 10 + uint64_t(ch - '0');
		}
		return true;
	};
	while (in.size() >= 17) {
		auto value = uint64_t(0);
		if (!parse(in.substr(0, 17), value) || (value >> 56) != 0) {
			return std::nullopt;
		}
		for (auto i = 0; i != 7; ++i) {
			result.push_back(uint8_t(value >> (8 * i)));
		}
		in = in.substr(17);
	}
	if (!in.empty()) {
		auto bytesCount = size_t(0);
		switch (in.size()) {
		case 3: bytesCount = 1; break;
		case 5: bytesCount = 2; break;
		case 8: bytesCount = 3; break;
		case 10: bytesCount = 4; break;
		case 13: bytesCount = 5; break;
		case 15: bytesCount = 6; break;
		default: return std::nullopt;
		}
		auto value = uint64_t(0);
		if (!parse(in, value)
			|| (bytesCount < 8 && (value >> (8 * bytesCount)) != 0)) {
			return std::nullopt;
		}
		for (auto i = size_t(0); i != bytesCount; ++i) {
			result.push_back(uint8_t(value >> (8 * i)));
		}
	}
	return result;
}

std::optional<std::array<uint8_t, kEidSize>> DecryptAdvert(
		ByteSpan serviceData,
		ByteSpan eidKey64) {
	if (serviceData.size() < kAdvertSize || eidKey64.size() != kEidKeySize) {
		return std::nullopt;
	}
	const auto aesKey = eidKey64.first(32);
	const auto hmacKey = eidKey64.subspan(32);
	const auto body = serviceData.first(16);
	const auto tag = serviceData.subspan(16, 4);
	const auto expected = HmacSha256(hmacKey, body);
	if (!std::equal(tag.begin(), tag.end(), expected.begin())) {
		return std::nullopt;
	}
	auto aes = AES_KEY();
	if (AES_set_decrypt_key(aesKey.data(), 256, &aes) != 0) {
		return std::nullopt;
	}
	auto plaintext = std::array<uint8_t, kEidSize>();
	AES_decrypt(body.data(), plaintext.data(), &aes);
	if (plaintext[0] != 0) {
		return std::nullopt;
	}
	const auto domain = uint16_t(plaintext[14])
		| (uint16_t(plaintext[15]) << 8);
	if (domain >= 256 || domain < kAssignedTunnelDomains.size()) {
		return plaintext;
	}
	return std::nullopt;
}

EidComponents ToEidComponents(const std::array<uint8_t, kEidSize> &eid) {
	auto result = EidComponents();
	std::memcpy(result.nonce.data(), eid.data() + 1, kNonceSize);
	std::memcpy(
		result.routingId.data(),
		eid.data() + 1 + kNonceSize,
		kRoutingIdSize);
	result.tunnelServerDomain = uint16_t(eid[14])
		| (uint16_t(eid[15]) << 8);
	return result;
}

std::string DecodeTunnelServerDomain(uint16_t domain) {
	if (domain < 256) {
		return (domain < kAssignedTunnelDomains.size())
			? kAssignedTunnelDomains[domain]
			: std::string();
	}
	auto input = Bytes();
	const auto prefix = std::string_view("caBLEv2 tunnel server domain");
	input.insert(input.end(), prefix.begin(), prefix.end());
	input.push_back(uint8_t(domain & 0xFF));
	input.push_back(uint8_t(domain >> 8));
	input.push_back(0);
	const auto digest = Sha256Digest(input);
	auto value = uint64_t(0);
	for (auto i = 0; i != 8; ++i) {
		value |= uint64_t(digest[i]) << (8 * i);
	}
	constexpr auto kBase32 = std::string_view(
		"abcdefghijklmnopqrstuvwxyz234567");
	constexpr auto kTlds = std::array{ ".com", ".org", ".net", ".info" };
	const auto tldIndex = size_t(value & 3);
	value >>= 2;
	auto result = std::string("cable.");
	while (value != 0) {
		result += kBase32[value & 31];
		value >>= 5;
	}
	result += kTlds[tldIndex];
	return result;
}

std::string HexLower(ByteSpan bytes) {
	constexpr auto kHex = "0123456789abcdef";
	auto result = std::string();
	result.reserve(bytes.size() * 2);
	for (const auto byte : bytes) {
		result += kHex[byte >> 4];
		result += kHex[byte & 0x0F];
	}
	return result;
}

Bytes BuildMakeCredentialRequest(
		ByteSpan clientDataHash32,
		const std::string &rpId,
		const std::string &rpName,
		ByteSpan userId,
		const std::string &userName,
		const std::string &userDisplayName,
		const std::vector<int> &algorithms) {
	auto params = CborArray();
	for (const auto algorithm : algorithms) {
		auto param = CborMap();
		param.emplace_back("alg", algorithm);
		param.emplace_back("type", "public-key");
		params.push_back(CborValue(std::move(param)));
	}
	auto rp = CborMap();
	rp.emplace_back("id", rpId);
	rp.emplace_back("name", rpName);
	auto user = CborMap();
	user.emplace_back("id", userId);
	user.emplace_back("name", userName);
	user.emplace_back("displayName", userDisplayName);
	auto options = CborMap();
	options.emplace_back("rk", true);
	options.emplace_back("uv", true);
	auto map = CborMap();
	map.emplace_back(1, clientDataHash32);
	map.emplace_back(2, std::move(rp));
	map.emplace_back(3, std::move(user));
	map.emplace_back(4, std::move(params));
	map.emplace_back(7, std::move(options));
	auto result = Bytes{ 0x01 };
	const auto encoded = CborEncode(CborValue(std::move(map)));
	result.insert(result.end(), encoded.begin(), encoded.end());
	return result;
}

Bytes BuildGetAssertionRequest(
		const std::string &rpId,
		ByteSpan clientDataHash32,
		const std::vector<Bytes> &allowCredentialIds) {
	auto map = CborMap();
	map.emplace_back(1, rpId);
	map.emplace_back(2, clientDataHash32);
	if (!allowCredentialIds.empty()) {
		auto allow = CborArray();
		for (const auto &id : allowCredentialIds) {
			auto descriptor = CborMap();
			descriptor.emplace_back("id", id);
			descriptor.emplace_back("type", "public-key");
			allow.push_back(CborValue(std::move(descriptor)));
		}
		map.emplace_back(3, std::move(allow));
	}
	auto options = CborMap();
	options.emplace_back("up", true);
	options.emplace_back("uv", true);
	map.emplace_back(5, std::move(options));
	auto result = Bytes{ 0x02 };
	const auto encoded = CborEncode(CborValue(std::move(map)));
	result.insert(result.end(), encoded.begin(), encoded.end());
	return result;
}

std::optional<Bytes> CredentialIdFromAuthData(ByteSpan authData) {
	constexpr auto kPrefix = size_t(32 + 1 + 4);
	if (authData.size() < kPrefix + 16 + 2) {
		return std::nullopt;
	}
	const auto flags = authData[32];
	if (!(flags & 0x40)) {
		return std::nullopt;
	}
	const auto idLength = (size_t(authData[kPrefix + 16]) << 8)
		| size_t(authData[kPrefix + 17]);
	if (authData.size() < kPrefix + 18 + idLength) {
		return std::nullopt;
	}
	const auto from = authData.begin() + kPrefix + 18;
	return Bytes(from, from + idLength);
}

std::optional<MakeCredentialResponse> ParseMakeCredentialResponse(
		ByteSpan payloadWithStatus) {
	if (payloadWithStatus.empty()
		|| payloadWithStatus[0] != kCtapSuccess) {
		return std::nullopt;
	}
	const auto parsed = CborDecode(payloadWithStatus.subspan(1));
	if (!parsed || !parsed->isMap()) {
		return std::nullopt;
	}
	const auto authData = parsed->find(2);
	if (!authData || !authData->isBytes()) {
		return std::nullopt;
	}
	auto credentialId = CredentialIdFromAuthData(authData->bytes());
	if (!credentialId) {
		return std::nullopt;
	}
	auto result = MakeCredentialResponse();
	result.authData = authData->bytes();
	result.credentialId = std::move(*credentialId);
	return result;
}

std::optional<GetAssertionResponse> ParseGetAssertionResponse(
		ByteSpan payloadWithStatus) {
	if (payloadWithStatus.empty()
		|| payloadWithStatus[0] != kCtapSuccess) {
		return std::nullopt;
	}
	const auto parsed = CborDecode(payloadWithStatus.subspan(1));
	if (!parsed || !parsed->isMap()) {
		return std::nullopt;
	}
	auto result = GetAssertionResponse();
	if (const auto credential = parsed->find(1)) {
		if (credential->isMap()) {
			const auto id = FindText(credential->map(), "id");
			if (id && id->isBytes()) {
				result.credentialId = id->bytes();
			}
		}
	}
	const auto authData = parsed->find(2);
	const auto signature = parsed->find(3);
	if (!authData
		|| !authData->isBytes()
		|| !signature
		|| !signature->isBytes()) {
		return std::nullopt;
	}
	result.authData = authData->bytes();
	result.signature = signature->bytes();
	if (const auto user = parsed->find(4)) {
		if (user->isMap()) {
			const auto id = FindText(user->map(), "id");
			if (id && id->isBytes()) {
				result.userHandle = id->bytes();
			}
		}
	}
	return result;
}

std::optional<PostHandshakeMessage> ParsePostHandshakeMessage(
		ByteSpan plaintext) {
	auto revision = 1;
	auto parsed = CborDecode(plaintext);
	if (!parsed || !parsed->isMap()) {
		parsed = DecodePaddedCborMap(plaintext);
		revision = 0;
	}
	if (!parsed || !parsed->isMap()) {
		return std::nullopt;
	}
	auto result = PostHandshakeMessage();
	result.protocolRevision = revision;
	if (const auto features = parsed->find(3)) {
		if (!features->isArray()) {
			return std::nullopt;
		}
		result.supportsCtap = false;
		for (const auto &feature : features->array()) {
			if (feature.isString() && feature.string() == "ctap") {
				result.supportsCtap = true;
			}
		}
	}
	if (const auto getInfo = parsed->find(1)) {
		if (!getInfo->isBytes()) {
			return std::nullopt;
		}
		result.getInfo = getInfo->bytes();
	}
	return result;
}

} // namespace Platform::WebAuthn::Cable
