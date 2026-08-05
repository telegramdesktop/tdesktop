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
	const Gram::CurrencyRates &rates);

[[nodiscard]] QString FormatFiat(
	int64 nanoAmount,
	const FiatRate &rate,
	int decimals = kFiatCurrencyDecimals,
	bool approximate = false);

[[nodiscard]] int64 FiatMinorUnitNanos(const QString &currency);

[[nodiscard]] rpl::producer<FiatRate> FiatRateValue(
	not_null<Main::Session*> session);

} // namespace Wallet
