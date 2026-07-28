/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "gram/crypto/gram_ed25519.h"

#include <QtCore/QString>

#include <optional>
#include <vector>

namespace Gram {

enum class MnemonicType {
	Ton,
	Bip39,
};

[[nodiscard]] std::vector<QString> GenerateMnemonic();
[[nodiscard]] bool IsWordlistWord(const QString &word);
[[nodiscard]] std::vector<QString> WordlistSuggestions(
	const QString &prefix,
	int limit);
[[nodiscard]] bool ValidateTonMnemonic(const std::vector<QString> &words);
[[nodiscard]] std::optional<KeyPair> MnemonicToKeyPair(
	const std::vector<QString> &words,
	MnemonicType type);
[[nodiscard]] std::optional<MnemonicType> DetectMnemonicType(
	const std::vector<QString> &words);

} // namespace Gram
