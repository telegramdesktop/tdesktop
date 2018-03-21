/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_encryption.h"

#include "base/openssl_help.h"

#include <openssl/aes.h>
#include <openssl/crypto.h>

namespace Passport {
namespace {

constexpr auto kAesKeyLength = 32;
constexpr auto kAesIvLength = 16;
constexpr auto kSecretSize = 32;
constexpr auto kMinPadding = 32;
constexpr auto kMaxPadding = 255;
constexpr auto kAlignTo = 16;

base::byte_vector SerializeData(const std::map<QString, QString> &data) {
	auto root = QJsonObject();
	for (const auto &[key, value] : data) {
		root.insert(key, value);
	}
	auto document = QJsonDocument(root);
	const auto result = document.toJson(QJsonDocument::Compact);
	const auto bytes = gsl::as_bytes(gsl::make_span(result));
	return { bytes.begin(), bytes.end() };
}

std::map<QString, QString> DeserializeData(base::const_byte_span bytes) {
	const auto serialized = QByteArray::fromRawData(
		reinterpret_cast<const char*>(bytes.data()),
		bytes.size());
	auto error = QJsonParseError();
	auto document = QJsonDocument::fromJson(serialized, &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("API Error: Could not deserialize decrypted JSON, error %1"
			).arg(error.errorString()));
		return {};
	} else if (!document.isObject()) {
		LOG(("API Error: decrypted JSON root is not an object."));
		return {};
	}
	auto object = document.object();
	auto result = std::map<QString, QString>();
	for (auto i = object.constBegin(), e = object.constEnd(); i != e; ++i) {
		const auto key = i.key();
		switch (i->type()) {
		case QJsonValue::Null: {
			LOG(("API Error: null found inside decrypted JSON root. "
				"Defaulting to empty string value."));
			result[key] = QString();
		} break;
		case QJsonValue::Undefined: {
			LOG(("API Error: undefined found inside decrypted JSON root. "
				"Defaulting to empty string value."));
			result[key] = QString();
		} break;
		case QJsonValue::Bool: {
			LOG(("API Error: bool found inside decrypted JSON root. "
				"Aborting."));
			return {};
		} break;
		case QJsonValue::Double: {
			LOG(("API Error: double found inside decrypted JSON root. "
				"Converting to string."));
			result[key] = QString::number(i->toDouble());
		} break;
		case QJsonValue::String: {
			result[key] = i->toString();
		} break;
		case QJsonValue::Array: {
			LOG(("API Error: array found inside decrypted JSON root. "
				"Aborting."));
			return {};
		} break;
		case QJsonValue::Object: {
			LOG(("API Error: object found inside decrypted JSON root. "
				"Aborting."));
			return {};
		} break;
		}
	}
	return result;
}

} // namespace

struct AesParams {
	base::byte_array<kAesKeyLength> key;
	base::byte_array<kAesIvLength> iv;
};

AesParams PrepareAesParams(base::const_byte_span secretHash) {
	const auto hash = openssl::Sha512(secretHash);
	const auto view = gsl::make_span(hash);
	const auto key = view.subspan(0, kAesKeyLength);
	const auto iv = view.subspan(kAesKeyLength, kAesIvLength);

	auto result = AesParams();
	base::copy_bytes(result.key, view.subspan(0, kAesKeyLength));
	base::copy_bytes(result.iv, view.subspan(kAesKeyLength, kAesIvLength));
	return result;
}

base::byte_vector EncryptOrDecrypt(
		base::const_byte_span initial,
		AesParams &&params,
		int encryptOrDecrypt) {
	Expects((initial.size() & 0x0F) == 0);

	auto aesKey = AES_KEY();
	const auto error = (encryptOrDecrypt == AES_ENCRYPT)
		? AES_set_encrypt_key(
			reinterpret_cast<const uchar*>(params.key.data()),
			params.key.size() * CHAR_BIT,
			&aesKey)
		: AES_set_decrypt_key(
			reinterpret_cast<const uchar*>(params.key.data()),
			params.key.size() * CHAR_BIT,
			&aesKey);
	if (error != 0) {
		LOG(("App Error: Could not AES_set_encrypt_key, result %1"
			).arg(error));
		return {};
	}
	auto result = base::byte_vector(initial.size());
	AES_cbc_encrypt(
		reinterpret_cast<const uchar*>(initial.data()),
		reinterpret_cast<uchar*>(result.data()),
		initial.size(),
		&aesKey,
		reinterpret_cast<uchar*>(params.iv.data()),
		encryptOrDecrypt);
	return result;
}

base::byte_vector Encrypt(
		base::const_byte_span decrypted,
		AesParams &&params) {
	return EncryptOrDecrypt(decrypted, std::move(params), AES_ENCRYPT);
}

base::byte_vector Decrypt(
		base::const_byte_span encrypted,
		AesParams &&params) {
	return EncryptOrDecrypt(encrypted, std::move(params), AES_DECRYPT);
}

base::byte_vector PasswordHashForSecret(
		base::const_byte_span passwordUtf8) {
	//new_secure_salt = new_salt + random_bytes(8) // #TODO
	//password_hash = SHA512(new_secure_salt + password + new_secure_salt)
	const auto result = openssl::Sha512(passwordUtf8);
	return { result.begin(), result.end() };
}

bool CheckBytesMod255(base::const_byte_span bytes) {
	const auto full = std::accumulate(
		bytes.begin(),
		bytes.end(),
		0ULL,
		[](uint64 sum, gsl::byte value) { return sum + uchar(value); });
	const auto mod = (full % 255ULL);
	return (mod == 239);
}

