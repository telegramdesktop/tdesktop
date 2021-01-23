/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_encryption.h"

#include "base/openssl_help.h"
#include "mtproto/details/mtproto_rsa_public_key.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

namespace Passport {
namespace {

constexpr auto kAesKeyLength = 32;
constexpr auto kAesIvLength = 16;
constexpr auto kSecretSize = 32;
constexpr auto kAesParamsHashSize = 64;
constexpr auto kMinPadding = 32;
constexpr auto kMaxPadding = 255;
constexpr auto kAlignTo = 16;

} // namespace

struct AesParams {
	bytes::vector key;
	bytes::vector iv;
};

AesParams PrepareAesParamsWithHash(bytes::const_span hashForEncryptionKey) {
	Expects(hashForEncryptionKey.size() == kAesParamsHashSize);

	auto result = AesParams();
	result.key = bytes::make_vector(
		hashForEncryptionKey.subspan(0, kAesKeyLength));
	result.iv = bytes::make_vector(
		hashForEncryptionKey.subspan(kAesKeyLength, kAesIvLength));
	return result;
}

AesParams PrepareAesParams(bytes::const_span bytesForEncryptionKey) {
	return PrepareAesParamsWithHash(openssl::Sha512(bytesForEncryptionKey));
}

bytes::vector EncryptOrDecrypt(
		bytes::const_span initial,
		AesParams &&params,
		int encryptOrDecrypt) {
	Expects((initial.size() & 0x0F) == 0);
	Expects(params.key.size() == kAesKeyLength);
	Expects(params.iv.size() == kAesIvLength);

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
	auto result = bytes::vector(initial.size());
	AES_cbc_encrypt(
		reinterpret_cast<const uchar*>(initial.data()),
		reinterpret_cast<uchar*>(result.data()),
		initial.size(),
		&aesKey,
		reinterpret_cast<uchar*>(params.iv.data()),
		encryptOrDecrypt);
	return result;
}

bytes::vector Encrypt(
		bytes::const_span decrypted,
		AesParams &&params) {
	return EncryptOrDecrypt(decrypted, std::move(params), AES_ENCRYPT);
}

bytes::vector Decrypt(
		bytes::const_span encrypted,
		AesParams &&params) {
	return EncryptOrDecrypt(encrypted, std::move(params), AES_DECRYPT);
}

bool CheckBytesMod255(bytes::const_span bytes) {
	const auto full = ranges::accumulate(
		bytes,
		0ULL,
		[](uint64 sum, gsl::byte value) { return sum + uchar(value); });
	const auto mod = (full % 255ULL);
	return (mod == 239);
}

bool CheckSecretBytes(bytes::const_span secret) {
	return CheckBytesMod255(secret);
}

bytes::vector GenerateSecretBytes() {
	auto result = bytes::vector(kSecretSize);
	memset_rand(result.data(), result.size());
	const auto full = ranges::accumulate(
		result,
		0ULL,
		[](uint64 sum, gsl::byte value) { return sum + uchar(value); });
	const auto mod = (full % 255ULL);
	const auto add = 255ULL + 239 - mod;
	auto first = (static_cast<uchar>(result[0]) + add) % 255ULL;
	result[0] = static_cast<gsl::byte>(first);
	return result;
}

bytes::vector DecryptSecretBytesWithHash(
		bytes::const_span encryptedSecret,
		bytes::const_span hashForEncryptionKey) {
	if (encryptedSecret.empty()) {
		return {};
	} else if (encryptedSecret.size() != kSecretSize) {
		LOG(("API Error: Wrong secret size %1"
			).arg(encryptedSecret.size()));
		return {};
	}
	auto params = PrepareAesParamsWithHash(hashForEncryptionKey);
	auto result = Decrypt(encryptedSecret, std::move(params));
	if (!CheckSecretBytes(result)) {
		LOG(("API Error: Bad secret bytes."));
		return {};
	}
	return result;
}

bytes::vector DecryptSecretBytes(
		bytes::const_span encryptedSecret,
		bytes::const_span bytesForEncryptionKey) {
	return DecryptSecretBytesWithHash(
		encryptedSecret,
		openssl::Sha512(bytesForEncryptionKey));
}

bytes::vector EncryptSecretBytesWithHash(
		bytes::const_span secret,
		bytes::const_span hashForEncryptionKey) {
	Expects(secret.size() == kSecretSize);
	Expects(CheckSecretBytes(secret) == true);

	auto params = PrepareAesParamsWithHash(hashForEncryptionKey);
	return Encrypt(secret, std::move(params));
}

bytes::vector EncryptSecretBytes(
		bytes::const_span secret,
		bytes::const_span bytesForEncryptionKey) {
	Expects(secret.size() == kSecretSize);
	Expects(CheckSecretBytes(secret) == true);

	auto params = PrepareAesParams(bytesForEncryptionKey);
	return Encrypt(secret, std::move(params));
}

bytes::vector DecryptSecureSecret(
		bytes::const_span encryptedSecret,
		bytes::const_span passwordHashForSecret) {
	Expects(!encryptedSecret.empty());

	return DecryptSecretBytesWithHash(
		encryptedSecret,
		passwordHashForSecret);
}

bytes::vector EncryptSecureSecret(
		bytes::const_span secret,
		bytes::const_span passwordHashForSecret) {
	Expects(secret.size() == kSecretSize);

	return EncryptSecretBytesWithHash(secret, passwordHashForSecret);
}

bytes::vector SerializeData(const std::map<QString, QString> &data) {
	auto root = QJsonObject();
	for (const auto &[key, value] : data) {
		root.insert(key, value);
	}
	auto document = QJsonDocument(root);
	const auto result = document.toJson(QJsonDocument::Compact);
	return bytes::make_vector(result);
}

std::map<QString, QString> DeserializeData(bytes::const_span bytes) {
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
		switch ((*i).type()) {
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
			result[key] = QString::number((*i).toDouble());
		} break;
		case QJsonValue::String: {
			result[key] = (*i).toString();
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

std::vector<DataError> DeserializeErrors(bytes::const_span json) {
	const auto serialized = QByteArray::fromRawData(
		reinterpret_cast<const char*>(json.data()),
		json.size());
	auto error = QJsonParseError();
	auto document = QJsonDocument::fromJson(serialized, &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("API Error: Could not deserialize errors JSON, error %1"
			).arg(error.errorString()));
		return {};
	} else if (!document.isArray()) {
		LOG(("API Error: Errors JSON root is not an array."));
		return {};
	}
	auto array = document.array();
	auto result = std::vector<DataError>();
	for (const auto &error : array) {
		if (!error.isObject()) {
			LOG(("API Error: Not an object inside errors JSON."));
			continue;
		}
		auto fields = error.toObject();
		const auto typeIt = fields.constFind("type");
		if (typeIt == fields.constEnd()) {
			LOG(("API Error: type was not found in an error."));
			continue;
		} else if (!(*typeIt).isString()) {
			LOG(("API Error: type was not a string in an error."));
			continue;
		}
		const auto descriptionIt = fields.constFind("description");
		if (descriptionIt == fields.constEnd()) {
			LOG(("API Error: description was not found in an error."));
			continue;
		} else if (!(*typeIt).isString()) {
			LOG(("API Error: description was not a string in an error."));
			continue;
		}
		const auto targetIt = fields.constFind("target");
		if (targetIt == fields.constEnd()) {
			LOG(("API Error: target aws not found in an error."));
			continue;
		} else if (!(*targetIt).isString()) {
			LOG(("API Error: target was not as string in an error."));
			continue;
		}
		auto next = DataError();
		next.type = (*typeIt).toString();
		next.text = (*descriptionIt).toString();
		const auto fieldIt = fields.constFind("field");
		const auto fileHashIt = fields.constFind("file_hash");
		if (fieldIt != fields.constEnd()) {
			if (!(*fieldIt).isString()) {
				LOG(("API Error: field was not a string in an error."));
				continue;
			}
			next.key = (*fieldIt).toString();
		} else if (fileHashIt != fields.constEnd()) {
			if (!(*fileHashIt).isString()) {
				LOG(("API Error: file_hash was not a string in an error."));
				continue;
			}
			next.key = QByteArray::fromBase64(
				(*fileHashIt).toString().toUtf8());
		} else if ((*targetIt).toString() == "selfie") {
			next.key = QByteArray();
		}
		result.push_back(std::move(next));
	}
	return result;
}

EncryptedData EncryptData(bytes::const_span bytes) {
	return EncryptData(bytes, GenerateSecretBytes());
}

EncryptedData EncryptData(
		bytes::const_span bytes,
		bytes::const_span dataSecret) {
	constexpr auto kFromPadding = kMinPadding + kAlignTo - 1;
	constexpr auto kPaddingDelta = kMaxPadding - kFromPadding;
	const auto randomPadding = kFromPadding
		+ (openssl::RandomValue<uint32>() % kPaddingDelta);
	const auto padding = randomPadding
		- ((bytes.size() + randomPadding) % kAlignTo);
	Assert(padding >= kMinPadding && padding <= kMaxPadding);

	auto unencrypted = bytes::vector(padding + bytes.size());
	Assert(unencrypted.size() % kAlignTo == 0);

	unencrypted[0] = static_cast<gsl::byte>(padding);
	memset_rand(unencrypted.data() + 1, padding - 1);
	bytes::copy(
		gsl::make_span(unencrypted).subspan(padding),
		bytes);
	const auto dataHash = openssl::Sha256(unencrypted);
	const auto bytesForEncryptionKey = bytes::concatenate(
		dataSecret,
		dataHash);

	auto params = PrepareAesParams(bytesForEncryptionKey);
	return {
		{ dataSecret.begin(), dataSecret.end() },
		{ dataHash.begin(), dataHash.end() },
		Encrypt(unencrypted, std::move(params))
	};
}

bytes::vector DecryptData(
		bytes::const_span encrypted,
		bytes::const_span dataHash,
		bytes::const_span dataSecret) {
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

	const auto bytesForEncryptionKey = bytes::concatenate(
		dataSecret,
		dataHash);
	auto params = PrepareAesParams(bytesForEncryptionKey);
	const auto decrypted = Decrypt(encrypted, std::move(params));
	if (bytes::compare(openssl::Sha256(decrypted), dataHash) != 0) {
		LOG(("API Error: Bad data hash."));
		return {};
	}
	const auto padding = static_cast<uint32>(decrypted[0]);
	if (padding < kMinPadding
		|| padding > kMaxPadding
		|| padding > decrypted.size()) {
		LOG(("API Error: Bad padding value %1").arg(padding));
		return {};
	}
	const auto bytes = gsl::make_span(decrypted).subspan(padding);
	return { bytes.begin(), bytes.end() };
}

bytes::vector PrepareValueHash(
		bytes::const_span dataHash,
		bytes::const_span valueSecret) {
	return openssl::Sha256(dataHash, valueSecret);
}

bytes::vector EncryptValueSecret(
		bytes::const_span valueSecret,
		bytes::const_span secret,
		bytes::const_span valueHash) {
	const auto bytesForEncryptionKey = bytes::concatenate(
		secret,
		valueHash);
	return EncryptSecretBytes(valueSecret, bytesForEncryptionKey);
}

bytes::vector DecryptValueSecret(
		bytes::const_span encrypted,
		bytes::const_span secret,
		bytes::const_span valueHash) {
	const auto bytesForEncryptionKey = bytes::concatenate(
		secret,
		valueHash);
	return DecryptSecretBytes(encrypted, bytesForEncryptionKey);
}

uint64 CountSecureSecretId(bytes::const_span secret) {
	const auto full = openssl::Sha256(secret);
	return *reinterpret_cast<const uint64*>(full.data());
}

bytes::vector EncryptCredentialsSecret(
		bytes::const_span secret,
		bytes::const_span publicKey) {
	const auto key = MTP::details::RSAPublicKey(publicKey);
	return key.encryptOAEPpadding(secret);
}

} // namespace Passport
