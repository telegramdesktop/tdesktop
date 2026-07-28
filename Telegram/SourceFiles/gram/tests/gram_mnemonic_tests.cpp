/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include "gram/crypto/gram_ed25519.h"
#include "gram/crypto/gram_mnemonic.h"
#include "gram/crypto/gram_wordlist.h"

#include <QtCore/QStringList>

#include <cstring>
#include <utility>

namespace Gram::Tests {
namespace {

[[nodiscard]] std::vector<QString> Words(const QString &phrase) {
	const auto list = phrase.split(QChar(u' '));
	return std::vector<QString>(list.begin(), list.end());
}

[[nodiscard]] std::vector<QString> RepeatWord(int count, const QString &word) {
	return std::vector<QString>(count, word);
}

[[nodiscard]] QString AbandonAboutPhrase(int count) {
	auto result = QString();
	for (auto i = 0; i != count - 1; ++i) {
		result += u"abandon "_q;
	}
	result += u"about"_q;
	return result;
}

[[nodiscard]] QString Vector0Phrase() {
	return u"hospital stove relief fringe tongue always charge angry urge sentence again match nerve inquiry senior coconut label tumble carry category beauty bean road solution"_q;
}

[[nodiscard]] QString CheckMnemonicKey(
		const QString &phrase,
		MnemonicType type,
		const QByteArray &secretHex) {
	const auto keypair = MnemonicToKeyPair(Words(phrase), type);
	if (!keypair) {
		return u"MnemonicToKeyPair returned nullopt"_q;
	}
	const auto failure = CompareHex(keypair->secretKey, secretHex);
	if (!failure.isEmpty()) {
		return u"secretKey: "_q + failure;
	}
	return CompareHex(keypair->publicKey, secretHex.right(64));
}

} // namespace

std::vector<Check> MnemonicChecks() {
	return {
		{ u"wordlist_shape"_q, [] {
			const auto words = Wordlist();
			if (int(words.size()) != 2048) {
				return u"wordlist size %1, expected 2048"_q.arg(
					int(words.size()));
			}
			if (std::strcmp(words[0], "abandon") != 0) {
				return u"first word is not abandon"_q;
			}
			if (std::strcmp(words[int(words.size()) - 1], "zoo") != 0) {
				return u"last word is not zoo"_q;
			}
			for (auto i = 1; i != int(words.size()); ++i) {
				if (std::strcmp(words[i - 1], words[i]) >= 0) {
					return u"wordlist is not strictly ascending at index %1"_q
						.arg(i);
				}
			}
			const auto present = {
				u"abandon"_q,
				u"about"_q,
				u"hospital"_q,
				u"zoo"_q,
				u" About "_q,
			};
			for (const auto &word : present) {
				if (!IsWordlistWord(word)) {
					return u"IsWordlistWord false for a listed word"_q;
				}
			}
			if (IsWordlistWord(u"qwerty"_q)) {
				return u"IsWordlistWord true for qwerty"_q;
			}
			if (IsWordlistWord(u""_q)) {
				return u"IsWordlistWord true for the empty string"_q;
			}
			return QString();
		} },
		{ u"wordlist_suggestions"_q, [] {
			const auto compare = [](
					const std::vector<QString> &got,
					const QStringList &expected) {
				if (int(got.size()) != expected.size()) {
					return false;
				}
				for (auto i = 0; i != int(got.size()); ++i) {
					if (got[i] != expected[i]) {
						return false;
					}
				}
				return true;
			};
			if (!compare(
					WordlistSuggestions(u"wor"_q, 3),
					{ u"word"_q, u"work"_q, u"world"_q })) {
				return u"wor limit 3 mismatch"_q;
			}
			if (!compare(
					WordlistSuggestions(u"wor"_q, 10),
					{ u"word"_q, u"work"_q, u"world"_q, u"worry"_q, u"worth"_q })) {
				return u"wor limit 10 mismatch"_q;
			}
			if (!compare(
					WordlistSuggestions(u"a"_q, 3),
					{ u"abandon"_q, u"ability"_q, u"able"_q })) {
				return u"leading-boundary prefix a mismatch"_q;
			}
			if (!compare(WordlistSuggestions(u"zoo"_q, 3), { u"zoo"_q })) {
				return u"single-match zoo mismatch"_q;
			}
			if (!compare(
					WordlistSuggestions(u"sea"_q, 3),
					{ u"sea"_q, u"search"_q, u"season"_q })) {
				return u"exact-word-with-completions sea mismatch"_q;
			}
			if (!WordlistSuggestions(u"qq"_q, 3).empty()) {
				return u"qq returned suggestions"_q;
			}
			if (!compare(
					WordlistSuggestions(u" WoR "_q, 3),
					{ u"word"_q, u"work"_q, u"world"_q })) {
				return u"normalization mismatch"_q;
			}
			if (!WordlistSuggestions(u""_q, 3).empty()) {
				return u"empty prefix returned suggestions"_q;
			}
			if (!WordlistSuggestions(u"wor"_q, 0).empty()) {
				return u"limit 0 returned suggestions"_q;
			}
			return QString();
		} },
		{ u"ton_mnemonic_vector_0_golden"_q, [] {
			return CheckMnemonicKey(
				Vector0Phrase(),
				MnemonicType::Ton,
				"9d659a6c2234db7f6e4f977e6e8653b9f5946d557163f31034011375d8f3f97df6c450a16bb1c514e22f1977e390a3025599aa1e7b00068a6aacf2119484c1bd");
		} },
		{ u"ton_mnemonic_vector_1"_q, [] {
			return CheckMnemonicKey(
				u"dose ice enrich trigger test dove century still betray gas diet dune use other base gym mad law immense village world example praise game"_q,
				MnemonicType::Ton,
				"119dcf2840a3d56521d260b2f125eedc0d4f3795b9e627269a4b5a6dca8257bdc04ad1885c127fe863abb00752fa844e6439bb04f264d70de7cea580b32637ab");
		} },
		{ u"ton_mnemonic_vector_2"_q, [] {
			return CheckMnemonicKey(
				u"hobby coil wisdom mechanic fossil pretty enough attract since choice exhaust hazard kit oven damp flip hawk tribe spice glare step hammer apple number"_q,
				MnemonicType::Ton,
				"764c63ecdc92b331caf3c5a81c483da8444d4ac87d87af9e3cd36ae207d94e5199ac861b19db16bc0f01adfc6897f4760dfc44f9415284c78689d4fcc28b94f7");
		} },
		{ u"ton_mnemonic_vector_3"_q, [] {
			return CheckMnemonicKey(
				u"now wide tag purity diamond coin unit rack device replace cheap deposit mention fence elite elder city measure reward lion chef promote depart connect"_q,
				MnemonicType::Ton,
				"2a8a63e0467f1f4148e0be0cc13e922d726f0b1c29272d6743eb83cf5549128f313abf58635fd310310d1debd54f4fe1fd63631ced044ba0af96b67b85eed31b");
		} },
		{ u"ton_mnemonic_vector_4"_q, [] {
			return CheckMnemonicKey(
				u"clinic toward wedding category tip spin purity absent army gun brain happy move company that cheap tank way shoe awkward pole protect wear crystal"_q,
				MnemonicType::Ton,
				"e5e78a8e1e509da180bc5aeb8af1a37d4311c5110402842925760a4035119362b1f8a0b9b4c2353ddfad8937ed396fb7670e88e8b72128b15006839a2a86be47");
		} },
		{ u"kit_abandon_ton"_q, [] {
			return CheckMnemonicKey(
				AbandonAboutPhrase(24),
				MnemonicType::Ton,
				"0e5d1af976fc42954764144d47f6a4d38e1b753a22fda8173be659edddfd00ed2fea08c108702b69531fc793953466e690f2076c64c0393948aae4177dd2a9f5");
		} },
		{ u"kit_abandon_bip39"_q, [] {
			return CheckMnemonicKey(
				AbandonAboutPhrase(24),
				MnemonicType::Bip39,
				"098cb1f3427de537ea4a4ef682e54d8cdd69d0f51ddb37f7f2ae48ffa1445ea1d2ce1d2618ee59ecdcc2b55b9f05b2eea8729e98a49306b1a04c6510074ecc9f");
		} },
		{ u"ton_bip39_keys_differ"_q, [] {
			const auto phrase = AbandonAboutPhrase(24);
			const auto ton = MnemonicToKeyPair(
				Words(phrase),
				MnemonicType::Ton);
			const auto bip39 = MnemonicToKeyPair(
				Words(phrase),
				MnemonicType::Bip39);
			if (!ton || !bip39) {
				return u"MnemonicToKeyPair returned nullopt"_q;
			}
			if (ton->publicKey == bip39->publicKey) {
				return u"ton and bip39 public keys are equal"_q;
			}
			return QString();
		} },
		{ u"bip39_12_words"_q, [] {
			const auto keypair = MnemonicToKeyPair(
				Words(AbandonAboutPhrase(12)),
				MnemonicType::Bip39);
			if (!keypair) {
				return u"MnemonicToKeyPair returned nullopt for 12 bip39 words"_q;
			}
			if (keypair->secretKey.size() != 64) {
				return u"secretKey size %1, expected 64"_q.arg(
					keypair->secretKey.size());
			}
			if (keypair->publicKey.size() != 32) {
				return u"publicKey size %1, expected 32"_q.arg(
					keypair->publicKey.size());
			}
			const auto message = QByteArray("bip39 round-trip");
			const auto signature = Sign(message, keypair->secretKey);
			if (!Verify(message, signature, keypair->publicKey)) {
				return u"12-word bip39 signature failed to verify"_q;
			}
			return QString();
		} },
		{ u"validate_ton_mnemonic"_q, [] {
			if (ValidateTonMnemonic({ u"a"_q })) {
				return u"validate accepted a single non-wordlist word"_q;
			}
			if (!ValidateTonMnemonic(Words(Vector0Phrase()))) {
				return u"validate rejected the vector 0 phrase"_q;
			}
			if (ValidateTonMnemonic(Words(AbandonAboutPhrase(24)))) {
				return u"validate accepted the abandon-about phrase"_q;
			}
			return QString();
		} },
		{ u"mnemonic_word_count_rejected"_q, [] {
			const auto counts = { 0, 5, 15, 23, 25 };
			const auto types = { MnemonicType::Ton, MnemonicType::Bip39 };
			for (const auto type : types) {
				for (const auto count : counts) {
					const auto words = (count == 0)
						? std::vector<QString>()
						: RepeatWord(count, u"abandon"_q);
					if (MnemonicToKeyPair(words, type)) {
						return u"accepted a %1-word mnemonic"_q.arg(count);
					}
				}
			}
			return QString();
		} },
		{ u"mnemonic_unknown_word_rejected"_q, [] {
			auto words = Words(Vector0Phrase());
			words[0] = u"qwerty"_q;
			if (MnemonicToKeyPair(words, MnemonicType::Ton)) {
				return u"ton accepted a non-wordlist word"_q;
			}
			if (MnemonicToKeyPair(words, MnemonicType::Bip39)) {
				return u"bip39 accepted a non-wordlist word"_q;
			}
			return QString();
		} },
		{ u"detect_mnemonic_type"_q, [] {
			if (DetectMnemonicType(Words(AbandonAboutPhrase(24)))
					!= MnemonicType::Bip39) {
				return u"abandon-about not detected as Bip39"_q;
			}
			if (DetectMnemonicType(Words(Vector0Phrase()))
					!= MnemonicType::Ton) {
				return u"vector 0 not detected as Ton"_q;
			}
			const auto swaps = {
				std::pair<int, int>(0, 1),
				std::pair<int, int>(2, 3),
				std::pair<int, int>(4, 5),
				std::pair<int, int>(6, 7),
			};
			auto probed = false;
			for (const auto &swap : swaps) {
				auto words = Words(Vector0Phrase());
				std::swap(words[swap.first], words[swap.second]);
				if (ValidateTonMnemonic(words)) {
					continue;
				}
				probed = true;
				if (DetectMnemonicType(words) != MnemonicType::Bip39) {
					return u"a non-basic-seed wordlist phrase was not detected as Bip39"_q;
				}
				break;
			}
			if (!probed) {
				return u"no permutation produced a Bip39 probe"_q;
			}
			auto unknown = Words(Vector0Phrase());
			unknown[0] = u"qwerty"_q;
			if (DetectMnemonicType(unknown)) {
				return u"detect accepted a non-wordlist word"_q;
			}
			if (DetectMnemonicType(RepeatWord(23, u"abandon"_q))) {
				return u"detect accepted a 23-word phrase"_q;
			}
			return QString();
		} },
		{ u"generate_validate_derive_roundtrip"_q, [] {
			const auto words = GenerateMnemonic();
			if (int(words.size()) != 24) {
				return u"generated %1 words, expected 24"_q.arg(
					int(words.size()));
			}
			for (const auto &word : words) {
				if (!IsWordlistWord(word)) {
					return u"a generated word is not in the wordlist"_q;
				}
			}
			if (!ValidateTonMnemonic(words)) {
				return u"the generated mnemonic failed validation"_q;
			}
			const auto keypair = MnemonicToKeyPair(words, MnemonicType::Ton);
			if (!keypair) {
				return u"the generated mnemonic did not derive a keypair"_q;
			}
			const auto message = QByteArray("generated round-trip");
			const auto signature = Sign(message, keypair->secretKey);
			if (!Verify(message, signature, keypair->publicKey)) {
				return u"the generated keypair signature failed to verify"_q;
			}
			return QString();
		} },
	};
}

} // namespace Gram::Tests
