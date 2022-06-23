/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

inline constexpr auto FileStatusSizeReady = 0xFFFFFFF0LL;
inline constexpr auto FileStatusSizeLoaded = 0xFFFFFFF1LL;
inline constexpr auto FileStatusSizeFailed = 0xFFFFFFF2LL;

[[nodiscard]] QString FormatSizeText(qint64 size);
[[nodiscard]] QString FormatDownloadText(qint64 ready, qint64 total);
[[nodiscard]] QString FormatProgressText(qint64 ready, qint64 total);
[[nodiscard]] QString FormatDateTime(
	QDateTime date,
	QString dateFormat,
	QString timeFormat);
[[nodiscard]] QString FormatDurationText(qint64 duration);
[[nodiscard]] QString FormatDurationWords(qint64 duration);
[[nodiscard]] QString FormatDurationAndSizeText(qint64 duration, qint64 size);
[[nodiscard]] QString FormatGifAndSizeText(qint64 size);
[[nodiscard]] QString FormatPlayedText(qint64 played, qint64 duration);
[[nodiscard]] QString FormatImageSizeText(const QSize &size);
[[nodiscard]] QString FormatPhone(const QString &phone);
[[nodiscard]] QString FormatTTL(float64 ttl);
[[nodiscard]] QString FormatTTLTiny(float64 ttl);
[[nodiscard]] QString FormatMuteFor(float64 sec);
[[nodiscard]] QString FormatMuteForTiny(float64 sec);
[[nodiscard]] QString FormatResetCloudPasswordIn(float64 sec);

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

} // namespace Ui
