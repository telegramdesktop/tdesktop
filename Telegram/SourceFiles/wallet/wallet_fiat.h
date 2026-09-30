/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "gram/api/gram_api_rates.h"

namespace Main {
class Session;
} // namespace Main

namespace Wallet {

inline constexpr auto kFiatCurrencyDecimals = -1;
inline constexpr auto kFeeFiatDecimals = 5;

struct FiatRate {
	QString currency;
	float64 perGram = 0.;

	[[nodiscard]] bool available() const {
		return perGram > 0.;
	}
	friend inline bool operator==(const FiatRate &, const FiatRate &)
		= default;
};

[[nodiscard]] FiatRate ComputeFiatRate(
	const QString &currency,
	const Gram::CurrencyRates &rates,
	float64 usdPerGram);

[[nodiscard]] QString FormatFiat(
	int64 nanoAmount,
	const FiatRate &rate,
	int decimals = kFiatCurrencyDecimals,
	bool approximate = false);

[[nodiscard]] QString FormatFiatAmount(
	int64 nanoAmount,
	const FiatRate &rate,
	int decimals = kFiatCurrencyDecimals);

struct CurrencyNames {
	QString english;
	QString localized;
};

[[nodiscard]] CurrencyNames LookupCurrencyNames(const QString &currency);
[[nodiscard]] std::vector<QString> TopCurrencies(
	not_null<Main::Session*> session);

[[nodiscard]] int64 FiatMinorUnitNanos(const QString &currency);
[[nodiscard]] QString TinyAmountFraction(
	int64 units,
	int scale,
	int decimals = 2);

[[nodiscard]] rpl::producer<FiatRate> FiatRateValue(
	not_null<Main::Session*> session);

} // namespace Wallet
