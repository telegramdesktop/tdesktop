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

[[nodiscard]] bool Usable(float64 value) {
	return std::isfinite(value) && (value > 0.);
}

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

std::optional<float64> ComputePerGram(
		const CurrencyRates &rates,
		const QString &code,
		float64 usdPerGram) {
	const auto value = LookupRate(rates, code);
	if (!value || !Usable(usdPerGram)) {
		return std::nullopt;
	}
	const auto result = *value * usdPerGram;
	if (!Usable(result)) {
		return std::nullopt;
	}
	return result;
}

std::optional<CurrencyRates> MakeCurrencyRates(
		const std::vector<CurrencyRateEntry> &entries) {
	auto result = CurrencyRates();
	for (const auto &entry : entries) {
		if (Usable(entry.value)) {
			result.values.emplace(entry.code.toUpper(), entry.value);
		}
	}
	if (result.values.empty()) {
		return std::nullopt;
	}
	return result;
}

} // namespace Gram