bool CheckSecretBytes(base::const_byte_span secret) {
	return CheckBytesMod255(secret);
}

base::byte_vector GenerateSecretBytes() {
	auto result = base::byte_vector(kSecretSize);
	memset_rand(result.data(), result.size());
	const auto full = std::accumulate(
		result.begin(),
		result.end(),
		0ULL,
		[](uint64 sum, gsl::byte value) { return sum + uchar(value); });
	const auto mod = (full % 255ULL);
	const auto add = 255ULL + 239 - mod;
	auto first = (static_cast<uchar>(result[0]) + add) % 255ULL;
	result[0] = static_cast<gsl::byte>(first);
	return result;
}

base::byte_vector DecryptSecretBytes(
		base::const_byte_span encryptedSecret,
		base::const_byte_span passwordHashForSecret) {
	if (encryptedSecret.empty()) {
		return {};
	} else if (encryptedSecret.size() != kSecretSize) {
		LOG(("API Error: Wrong secret size %1"
			).arg(encryptedSecret.size()));
		return {};
	}
	auto params = PrepareAesParams(passwordHashForSecret);
	auto result = Decrypt(encryptedSecret, std::move(params));
	if (!CheckSecretBytes(result)) {
		LOG(("API Error: Bad secret bytes."));
		return {};
	}
	return result;
}

base::byte_vector EncryptSecretBytes(
		base::const_byte_span secret,
		base::const_byte_span passwordHashForSecret) {
	Expects(secret.size() == kSecretSize);
	Expects(CheckSecretBytes(secret) == true);

	auto params = PrepareAesParams(passwordHashForSecret);
	return Encrypt(secret, std::move(params));
}

base::byte_vector Concatenate(
		base::const_byte_span a,
		base::const_byte_span b) {
	auto result = base::byte_vector(a.size() + b.size());
	base::copy_bytes(result, a);
	base::copy_bytes(gsl::make_span(result).subspan(a.size()), b);
	return result;
}

EncryptedData EncryptData(
		base::const_byte_span dataSecret,
		const std::map<QString, QString> &data) {
	Expects(dataSecret.size() == kSecretSize);

	const auto bytes = SerializeData(data);

	constexpr auto kFromPadding = kMinPadding + kAlignTo - 1;
	constexpr auto kPaddingDelta = kMaxPadding - kFromPadding;
	const auto randomPadding = kFromPadding
		+ (rand_value<uint32>() % kPaddingDelta);
	const auto padding = randomPadding
		- ((bytes.size() + randomPadding) % kAlignTo);
	Assert(padding >= kMinPadding && padding <= kMaxPadding);

	auto unencrypted = base::byte_vector(padding + bytes.size());
	Assert(unencrypted.size() % kAlignTo == 0);

	unencrypted[0] = static_cast<gsl::byte>(padding);
	memset_rand(unencrypted.data() + 1, padding - 1);
	base::copy_bytes(
		gsl::make_span(unencrypted).subspan(padding),
		bytes);
	const auto dataHash = openssl::Sha256(unencrypted);
	const auto dataSecretHash = openssl::Sha512(
		Concatenate(dataSecret, dataHash));
	auto params = PrepareAesParams(dataSecretHash);
	return {
		{ dataHash.begin(), dataHash.end() },
		Encrypt(unencrypted, std::move(params))
	};
}

std::map<QString, QString> DecryptData(
		base::const_byte_span encrypted,
		base::const_byte_span dataHash,
		base::const_byte_span dataSecret) {
	constexpr auto kDataHashSize = 32;
	if (encrypted.empty()) {
		return {};
	} else if (dataHash.size() != kDataHashSize) {
		LOG(("API Error: Bad data hash size %1").arg(dataHash.size()));
		return {};
	} else if (dataSecret.size() != kSecretSize) {
		LOG(("API Error: Bad data secret size %1").arg(dataSecret.size()));
		return {};
	}

	const auto dataSecretHash = openssl::Sha512(
		Concatenate(dataSecret, dataHash));
	auto params = PrepareAesParams(dataSecretHash);
	const auto decrypted = Decrypt(encrypted, std::move(params));
	if (base::compare_bytes(openssl::Sha256(decrypted), dataHash) != 0) {
		LOG(("API Error: Bad data hash."));
		return {};
	}
	const auto padding = static_cast<uchar>(decrypted[0]);
	if (padding < kMinPadding
		|| padding > kMaxPadding
		|| padding > decrypted.size()) {
		LOG(("API Error: Bad padding value %1").arg(padding));
		return {};
	}
	const auto bytes = gsl::make_span(decrypted).subspan(padding);
	return DeserializeData(bytes);
}

base::byte_vector PrepareValueHash(
		base::const_byte_span dataHash,
		base::const_byte_span valueSecret) {
	const auto result = openssl::Sha256(Concatenate(dataHash, valueSecret));
	return { result.begin(), result.end() };
}

base::byte_vector EncryptValueSecret(
		base::const_byte_span valueSecret,
		base::const_byte_span secret,
		base::const_byte_span valueHash) {
	const auto valueSecretHash = openssl::Sha512(
		Concatenate(secret, valueHash));
	return EncryptSecretBytes(valueSecret, valueSecretHash);
}

base::byte_vector DecryptValueSecret(
		base::const_byte_span encrypted,
		base::const_byte_span secret,
		base::const_byte_span valueHash) {
	const auto valueSecretHash = openssl::Sha512(
		Concatenate(secret, valueHash));
	return DecryptSecretBytes(encrypted, valueSecretHash);
}

} // namespace Passport
