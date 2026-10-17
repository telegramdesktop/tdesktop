/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <cstdint>

namespace Ui {

// Cyrillic case fold and Cyrillic-to-Latin transliteration shared by the
// passcode strength estimator and the zxcvbn_translit build tool, so the
// candidate and the dictionary are normalised by the same rows. Covers
// U+0400-U+045F and U+0490/U+0491; returns nullptr for any other code
// point and "" for letters that transliterate to nothing.
[[nodiscard]] bool IsUppercaseCyrillic(char32_t point);
[[nodiscard]] char32_t LowercaseCyrillic(char32_t point);
[[nodiscard]] const char *TransliterateCyrillic(char32_t lowercase);

} // namespace Ui
