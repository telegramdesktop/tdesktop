/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

typedef struct ec_key_st EC_KEY;

namespace Platform::WebAuthn::Cable {

using Bytes = std::vector<uint8_t>;
using ByteSpan = std::span<const uint8_t>;

inline constexpr auto kP256X962Length = size_t(65);
inline constexpr auto kCompressedPublicKeySize = size_t(33);
inline constexpr auto kQRSecretSize = size_t(16);
inline constexpr auto kAdvertSize = size_t(20);
inline constexpr auto kEidSize = size_t(16);
inline constexpr auto kEidKeySize = size_t(64);
inline constexpr auto kNonceSize = size_t(10);
inline constexpr auto kRoutingIdSize = size_t(3);
inline constexpr auto kTunnelIdSize = size_t(16);
inline constexpr auto kPskSize = size_t(32);
inline constexpr auto kHandshakeResponseSize = kP256X962Length + 16;

enum class MessageType : uint8_t {
	Shutdown = 0,
	Ctap = 1,
	Update = 2,
	Json = 3,
};

class CborValue;
using CborArray = std::vector<CborValue>;
using CborMap = std::vector<std::pair<CborValue, CborValue>>;

class CborValue final {
public:
	CborValue(int64_t value) : _data(value) {
	}
	CborValue(int value) : _data(int64_t(value)) {
	}
	CborValue(Bytes value) : _data(std::move(value)) {
	}
	CborValue(ByteSpan value) : _data(Bytes(value.begin(), value.end())) {
	}
	CborValue(std::string value) : _data(std::move(value)) {
	}
	CborValue(const char *value) : _data(std::string(value)) {
	}
	CborValue(CborArray value) : _data(std::move(value)) {
	}
	CborValue(CborMap value) : _data(std::move(value)) {
	}
	CborValue(bool value) : _data(value) {
	}

	[[nodiscard]] bool isInt() const {
		return std::holds_alternative<int64_t>(_data);
	}
	[[nodiscard]] bool isBytes() const {
		return std::holds_alternative<Bytes>(_data);
	}
	[[nodiscard]] bool isString() const {
		return std::holds_alternative<std::string>(_data);
	}
	[[nodiscard]] bool isArray() const {
		return std::holds_alternative<CborArray>(_data);
	}
	[[nodiscard]] bool isMap() const {
		return std::holds_alternative<CborMap>(_data);
	}
	[[nodiscard]] bool isBool() const {
		return std::holds_alternative<bool>(_data);
	}
	[[nodiscard]] int64_t toInt() const {
		return std::get<int64_t>(_data);
	}
	[[nodiscard]] const Bytes &bytes() const {
		return std::get<Bytes>(_data);
	}
	[[nodiscard]] const std::string &string() const {
		return std::get<std::string>(_data);
	}
	[[nodiscard]] const CborArray &array() const {
		return std::get<CborArray>(_data);
	}
	[[nodiscard]] const CborMap &map() const {
		return std::get<CborMap>(_data);
	}
	[[nodiscard]] bool toBool() const {
		return std::get<bool>(_data);
	}

	[[nodiscard]] const CborValue *find(int64_t key) const;

private:
	std::variant<int64_t, Bytes, std::string, CborArray, CborMap, bool> _data;

};

[[nodiscard]] Bytes CborEncode(const CborValue &value);
[[nodiscard]] std::optional<CborValue> CborDecode(ByteSpan data);

struct EcKeyDeleter {
	void operator()(EC_KEY *key) const;
};
using EcKeyPtr = std::unique_ptr<EC_KEY, EcKeyDeleter>;

[[nodiscard]] bool RandomBytes(uint8_t *out, size_t size);
[[nodiscard]] std::array<uint8_t, 20> Sha1Digest(ByteSpan data);
[[nodiscard]] std::array<uint8_t, 32> Sha256Digest(ByteSpan data);
[[nodiscard]] std::array<uint8_t, 32> HmacSha256(
	ByteSpan key,
	ByteSpan data);
void HkdfSha256(
	uint8_t *out,
	size_t outLength,
	ByteSpan secret,
	ByteSpan salt,
	ByteSpan info);

enum class DerivedValueType : uint32_t {
	EidKey = 1,
	TunnelId = 2,
	Psk = 3,
};

template <size_t Size>
[[nodiscard]] std::array<uint8_t, Size> Derive(
		ByteSpan secret,
		ByteSpan nonce,
		DerivedValueType type) {
	const auto type32 = uint32_t(type);
	const auto info = std::array<uint8_t, 4>{
		uint8_t(type32 & 0xFF),
		uint8_t((type32 >> 8) & 0xFF),
		uint8_t((type32 >> 16) & 0xFF),
		uint8_t((type32 >> 24) & 0xFF),
	};
	auto result = std::array<uint8_t, Size>();
	HkdfSha256(result.data(), Size, secret, nonce, info);
	return result;
}

[[nodiscard]] EcKeyPtr GenerateP256Key();
[[nodiscard]] Bytes PublicKeyX962(EC_KEY *key, bool compressed);
[[nodiscard]] std::optional<std::array<uint8_t, 32>> EcdhSharedX(
	EC_KEY *privateKey,
	ByteSpan peerX962);

[[nodiscard]] Bytes AesGcmSeal(
	ByteSpan key32,
	ByteSpan nonce12,
	ByteSpan plaintext,
	ByteSpan additionalData);
[[nodiscard]] std::optional<Bytes> AesGcmOpen(
	ByteSpan key32,
	ByteSpan nonce12,
	ByteSpan ciphertext,
	ByteSpan additionalData);

class Noise final {
public:
	enum class HandshakeType {
		KNpsk0,
		NKpsk0,
	};

