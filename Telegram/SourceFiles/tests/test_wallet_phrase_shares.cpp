/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_phrase_shares.h"

#include "tde2e/tde2e_api.h"

#include <cstdio>
#include <initializer_list>

namespace {

using namespace Wallet::PhraseShares;
using TdE2E::TemporaryKeyPair;

static_assert(
	Q_BYTE_ORDER == Q_LITTLE_ENDIAN,
	"The TL and envelope literals here are little-endian wire bytes.");

constexpr auto kSeedBytes = 215;
constexpr auto kKeyBytes = 32;

struct Check {
	const char *name = nullptr;
	QString (*run)() = nullptr;
};

struct Parties {
	TemporaryKeyPair recipient;
	TemporaryKeyPair sender;
};

struct SealedShare {
	TemporaryKeyPair recipient;
	QByteArray envelope;
};

[[nodiscard]] QByteArray Hex(const char *text) {
	return QByteArray::fromHex(text);
}

[[nodiscard]] QString Text(const QByteArray &bytes) {
	return u"size=%1 hex=%2"_q
		.arg(bytes.size())
		.arg(QString::fromLatin1(bytes.toHex()));
}

[[nodiscard]] QString Text(const std::optional<QByteArray> &bytes) {
	return bytes ? Text(*bytes) : u"nullopt"_q;
}

[[nodiscard]] QString ExpectBytes(
		const QByteArray &got,
		const QByteArray &expected) {
	return (got == expected)
		? QString()
		: u"got %1, expected %2"_q.arg(Text(got), Text(expected));
}

[[nodiscard]] QString ExpectBytes(
		const std::optional<QByteArray> &got,
		const QByteArray &expected) {
	return (got && *got == expected)
		? QString()
		: u"got %1, expected %2"_q.arg(Text(got), Text(expected));
}

[[nodiscard]] QString ExpectNothing(const std::optional<QByteArray> &got) {
	return got
		? u"got a value of size=%1, expected nullopt"_q.arg(got->size())
		: QString();
}

[[nodiscard]] QString ExpectSize(const QByteArray &got, int expected) {
	return (got.size() == expected)
		? QString()
		: u"got size=%1, expected size=%2"_q.arg(got.size()).arg(expected);
}

// A mutated split can rebuild a raw random share, so this never prints bytes.
[[nodiscard]] QString ExpectOpaqueBytes(
		const std::optional<QByteArray> &got,
		const QByteArray &expected) {
	if (!got) {
		return u"got nullopt, expected size=%1"_q.arg(expected.size());
	}
	return (*got == expected)
		? QString()
		: u"got different bytes of size=%1, expected size=%2"_q
			.arg(got->size())
			.arg(expected.size());
}

[[nodiscard]] QString ExpectSeed(
		const std::vector<QString> &words,
		const QByteArray &expected) {
	const auto seed = SeedFromWords(words);
	const auto size = ExpectSize(seed, kSeedBytes);
	return !size.isEmpty() ? size : ExpectBytes(seed, expected);
}

[[nodiscard]] QString FirstFailure(std::initializer_list<QString> results) {
	for (const auto &result : results) {
		if (!result.isEmpty()) {
			return result;
		}
	}
	return QString();
}

[[nodiscard]] QString CheckByteBoundary(
		const QByteArray &fits,
		const QByteArray &overflows) {
	return FirstFailure({
		ExpectSeed({ QString::fromUtf8(fits) }, fits),
		ExpectSize(overflows, 216),
		ExpectBytes(
			SeedFromWords({ QString::fromUtf8(overflows) }),
			QByteArray()),
	});
}

[[nodiscard]] QString CheckExactTwentyFourWords() {
	auto words = std::vector<QString>();
	for (auto i = 1; i != 25; ++i) {
		words.push_back(u"word%1"_q.arg(i, 4, 10, QChar('0')));
	}
	const auto exact = ExpectSeed(
		words,
		"word0001 word0002 word0003 word0004 word0005 word0006 "
		"word0007 word0008 word0009 word0010 word0011 word0012 "
		"word0013 word0014 word0015 word0016 word0017 word0018 "
		"word0019 word0020 word0021 word0022 word0023 word0024");
	words.back() += QChar('x');
	return FirstFailure({
		exact,
		ExpectBytes(SeedFromWords(words), QByteArray()),
	});
}

[[nodiscard]] QString CheckXorPermutations() {
	const auto a = Hex("00ff55");
	const auto b = Hex("0f0faa");
	const auto c = Hex("f0000f");
	const auto expected = Hex("fff0f0");
	return FirstFailure({
		ExpectBytes(CombineShares({ a, c, b }), expected),
		ExpectBytes(CombineShares({ b, a, c }), expected),
		ExpectBytes(CombineShares({ b, c, a }), expected),
		ExpectBytes(CombineShares({ c, a, b }), expected),
		ExpectBytes(CombineShares({ c, b, a }), expected),
	});
}

[[nodiscard]] QString CheckXorInputsUnchanged() {
	const auto shares = std::vector<QByteArray>{
		Hex("00ff55"),
		Hex("0f0faa"),
	};
	return FirstFailure({
		ExpectBytes(CombineShares(shares), Hex("0ff0ff")),
		ExpectBytes(shares[0], Hex("00ff55")),
		ExpectBytes(shares[1], Hex("0f0faa")),
	});
}

[[nodiscard]] QByteArray Binary(int size) {
	auto result = QByteArray(size, '\0');
	for (auto i = 0; i != size; ++i) {
		result[i] = char((i * 37 + 128) & 0xFF);
	}
	return result;
}

[[nodiscard]] QByteArray IndependentXor(
		const std::vector<QByteArray> &shares) {
	auto result = QByteArray(shares.front().size(), '\0');
	for (const auto &share : shares) {
		for (auto i = 0, count = int(share.size()); i != count; ++i) {
			result[i] = char(uchar(result[i]) ^ uchar(share[i]));
		}
	}
	return result;
}

[[nodiscard]] QString CheckSplit(const QByteArray &seed, int count) {
	const auto before = QByteArray(seed.constData(), seed.size());
	const auto shares = SplitSeed(seed, count);
	const auto got = int(shares.size());
	if (got != count) {
		return u"got %1 shares, expected %2"_q.arg(got).arg(count);
	}
	for (const auto &share : shares) {
		const auto size = ExpectSize(share, int(before.size()));
		if (!size.isEmpty()) {
			return size;
		}
	}
	return FirstFailure({
		ExpectOpaqueBytes(IndependentXor(shares), before),
		ExpectOpaqueBytes(CombineShares(shares), before),
		ExpectOpaqueBytes(seed, before),
	});
}

[[nodiscard]] QString CheckSplits(int count) {
	return FirstFailure({
		CheckSplit(Hex("a5"), count),
		CheckSplit(Hex("00ff80017f"), count),
		CheckSplit(QByteArray("ab cd") + QByteArray(210, ' '), count),
		CheckSplit(Binary(215), count),
	});
}

[[nodiscard]] std::optional<Parties> GenerateParties() {
	auto recipient = TemporaryKeyPair::Generate();
	auto sender = TemporaryKeyPair::Generate();
	if (!recipient || !sender) {
		return std::nullopt;
	}
	return Parties{ std::move(*recipient), std::move(*sender) };
}

[[nodiscard]] std::optional<SealedShare> SealShare(const QByteArray &share) {
	auto recipient = TemporaryKeyPair::Generate();
	if (!recipient) {
		return std::nullopt;
	}
	auto envelope = EncryptShare(recipient->publicKey(), share);
	if (!envelope) {
		return std::nullopt;
	}
	return SealedShare{ std::move(*recipient), std::move(*envelope) };
}

[[nodiscard]] std::optional<QByteArray> SealPlain(
		const Parties &parties,
		const QByteArray &plain) {
	const auto cipher = parties.sender.encryptForOne(
		parties.recipient.publicKey(),
		plain);
	if (!cipher) {
		return std::nullopt;
	}
	return parties.sender.publicKey() + *cipher;
}

[[nodiscard]] std::optional<QByteArray> OpenPlain(
		const TemporaryKeyPair &recipient,
		const QByteArray &envelope) {
	return recipient.decryptForOne(
		envelope.left(kKeyBytes),
		envelope.mid(kKeyBytes));
}

[[nodiscard]] QString CheckEncoded(
		const QByteArray &share,
		const QByteArray &plain,
		int envelopeBytes) {
	const auto sealed = SealShare(share);
	if (!sealed) {
		return u"sealing failed"_q;
	}
	const auto size = ExpectSize(sealed->envelope, envelopeBytes);
	return !size.isEmpty()
		? size
		: ExpectBytes(OpenPlain(sealed->recipient, sealed->envelope), plain);
}

[[nodiscard]] QString CheckDecoded(
		const QByteArray &plain,
		const QByteArray &share) {
	const auto parties = GenerateParties();
	if (!parties) {
		return u"key generation failed"_q;
	}
	const auto envelope = SealPlain(*parties, plain);
	if (!envelope) {
		return u"encryptForOne returned nullopt"_q;
	}
	return ExpectBytes(DecryptShare(parties->recipient, *envelope), share);
}

[[nodiscard]] QString CheckRejected(const QByteArray &plain) {
	const auto parties = GenerateParties();
	if (!parties) {
		return u"key generation failed"_q;
	}
	const auto envelope = SealPlain(*parties, plain);
	if (!envelope) {
		return u"encryptForOne returned nullopt"_q;
	}
	const auto &recipient = parties->recipient;
	const auto control = ExpectBytes(OpenPlain(recipient, *envelope), plain);
	return !control.isEmpty()
		? control
		: ExpectNothing(DecryptShare(recipient, *envelope));
}

[[nodiscard]] QString CheckRoundtrip(const QByteArray &share) {
	const auto sealed = SealShare(share);
	if (!sealed) {
		return u"sealing failed"_q;
	}
	return ExpectBytes(
		DecryptShare(sealed->recipient, sealed->envelope),
		share);
}

[[nodiscard]] QString CheckEncryptKeyLengths() {
	const auto share = QByteArray("abc");
	const auto sealed = SealShare(share);
	if (!sealed) {
		return u"sealing failed"_q;
	}
	const auto key = sealed->recipient.publicKey();
	const auto size = ExpectSize(key, 32);
	if (!size.isEmpty()) {
		return size;
	}
	return FirstFailure({
		ExpectNothing(EncryptShare(QByteArray(), share)),
		ExpectNothing(EncryptShare(key.left(31), share)),
		ExpectNothing(EncryptShare(key + '\0', share)),
	});
}

[[nodiscard]] QString CheckEnvelopeLengths() {
	const auto parties = GenerateParties();
	if (!parties) {
		return u"key generation failed"_q;
	}
	const auto key = parties->sender.publicKey();
	const auto size = ExpectSize(key, 32);
	if (!size.isEmpty()) {
		return size;
	}
	const auto envelope = SealPlain(*parties, Hex("08dd908b03616263"));
	if (!envelope) {
		return u"encryptForOne returned nullopt"_q;
	}
	const auto &recipient = parties->recipient;
	const auto control = ExpectBytes(
		DecryptShare(recipient, *envelope),
		QByteArray("abc"));
	if (!control.isEmpty()) {
		return control;
	}
	return FirstFailure({
		ExpectNothing(DecryptShare(recipient, QByteArray())),
		ExpectNothing(DecryptShare(recipient, key.left(31))),
		ExpectNothing(DecryptShare(recipient, key)),
	});
}

[[nodiscard]] QString CheckWrongRecipient() {
	const auto share = QByteArray("abc");
	const auto sealed = SealShare(share);
	if (!sealed) {
		return u"sealing failed"_q;
	}
	const auto control = ExpectBytes(
		DecryptShare(sealed->recipient, sealed->envelope),
		share);
	if (!control.isEmpty()) {
		return control;
	}
	const auto unrelated = TemporaryKeyPair::Generate();
	if (!unrelated) {
		return u"key generation failed"_q;
	}
	return ExpectNothing(DecryptShare(*unrelated, sealed->envelope));
}

[[nodiscard]] QByteArray FlipLowBit(QByteArray bytes, int index) {
	bytes[index] = char(uchar(bytes[index]) ^ 1);
	return bytes;
}

[[nodiscard]] QString CheckCorruptEnvelope() {
	const auto share = QByteArray("abc");
	const auto sealed = SealShare(share);
	if (!sealed) {
		return u"sealing failed"_q;
	}
	const auto &envelope = sealed->envelope;
	const auto size = ExpectSize(envelope, 80);
	if (!size.isEmpty()) {
		return size;
	}
	const auto &recipient = sealed->recipient;
	const auto control = ExpectBytes(DecryptShare(recipient, envelope), share);
	if (!control.isEmpty()) {
		return control;
	}
	return FirstFailure({
		ExpectNothing(DecryptShare(recipient, FlipLowBit(envelope, 0))),
		ExpectNothing(DecryptShare(recipient, FlipLowBit(envelope, 32))),
		ExpectNothing(DecryptShare(recipient, FlipLowBit(envelope, 48))),
		ExpectNothing(DecryptShare(recipient, FlipLowBit(envelope, 79))),
		ExpectNothing(DecryptShare(recipient, envelope.left(79))),
		ExpectNothing(DecryptShare(recipient, envelope.left(33))),
	});
}

[[nodiscard]] std::vector<Check> Checks() {
	return {
		{ "seed.two-words", [] {
			return ExpectSeed(
				{ u"ab"_q, u"cd"_q },
				Hex("6162206364") + QByteArray(210, ' '));
		} },
		{ "seed.empty-words", [] {
			return FirstFailure({
				ExpectSeed({}, QByteArray(215, ' ')),
				ExpectSeed({ QString() }, QByteArray(215, ' ')),
				ExpectSeed({ QString(), QString() }, QByteArray(215, ' ')),
			});
		} },
		{ "seed.ascii-214-pads-one", [] {
			return ExpectSeed(
				{ QString(214, QChar('a')) },
				QByteArray(214, 'a') + ' ');
		} },
		{ "seed.ascii-215-exact", [] {
			return ExpectSeed(
				{ QString(215, QChar('a')) },
				QByteArray(215, 'a'));
		} },
		{ "seed.ascii-216-empty", [] {
			return ExpectBytes(
				SeedFromWords({ QString(216, QChar('a')) }),
				QByteArray());
		} },
		{ "seed.utf8-two-bytes", [] {
			return ExpectSeed(
				{ QString::fromUtf8("\xc3\xa9") },
				Hex("c3a9") + QByteArray(213, ' '));
		} },
		{ "seed.utf8-mixed-words", [] {
			return ExpectSeed(
				{
					QString::fromUtf8("\xc3\xa9"),
					QString::fromUtf8("\xe2\x82\xac"),
				},
				Hex("c3a920e282ac") + QByteArray(209, ' '));
		} },
		{ "seed.utf8-two-byte-boundary", [] {
			return CheckByteBoundary(
				Hex("c3a9").repeated(107) + 'a',
				Hex("c3a9").repeated(108));
		} },
		{ "seed.utf8-three-byte-boundary", [] {
			return CheckByteBoundary(
				Hex("e282ac").repeated(71) + "ab",
				Hex("e282ac").repeated(72));
		} },
		{ "seed.utf8-four-byte-boundary", [] {
			return CheckByteBoundary(
				Hex("f09f9880").repeated(53) + "abc",
				Hex("f09f9880").repeated(54));
		} },
		{ "seed.twelve-words", [] {
			return ExpectSeed(
				{
					u"w01"_q, u"w02"_q, u"w03"_q, u"w04"_q, u"w05"_q, u"w06"_q,
					u"w07"_q, u"w08"_q, u"w09"_q, u"w10"_q, u"w11"_q, u"w12"_q,
				},
				QByteArray("w01 w02 w03 w04 w05 w06 w07 w08 w09 w10 w11 w12")
					+ QByteArray(168, ' '));
		} },
		{ "seed.twenty-four-words", [] {
			return ExpectSeed(
				{
					u"w01"_q, u"w02"_q, u"w03"_q, u"w04"_q, u"w05"_q, u"w06"_q,
					u"w07"_q, u"w08"_q, u"w09"_q, u"w10"_q, u"w11"_q, u"w12"_q,
					u"w13"_q, u"w14"_q, u"w15"_q, u"w16"_q, u"w17"_q, u"w18"_q,
					u"w19"_q, u"w20"_q, u"w21"_q, u"w22"_q, u"w23"_q, u"w24"_q,
				},
				QByteArray("w01 w02 w03 w04 w05 w06 w07 w08 w09 w10 w11 w12 ")
					+ "w13 w14 w15 w16 w17 w18 w19 w20 w21 w22 w23 w24"
					+ QByteArray(120, ' '));
		} },
		{ "seed.twenty-four-words-exact", CheckExactTwentyFourWords },
		{ "xor.literal", [] {
			return ExpectBytes(
				CombineShares({ Hex("00ff55"), Hex("0f0faa"), Hex("f0000f") }),
				Hex("fff0f0"));
		} },
		{ "xor.single-share", [] {
			return ExpectBytes(CombineShares({ Hex("00ff80") }), Hex("00ff80"));
		} },
		{ "xor.empty-input", [] {
			return ExpectNothing(CombineShares({}));
		} },
		{ "xor.empty-first-share", [] {
			return FirstFailure({
				ExpectNothing(CombineShares({ QByteArray() })),
				ExpectNothing(CombineShares({ QByteArray(), Hex("00") })),
				ExpectNothing(CombineShares({ QByteArray(), QByteArray() })),
			});
		} },
		{ "xor.unequal-lengths", [] {
			return FirstFailure({
				ExpectNothing(CombineShares({ Hex("00ff"), Hex("00") })),
				ExpectNothing(CombineShares({ Hex("00"), Hex("00ff") })),
				ExpectNothing(CombineShares({ Hex("00"), QByteArray() })),
				ExpectNothing(CombineShares(
					{ Hex("00ff"), Hex("00ff"), Hex("00") })),
			});
		} },
		{ "xor.nul-and-high-bit", [] {
			return ExpectBytes(
				CombineShares({ Hex("00ff00ff80"), Hex("ff00ff0080") }),
				Hex("ffffffff00"));
		} },
		{ "xor.permutations", CheckXorPermutations },
		{ "xor.equal-shares-cancel", [] {
			return FirstFailure({
				ExpectBytes(
					CombineShares({ Hex("a5f000"), Hex("a5f000") }),
					Hex("000000")),
				ExpectBytes(
					CombineShares(
						{ Hex("00ff55"), Hex("a5f000"), Hex("a5f000") }),
					Hex("00ff55")),
			});
		} },
		{ "xor.inputs-unchanged", CheckXorInputsUnchanged },
		{ "split.two-shares", [] { return CheckSplits(2); } },
		{ "split.three-shares", [] { return CheckSplits(3); } },
		{ "split.five-shares", [] { return CheckSplits(5); } },
		{ "encode.abc", [] {
			return CheckEncoded(QByteArray("abc"), Hex("08dd908b03616263"), 80);
		} },
		{ "encode.empty", [] {
			return CheckEncoded(QByteArray(), Hex("08dd908b00000000"), 80);
		} },
		{ "encode.one-byte", [] {
			return CheckEncoded(Hex("ff"), Hex("08dd908b01ff0000"), 80);
		} },
		{ "encode.two-bytes", [] {
			return CheckEncoded(Hex("00ff"), Hex("08dd908b0200ff00"), 80);
		} },
		{ "encode.binary", [] {
			return CheckEncoded(
				Hex("00ff80010203fe"),
				Hex("08dd908b0700ff80010203fe"),
				80);
		} },
		{ "encode.seed-sized", [] {
			return CheckEncoded(
				Binary(215),
				Hex("08dd908bd7") + Binary(215),
				288);
		} },
		{ "decode.abc", [] {
			return CheckDecoded(Hex("08dd908b03616263"), QByteArray("abc"));
		} },
		{ "decode.empty", [] {
			return CheckDecoded(Hex("08dd908b00000000"), QByteArray());
		} },
		{ "decode.binary", [] {
			return CheckDecoded(
				Hex("08dd908b0700ff80010203fe"),
				Hex("00ff80010203fe"));
		} },
		{ "decode.seed-sized", [] {
			return CheckDecoded(Hex("08dd908bd7") + Binary(215), Binary(215));
		} },
		{ "reject.wrong-constructor", [] {
			return CheckRejected(Hex("09dd908b03616263"));
		} },
		{ "reject.declared-length-too-long", [] {
			return CheckRejected(Hex("08dd908b04616263"));
		} },
		{ "reject.trailing-zero-word", [] {
			return CheckRejected(Hex("08dd908b0361626300000000"));
		} },
		{ "reject.trailing-byte", [] {
			return CheckRejected(Hex("08dd908b0361626300"));
		} },
		{ "reject.constructor-only", [] {
			return CheckRejected(Hex("08dd908b"));
		} },
		{ "reject.empty-plain", [] {
			return CheckRejected(QByteArray());
		} },
		{ "keys.encrypt-rejects-key-lengths", CheckEncryptKeyLengths },
		{ "keys.decrypt-rejects-envelope-lengths", CheckEnvelopeLengths },
		{ "roundtrip.binary", [] {
			return CheckRoundtrip(Hex("00ff80010203fe"));
		} },
		{ "roundtrip.seed-sized", [] {
			return CheckRoundtrip(Binary(215));
		} },
		{ "failure.wrong-recipient", CheckWrongRecipient },
		{ "failure.corrupt-envelope", CheckCorruptEnvelope },
	};
}

} // namespace

int main() {
	const auto checks = Checks();
	auto failed = 0;
	for (const auto &check : checks) {
		const auto failure = check.run();
		if (failure.isEmpty()) {
			std::printf("PASS %s\n", check.name);
		} else {
			std::printf(
				"FAIL %s: %s\n",
				check.name,
				failure.toUtf8().constData());
			++failed;
		}
	}
	const auto total = int(checks.size());
	std::printf("%d of %d checks passed.\n", total - failed, total);
	return failed ? 1 : 0;
}
