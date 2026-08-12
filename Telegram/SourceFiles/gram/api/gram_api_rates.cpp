/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/api/gram_api_rates.h"

#include <cmath>

namespace Gram {
namespace {

[[nodiscard]] std::optional<float64> LookupRate(
		const CurrencyRates &rates,
		const QString &code) {
	const auto i = rates.values.find(code.toUpper());
	if (i == rates.values.end()) {
		return std::nullopt;
	}
	return i->second;
}

} // namespace

std::optional<float64> ComputeRate(
		const CurrencyRates &rates,
		const QString &code,
		const QString &base) {
	const auto value = LookupRate(rates, code);
	const auto divider = LookupRate(rates, base);
	if (!value || !divider || !(*divider > 0.)) {
		return std::nullopt;
	}
	const auto result = *value / *divider;
	if (!std::isfinite(result)) {
		return std::nullopt;
	}
	return result;
}

std::optional<CurrencyRates> MakeCurrencyRates(
		const std::vector<CurrencyRateEntry> &entries) {
	auto result = CurrencyRates();
	for (const auto &entry : entries) {
		if (std::isfinite(entry.value) && entry.value > 0.) {
			result.values.emplace(entry.code.toUpper(), entry.value);
		}
	}
	if (result.values.empty()) {
		return std::nullopt;
	}
	return result;
}

} // namespace Gram
