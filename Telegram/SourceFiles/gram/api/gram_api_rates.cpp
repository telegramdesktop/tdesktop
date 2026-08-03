/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/api/gram_api_rates.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

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

std::optional<CurrencyRates> ParseCurrencyRates(const QByteArray &json) {
	const auto document = QJsonDocument::fromJson(json);
	if (document.isNull() || !document.isObject()) {
		return std::nullopt;
	}
	const auto entry = document.object().value(u"rates"_q);
	if (!entry.isObject()) {
		return std::nullopt;
	}
	const auto object = entry.toObject();
	auto result = CurrencyRates();
	for (auto i = object.begin(); i != object.end(); ++i) {
		const auto value = i.value();
		if (!value.isString()) {
			continue;
		}
		auto ok = false;
		const auto parsed = value.toString().toDouble(&ok);
		if (ok && std::isfinite(parsed) && parsed > 0.) {
			result.values.emplace(i.key().toUpper(), parsed);
		}
	}
	if (result.values.empty()) {
		return std::nullopt;
	}
	return result;
}

} // namespace Gram
