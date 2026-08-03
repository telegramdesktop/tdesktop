/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/flat_map.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>

namespace Gram {

struct CurrencyRates {
	base::flat_map<QString, float64> values;
};

[[nodiscard]] std::optional<CurrencyRates> ParseCurrencyRates(
	const QByteArray &json);

[[nodiscard]] std::optional<float64> ComputeRate(
	const CurrencyRates &rates,
	const QString &code,
	const QString &base);

} // namespace Gram
