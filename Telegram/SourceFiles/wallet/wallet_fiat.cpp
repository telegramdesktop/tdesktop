/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_fiat.h"

#include "main/main_session.h"
#include "ui/controls/ton_common.h"
#include "ui/text/format_values.h"
#include "wallet/wallet_rates.h"
#include "wallet/wallet_session.h"

#include <cmath>

namespace Wallet {
namespace {

[[nodiscard]] QString FiatDigits(
		int64 nanoAmount,
		const FiatRate &rate,
		int decimals,
		const Ui::CurrencyRule &rule) {
	if (!rate.available()) {
		return QString(QChar(0x2026));
	}
	const auto value = (double(nanoAmount) / Ui::kNanosInOne) * rate.perGram;
	return Ui::FormatWithSeparators(
		value,
		(decimals == kFiatCurrencyDecimals) ? rule.exponent : decimals,
		rule.decimal,
		rule.thousands);
}

} // namespace

FiatRate ComputeFiatRate(
		const QString &currency,
		const Gram::CurrencyRates &rates) {
	const auto perGram = Gram::ComputeRate(rates, currency, u"TON"_q);
	return { currency, perGram.value_or(0.) };
}

QString FormatFiat(
		int64 nanoAmount,
		const FiatRate &rate,
		int decimals,
		bool approximate) {
	const auto rule = Ui::LookupCurrencyRule(rate.currency);
	const auto name = Ui::CurrencyName(rate.currency);
	const auto digits = FiatDigits(nanoAmount, rate, decimals, rule);
	auto result = approximate ? QString(QChar('~')) : QString();
	if (rule.left) {
		result += name;
		if (rule.space) {
			result += QChar(' ');
		}
		result += digits;
	} else {
		result += digits;
		if (rule.space) {
			result += QChar(' ');
		}
		result += name;
	}
	return result;
}

QString FormatFiatAmount(int64 nanoAmount, const FiatRate &rate) {
	return FiatDigits(
		nanoAmount,
		rate,
		kFiatCurrencyDecimals,
		Ui::LookupCurrencyRule(rate.currency));
}

int64 FiatMinorUnitNanos(const QString &currency) {
	const auto exponent = Ui::LookupCurrencyRule(currency).exponent;
	const auto units = std::max(
		int64(std::llround(std::pow(10., exponent))),
		int64(1));
	return std::max(Ui::kNanosInOne / units, int64(1));
}

rpl::producer<FiatRate> FiatRateValue(not_null<Main::Session*> session) {
	return session->wallet().rates().value();
}

} // namespace Wallet
