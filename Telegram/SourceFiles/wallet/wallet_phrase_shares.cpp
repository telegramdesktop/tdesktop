/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_phrase_shares.h"

#include "base/random.h"
#include "mtproto/core_types.h"
#include "tde2e/tde2e_api.h"

namespace Wallet::PhraseShares {
namespace {

[[nodiscard]] std::optional<QByteArray> ParseDecryptedKeyPart(
		const QByteArray &plain) {
	if (plain.isEmpty() || (plain.size() % sizeof(mtpPrime))) {
		LOG(("Wallet Error: decrypted share invalid TL size=%1.")
			.arg(plain.size()));
		return std::nullopt;
	}
	auto buffer = mtpBuffer();
	buffer.resize(plain.size() / sizeof(mtpPrime));
	memcpy(buffer.data(), plain.constData(), plain.size());
	const mtpPrime *from = buffer.constData();
	const auto end = from + buffer.size();
	if (uint32(*from++) != kDecryptedKeyPartId) {
		LOG(("Wallet Error: decrypted share unexpected TL constructor."));
		return std::nullopt;
	}
	auto share = MTPstring();
	if (!share.read(from, end) || from != end) {
		LOG(("Wallet Error: decrypted share invalid TL string or trailing "
			"data; plain_bytes=%1.").arg(plain.size()));
		return std::nullopt;
	}
	return share.v;
}

[[nodiscard]] QByteArray SerializeDecryptedKeyPart(const QByteArray &share) {
	auto buffer = mtpBuffer();
	buffer.push_back(mtpPrime(kDecryptedKeyPartId));
	MTP_bytes(share).write(buffer);
	return QByteArray(
		reinterpret_cast<const char*>(buffer.constData()),
		buffer.size() * sizeof(mtpPrime));
}

} // namespace

std::optional<QByteArray> DecryptShare(
		const TdE2E::TemporaryKeyPair &keys,
		const QByteArray &data) {
	if (data.size() <= kPublicKeySize) {
		LOG(("Wallet Error: encrypted share too short bytes=%1 "
			"public_key_bytes=%2.").arg(data.size()).arg(kPublicKeySize));
		return std::nullopt;
	}
	const auto plain = keys.decryptForOne(
		data.left(kPublicKeySize),
		data.mid(kPublicKeySize));
	if (!plain) {
		LOG(("Wallet Error: share decryption failed encrypted_bytes=%1.")
			.arg(data.size()));
		return std::nullopt;
	}
	return ParseDecryptedKeyPart(*plain);
}

std::optional<QByteArray> CombineShares(
		const std::vector<QByteArray> &shares) {
	if (shares.empty() || shares.front().isEmpty()) {
		LOG(("Wallet Error: share combine empty input count=%1 "
			"first_bytes=%2."
			).arg(shares.size()
			).arg(shares.empty() ? 0 : shares.front().size()));
		return std::nullopt;
	}
	const auto length = shares.front().size();
	auto result = QByteArray(length, '\0');
	const auto to = result.data();
	for (const auto &share : shares) {
		if (share.size() != length) {
			LOG(("Wallet Error: share combine length mismatch "
				"expected_bytes=%1 actual_bytes=%2 count=%3."
				).arg(length).arg(share.size()).arg(shares.size()));
			return std::nullopt;
		}
		const auto from = share.constData();
		for (auto i = 0; i != length; ++i) {
			to[i] ^= from[i];
		}
	}
	return result;
}

QByteArray SeedFromWords(const std::vector<QString> &words) {
	const auto joined = QStringList(words.begin(), words.end())
		.join(QChar(' '))
		.toUtf8();
	if (joined.size() > kSeedLength) {
		return QByteArray();
	}
	return joined.leftJustified(kSeedLength, ' ');
}

std::vector<QByteArray> SplitSeed(const QByteArray &seed, int count) {
	Expects(count >= 2 && !seed.isEmpty());

	const auto length = seed.size();
	auto result = std::vector<QByteArray>(count, QByteArray(length, '\0'));
	auto last = seed;
	const auto to = last.data();
	for (auto i = 0; i != count - 1; ++i) {
		auto &share = result[i];
		base::RandomFill(bytes::make_detached_span(share));
		const auto from = share.constData();
		for (auto j = 0; j != length; ++j) {
			to[j] ^= from[j];
		}
	}
	result.back() = std::move(last);
	return result;
}

std::optional<QByteArray> EncryptShare(
		const QByteArray &holderPublicKey,
		const QByteArray &share) {
	if (holderPublicKey.size() != kPublicKeySize) {
		return std::nullopt;
	}
	const auto pair = TdE2E::TemporaryKeyPair::Generate();
	if (!pair) {
		return std::nullopt;
	}
	const auto cipher = pair->encryptForOne(
		holderPublicKey,
		SerializeDecryptedKeyPart(share));
	if (!cipher) {
		return std::nullopt;
	}
	return pair->publicKey() + *cipher;
}

} // namespace Wallet::PhraseShares
