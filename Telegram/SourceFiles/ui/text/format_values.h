/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

inline constexpr auto FileStatusSizeReady = 0x7FFFFFF0;
inline constexpr auto FileStatusSizeLoaded = 0x7FFFFFF1;
inline constexpr auto FileStatusSizeFailed = 0x7FFFFFF2;

[[nodiscard]] QString FormatSizeText(qint64 size);
[[nodiscard]] QString FormatDownloadText(qint64 ready, qint64 total);
[[nodiscard]] QString FormatProgressText(qint64 ready, qint64 total);
[[nodiscard]] QString FormatDateTime(QDateTime date, QString format);
[[nodiscard]] QString FormatDurationText(qint64 duration);
[[nodiscard]] QString FormatDurationWords(qint64 duration);
[[nodiscard]] QString FormatDurationAndSizeText(qint64 duration, qint64 size);
[[nodiscard]] QString FormatGifAndSizeText(qint64 size);
[[nodiscard]] QString FormatPlayedText(qint64 played, qint64 duration);

struct CurrencyRule {
	const char *international = "";
	char thousands = ',';
	char decimal = '.';
	bool left = true;
	bool space = false;
	int exponent = 2;
	bool stripDotZero = false;
};

[[nodiscard]] QString FillAmountAndCurrency(
	int64 amount,
	const QString &currency,
	bool forceStripDotZero = false);
[[nodiscard]] CurrencyRule LookupCurrencyRule(const QString &currency);
[[nodiscard]] QString FormatWithSeparators(
	double amount,
	int precision,
	char decimal,
	char thousands);

[[nodiscard]] QString ComposeNameString(
	const QString &filename,
	const QString &songTitle,
	const QString &songPerformer);

} // namespace Ui
