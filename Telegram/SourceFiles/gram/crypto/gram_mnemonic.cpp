/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/crypto/gram_mnemonic.h"

#include "base/openssl_help.h"
#include "gram/crypto/gram_hmac.h"
#include "gram/crypto/gram_slip10.h"
#include "gram/crypto/gram_wordlist.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace Gram {
namespace {

constexpr auto kPbkdfIterations = 100000;
constexpr auto kBasicSeedIterations = std::max(1, kPbkdfIterations / 256);
constexpr auto kBip39Iterations = 2048;
constexpr auto kGeneratedWordsCount = 24;
constexpr auto kWordIndexMask = quint32(0x7FF);

[[nodiscard]] QString NormalizeWord(const QString &word) {
	return word.trimmed().toLower();
}

[[nodiscard]] std::vector<QString> NormalizeWords(
		const std::vector<QString> &words) {
	auto result = std::vector<QString>();
	result.reserve(words.size());
	for (const auto &word : words) {
		result.push_back(NormalizeWord(word));
	}
	return result;
}

[[nodiscard]] bool ContainsWord(const QByteArray &utf8) {
	const auto list = Wordlist();
	const auto value = utf8.constData();
	return std::binary_search(
		list.begin(),
		list.end(),
		value,
		[](const char *a, const char *b) {
			return std::strcmp(a, b) < 0;
		});
}

[[nodiscard]] bool AllWordsInList(const std::vector<QString> &normalized) {
	for (const auto &word : normalized) {
		if (!ContainsWord(word.toUtf8())) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QString JoinedPhrase(
		const std::vector<QString> &normalized) {
	auto joined = QString();
	for (auto i = 0; i != int(normalized.size()); ++i) {
		if (i) {
			joined.append(u' ');
		}
		joined.append(normalized[i]);
	}
	return joined;
}

[[nodiscard]] QByteArray JoinedWords(
		const std::vector<QString> &normalized) {
	return JoinedPhrase(normalized).toUtf8();
}

[[nodiscard]] QByteArray MnemonicEntropy(const QByteArray &joined) {
	return HmacSha512(bytes::make_span(joined), bytes::const_span());
}

[[nodiscard]] bool IsBasicSeed(const QByteArray &entropy) {
	const auto salt = QByteArray("TON seed version");
	auto check = openssl::Pbkdf2Sha512(
		bytes::make_span(entropy),
		bytes::make_span(salt),
		kBasicSeedIterations);
	const auto result = (check[0] == bytes::type(0));
	OPENSSL_cleanse(check.data(), check.size());
	return result;
}

[[nodiscard]] bool PhrasePassesBasicSeed(
		const std::vector<QString> &words) {
	auto joined = JoinedWords(words);
	auto entropy = MnemonicEntropy(joined);
	const auto result = IsBasicSeed(entropy);
	OPENSSL_cleanse(entropy.data(), entropy.size());
	OPENSSL_cleanse(joined.data(), joined.size());
	return result;
}

[[nodiscard]] QByteArray TonSeed(const QByteArray &entropy) {
	const auto salt = QByteArray("TON default seed");
	auto seed = openssl::Pbkdf2Sha512(
		bytes::make_span(entropy),
		bytes::make_span(salt),
		kPbkdfIterations);
	auto result = QByteArray(
		reinterpret_cast<const char*>(seed.data()),
		int(seed.size()));
	OPENSSL_cleanse(seed.data(), seed.size());
	return result;
}

[[nodiscard]] QByteArray Bip39Seed(const QByteArray &joined) {
	const auto salt = QByteArray("mnemonic");
	auto seed = openssl::Pbkdf2Sha512(
		bytes::make_span(joined),
		bytes::make_span(salt),
		kBip39Iterations);
	auto result = QByteArray(
		reinterpret_cast<const char*>(seed.data()),
		int(seed.size()));
	OPENSSL_cleanse(seed.data(), seed.size());
	return result;
}

[[nodiscard]] int RandomWordIndex() {
	auto random = std::array<bytes::type, 2>();
	bytes::set_random(random);
	const auto value = ((quint32(gsl::to_integer<uchar>(random[0])) << 8)
		| quint32(gsl::to_integer<uchar>(random[1])))
		& kWordIndexMask;
	return int(value);
}

} // namespace

std::vector<QString> GenerateMnemonic() {
	const auto list = Wordlist();
	while (true) {
		auto words = std::vector<QString>();
		words.reserve(kGeneratedWordsCount);
		for (auto i = 0; i != kGeneratedWordsCount; ++i) {
			words.push_back(QString::fromLatin1(list[RandomWordIndex()]));
		}
		if (PhrasePassesBasicSeed(words)) {
			return words;
		}
	}
}

bool IsWordlistWord(const QString &word) {
	return ContainsWord(NormalizeWord(word).toUtf8());
}

bool ValidateTonMnemonic(const std::vector<QString> &words) {
	const auto normalized = NormalizeWords(words);
	if (!AllWordsInList(normalized)) {
		return false;
	}
	return PhrasePassesBasicSeed(normalized);
}

std::optional<KeyPair> MnemonicToKeyPair(
		const std::vector<QString> &words,
		MnemonicType type) {
	const auto normalized = NormalizeWords(words);
	const auto count = int(normalized.size());
	if ((count != 12 && count != 24) || !AllWordsInList(normalized)) {
		return std::nullopt;
	}
	if (type == MnemonicType::Ton) {
		auto joined = JoinedWords(normalized);
		auto entropy = MnemonicEntropy(joined);
		OPENSSL_cleanse(joined.data(), joined.size());
		auto seed = TonSeed(entropy);
		OPENSSL_cleanse(entropy.data(), entropy.size());
		auto seedHead = seed.left(32);
		auto result = KeyPairFromSeed(seedHead);
		OPENSSL_cleanse(seedHead.data(), seedHead.size());
		OPENSSL_cleanse(seed.data(), seed.size());
		if (result.secretKey.isEmpty()) {
			return std::nullopt;
		}
		return result;
	}
	auto joined = JoinedPhrase(normalized)
		.normalized(QString::NormalizationForm_KD)
		.toUtf8();
	auto seed64 = Bip39Seed(joined);
	OPENSSL_cleanse(joined.data(), joined.size());
	auto derived = DeriveEd25519Path(seed64, { 44, 607, 0 });
	OPENSSL_cleanse(seed64.data(), seed64.size());
	auto result = KeyPairFromSeed(derived);
	OPENSSL_cleanse(derived.data(), derived.size());
	if (result.secretKey.isEmpty()) {
		return std::nullopt;
	}
	return result;
}

std::optional<MnemonicType> DetectMnemonicType(
		const std::vector<QString> &words) {
	const auto normalized = NormalizeWords(words);
	const auto count = int(normalized.size());
	if ((count != 12 && count != 24) || !AllWordsInList(normalized)) {
		return std::nullopt;
	}
	return PhrasePassesBasicSeed(normalized)
		? MnemonicType::Ton
		: MnemonicType::Bip39;
}

} // namespace Gram
