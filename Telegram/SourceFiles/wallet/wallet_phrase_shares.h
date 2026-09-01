/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace TdE2E {
class TemporaryKeyPair;
} // namespace TdE2E

namespace Wallet::PhraseShares {

inline constexpr auto kPublicKeySize = 32;
inline constexpr auto kDecryptedKeyPartId = uint32(0x8b90dd08);

// Opens one holder part: the sender's public key, then the ciphertext of a
// TL-serialized mnemonic.decryptedKeyPart whose share string is returned.
[[nodiscard]] std::optional<QByteArray> DecryptShare(
	const TdE2E::TemporaryKeyPair &keys,
	const QByteArray &data);

[[nodiscard]] std::optional<QByteArray> CombineShares(
	const std::vector<QByteArray> &shares);

// The words joined by single spaces, UTF-8: the seed that gets split.
[[nodiscard]] QByteArray SeedFromWords(const std::vector<QString> &words);

// Returns count shares of the seed's length whose XOR is the seed; all but
// the last one are filled from the CSPRNG.
[[nodiscard]] std::vector<QByteArray> SplitSeed(
	const QByteArray &seed,
	int count);

// Seals one holder part: a 32-byte ephemeral public key, then the ciphertext
// of a TL-serialized mnemonic.decryptedKeyPart carrying the share.
[[nodiscard]] std::optional<QByteArray> EncryptShare(
	const QByteArray &holderPublicKey,
	const QByteArray &share);

} // namespace Wallet::PhraseShares
