/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class StarsAmount;

enum lngtag_count : int;

namespace Lang {

inline constexpr auto kTextCommand = 0x10;
inline constexpr auto kTextCommandLangTag = 0x20;
constexpr auto kTagReplacementSize = 4;

[[nodiscard]] int FindTagReplacementPosition(
	const QString &original,
	ushort tag);

struct ShortenedCount {
	int64 number = 0;
	QString string;
	bool shortened = false;
};
[[nodiscard]] ShortenedCount FormatCountToShort(int64 number);
[[nodiscard]] QString FormatCountDecimal(int64 number);
[[nodiscard]] QString FormatExactCountDecimal(float64 number);
[[nodiscard]] ShortenedCount FormatStarsAmountToShort(StarsAmount amount);
[[nodiscard]] QString FormatStarsAmountDecimal(StarsAmount amount);
[[nodiscard]] QString FormatStarsAmountRounded(StarsAmount amount);

struct PluralResult {
	int keyShift = 0;
	QString replacement;
};
PluralResult Plural(
	ushort keyBase,
	float64 value,
	lngtag_count type);
void UpdatePluralRules(const QString &languageId);

template <typename ResultString>
struct StartReplacements;

template <>
struct StartReplacements<QString> {
	static inline QString Call(QString &&langString) {
		return std::move(langString);
	}
};

template <typename ResultString>
struct ReplaceTag;

template <>
struct ReplaceTag<QString> {
	static inline QString Call(QString &&original, ushort tag, const QString &replacement) {
		auto replacementPosition = FindTagReplacementPosition(original, tag);
		if (replacementPosition < 0) {
			return std::move(original);
		}
		return Replace(std::move(original), replacement, replacementPosition);
	}
	static QString Replace(QString &&original, const QString &replacement, int start);

};

} // namespace Lang
