/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/passcode_strength_translit.h"

#include <array>

namespace Ui {
namespace {

constexpr auto kBasicLowerFirst = char32_t(0x0430);
constexpr auto kBasicLower = std::array<const char*, 48>{
	"a", "b", "v", "g", "d", "e", "zh", "z",
	"i", "y", "k", "l", "m", "n", "o", "p",
	"r", "s", "t", "u", "f", "kh", "ts", "ch",
	"sh", "sch", "", "y", "", "e", "yu", "ya",
	"e", "yo", "dj", "g", "ye", "dz", "i", "yi",
	"j", "lj", "nj", "c", "k", "i", "w", "dz",
};

} // namespace

bool IsUppercaseCyrillic(char32_t point) {
	return (point >= 0x0400 && point <= 0x042F) || (point == 0x0490);
}

char32_t LowercaseCyrillic(char32_t point) {
	if (point >= 0x0400 && point <= 0x040F) {
		return point + 0x50;
	} else if (point >= 0x0410 && point <= 0x042F) {
		return point + 0x20;
	} else if (point == 0x0490) {
		return 0x0491;
	}
	return point;
}

const char *TransliterateCyrillic(char32_t lowercase) {
	if (lowercase >= kBasicLowerFirst
		&& lowercase < kBasicLowerFirst + kBasicLower.size()) {
		return kBasicLower[lowercase - kBasicLowerFirst];
	} else if (lowercase == 0x0491) {
		return "g";
	}
	return nullptr;
}

} // namespace Ui