	void init(HandshakeType type);
	void mixHash(ByteSpan in);
	void mixHashPoint(ByteSpan pointX962);
	void mixKey(ByteSpan ikm);
	void mixKeyAndHash(ByteSpan ikm);
	[[nodiscard]] Bytes encryptAndHash(ByteSpan plaintext);
	[[nodiscard]] std::optional<Bytes> decryptAndHash(ByteSpan ciphertext);
	[[nodiscard]] auto trafficKeys() const
	-> std::pair<std::array<uint8_t, 32>, std::array<uint8_t, 32>>;
	[[nodiscard]] std::array<uint8_t, 32> handshakeHash() const {
		return _h;
	}

private:
	void initializeKey(ByteSpan key32);

	std::array<uint8_t, 32> _chainingKey = {};
	std::array<uint8_t, 32> _h = {};
	std::array<uint8_t, 32> _symmetricKey = {};
	uint32_t _symmetricNonce = 0;

};

class Crypter final {
public:
	Crypter(ByteSpan readKey32, ByteSpan writeKey32);

	[[nodiscard]] std::optional<Bytes> encrypt(ByteSpan plaintext);
	[[nodiscard]] std::optional<Bytes> decrypt(ByteSpan ciphertext);

private:
	std::array<uint8_t, 32> _readKey = {};
	std::array<uint8_t, 32> _writeKey = {};
	uint32_t _readSequence = 0;
	uint32_t _writeSequence = 0;

};

struct HandshakeResult {
	std::unique_ptr<Crypter> crypter;
	std::array<uint8_t, 32> handshakeHash = {};
};

class HandshakeInitiator final {
public:
	HandshakeInitiator(ByteSpan psk32, EC_KEY *localIdentity);

	[[nodiscard]] Bytes buildInitialMessage();
	[[nodiscard]] std::optional<HandshakeResult> processResponse(
		ByteSpan response);

private:
	std::array<uint8_t, 32> _psk = {};
	EC_KEY *_localIdentity = nullptr;
	EcKeyPtr _ephemeralKey;
	Noise _noise;

};

[[nodiscard]] std::optional<HandshakeResult> RespondToHandshake(
	ByteSpan psk32,
	ByteSpan peerIdentityX962,
	ByteSpan message,
	Bytes *outResponse);

struct QRKey {
	EcKeyPtr identity;
	std::array<uint8_t, kQRSecretSize> secret = {};
};

[[nodiscard]] QRKey MakeQRKey();
[[nodiscard]] std::string EncodeQRContents(
	const QRKey &key,
	bool makeCredentialHint,
	int64_t nowUnixSeconds);

[[nodiscard]] std::string BytesToDigits(ByteSpan bytes);
[[nodiscard]] std::optional<Bytes> DigitsToBytes(const std::string &digits);

struct EidComponents {
	std::array<uint8_t, kNonceSize> nonce = {};
	std::array<uint8_t, kRoutingIdSize> routingId = {};
	uint16_t tunnelServerDomain = 0;
};

[[nodiscard]] std::optional<std::array<uint8_t, kEidSize>> DecryptAdvert(
	ByteSpan serviceData,
	ByteSpan eidKey64);
[[nodiscard]] EidComponents ToEidComponents(
	const std::array<uint8_t, kEidSize> &eid);
[[nodiscard]] std::string DecodeTunnelServerDomain(uint16_t domain);
[[nodiscard]] std::string HexLower(ByteSpan bytes);

[[nodiscard]] Bytes BuildMakeCredentialRequest(
	ByteSpan clientDataHash32,
	const std::string &rpId,
	const std::string &rpName,
	ByteSpan userId,
	const std::string &userName,
	const std::string &userDisplayName,
	const std::vector<int> &algorithms);
[[nodiscard]] Bytes BuildGetAssertionRequest(
	const std::string &rpId,
	ByteSpan clientDataHash32,
	const std::vector<Bytes> &allowCredentialIds);

struct MakeCredentialResponse {
	Bytes authData;
	Bytes credentialId;
};
struct GetAssertionResponse {
	Bytes credentialId;
	Bytes authData;
	Bytes signature;
	Bytes userHandle;
};

inline constexpr auto kCtapSuccess = uint8_t(0);
inline constexpr auto kCtapErrOperationDenied = uint8_t(0x27);
inline constexpr auto kCtapErrNoCredentials = uint8_t(0x2E);
inline constexpr auto kCtapErrUserActionTimeout = uint8_t(0x2F);

[[nodiscard]] std::optional<MakeCredentialResponse>
ParseMakeCredentialResponse(ByteSpan payloadWithStatus);
[[nodiscard]] std::optional<GetAssertionResponse> ParseGetAssertionResponse(
	ByteSpan payloadWithStatus);
[[nodiscard]] std::optional<Bytes> CredentialIdFromAuthData(
	ByteSpan authData);

struct PostHandshakeMessage {
	Bytes getInfo;
	int protocolRevision = 1;
	bool supportsCtap = true;
};

[[nodiscard]] std::optional<PostHandshakeMessage> ParsePostHandshakeMessage(
	ByteSpan plaintext);

} // namespace Platform::WebAuthn::Cable
