/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/crypto/gram_ed25519.h"

#include "base/openssl_help.h"

namespace Gram {
namespace {

constexpr auto kSeedSize = 32;
constexpr auto kPublicKeySize = 32;
constexpr auto kSecretKeySize = 64;
constexpr auto kSignatureSize = 64;

[[nodiscard]] EVP_PKEY *MakeRawPrivateKey(const QByteArray &secret) {
	return EVP_PKEY_new_raw_private_key(
		EVP_PKEY_ED25519,
		nullptr,
		reinterpret_cast<const unsigned char*>(secret.constData()),
		kSeedSize);
}

} // namespace

KeyPair::~KeyPair() {
	if (!secretKey.isEmpty()) {
		OPENSSL_cleanse(secretKey.data(), secretKey.size());
	}
}

KeyPair KeyPairFromSeed(const QByteArray &seed32) {
	if (seed32.size() != kSeedSize) {
		return {};
	}
	const auto key = MakeRawPrivateKey(seed32);
	if (!key) {
		return {};
	}
	const auto guardKey = gsl::finally([&] {
		EVP_PKEY_free(key);
	});

	auto publicKey = QByteArray(kPublicKeySize, 0);
	auto length = size_t(kPublicKeySize);
	const auto code = EVP_PKEY_get_raw_public_key(
		key,
		reinterpret_cast<unsigned char*>(publicKey.data()),
		&length);
	if (code != 1 || length != kPublicKeySize) {
		return {};
	}

	auto result = KeyPair();
	result.publicKey = publicKey;
	result.secretKey = seed32 + publicKey;
	return result;
}

QByteArray Sign(const QByteArray &data, const QByteArray &secretKey64) {
	if (secretKey64.size() != kSecretKeySize) {
		return QByteArray();
	}
	const auto key = MakeRawPrivateKey(secretKey64);
	if (!key) {
		return QByteArray();
	}
	const auto guardKey = gsl::finally([&] {
		EVP_PKEY_free(key);
	});

	const auto context = EVP_MD_CTX_new();
	if (!context) {
		return QByteArray();
	}
	const auto guardContext = gsl::finally([&] {
		EVP_MD_CTX_free(context);
	});

	if (EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key) != 1) {
		return QByteArray();
	}

	auto signature = QByteArray(kSignatureSize, 0);
	auto length = size_t(kSignatureSize);
	const auto code = EVP_DigestSign(
		context,
		reinterpret_cast<unsigned char*>(signature.data()),
		&length,
		reinterpret_cast<const unsigned char*>(data.constData()),
		data.size());
	if (code != 1 || length != kSignatureSize) {
		return QByteArray();
	}
	return signature;
}

bool Verify(
		const QByteArray &data,
		const QByteArray &signature,
		const QByteArray &publicKey32) {
	if (signature.size() != kSignatureSize
		|| publicKey32.size() != kPublicKeySize) {
		return false;
	}
	const auto key = EVP_PKEY_new_raw_public_key(
		EVP_PKEY_ED25519,
		nullptr,
		reinterpret_cast<const unsigned char*>(publicKey32.constData()),
		kPublicKeySize);
	if (!key) {
		return false;
	}
	const auto guardKey = gsl::finally([&] {
		EVP_PKEY_free(key);
	});

	const auto context = EVP_MD_CTX_new();
	if (!context) {
		return false;
	}
	const auto guardContext = gsl::finally([&] {
		EVP_MD_CTX_free(context);
	});

	if (EVP_DigestVerifyInit(context, nullptr, nullptr, nullptr, key) != 1) {
		return false;
	}
	return EVP_DigestVerify(
		context,
		reinterpret_cast<const unsigned char*>(signature.constData()),
		kSignatureSize,
		reinterpret_cast<const unsigned char*>(data.constData()),
		data.size()) == 1;
}

QByteArray FakeSign(const QByteArray &data) {
	return Sign(data, KeyPairFromSeed(QByteArray(kSeedSize, 0)).secretKey);
}

} // namespace Gram
