/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/flat_map.h"

#include <QtCore/QString>

#include <optional>
#include <vector>

namespace Gram {

struct CurrencyRates {
	base::flat_map<QString, float64> values;
};

struct CurrencyRateEntry {
	QString code;
	float64 value = 0.;
};

[[nodiscard]] std::optional<CurrencyRates> MakeCurrencyRates(
	const std::vector<CurrencyRateEntry> &entries);

[[nodiscard]] std::optional<float64> ComputePerGram(
	const CurrencyRates &rates,
	const QString &code,
	float64 usdPerGram);

} // namespace Gram
