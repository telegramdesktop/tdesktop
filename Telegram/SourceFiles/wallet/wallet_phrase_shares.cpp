/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_phrase_shares.h"

#include "mtproto/core_types.h"
#include "tde2e/tde2e_api.h"

namespace Wallet::PhraseShares {
namespace {

[[nodiscard]] std::optional<QByteArray> ParseDecryptedKeyPart(
		const QByteArray &plain) {
	if (plain.isEmpty() || (plain.size() % sizeof(mtpPrime))) {
		return std::nullopt;
	}
	auto buffer = mtpBuffer();
	buffer.resize(plain.size() / sizeof(mtpPrime));
	memcpy(buffer.data(), plain.constData(), plain.size());
	const mtpPrime *from = buffer.constData();
	const auto end = from + buffer.size();
	if (uint32(*from++) != kDecryptedKeyPartId) {
		return std::nullopt;
	}
	auto share = MTPstring();
	if (!share.read(from, end) || from != end) {
		return std::nullopt;
	}
	return share.v;
}

} // namespace

std::optional<QByteArray> DecryptShare(
		const TdE2E::TemporaryKeyPair &keys,
		const QByteArray &data) {
	if (data.size() <= kPublicKeySize) {
		return std::nullopt;
	}
	const auto plain = keys.decryptForOne(
		data.left(kPublicKeySize),
		data.mid(kPublicKeySize));
	if (!plain) {
		return std::nullopt;
	}
	return ParseDecryptedKeyPart(*plain);
}

std::optional<QByteArray> CombineShares(
		const std::vector<QByteArray> &shares) {
	if (shares.empty() || shares.front().isEmpty()) {
		return std::nullopt;
	}
	const auto length = shares.front().size();
	auto result = QByteArray(length, '\0');
	const auto to = result.data();
	for (const auto &share : shares) {
		if (share.size() != length) {
			return std::nullopt;
		}
		const auto from = share.constData();
		for (auto i = 0; i != length; ++i) {
			to[i] ^= from[i];
		}
	}
	return result;
}

} // namespace Wallet::PhraseShares
