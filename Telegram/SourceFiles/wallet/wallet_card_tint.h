/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtGui/QColor>

namespace Wallet {

struct CardTint {
	QColor light;
	QColor dark;

	friend inline bool operator==(const CardTint &, const CardTint &) = default;
};

[[nodiscard]] float64 CardLuminance(QRgb color);

// White text contrast is mandatory, surface contrast only when feasible.
[[nodiscard]] CardTint ComputeCardTint(
	const QColor &first,
	const QColor &second,
	const std::vector<QColor> &surfaces);
[[nodiscard]] QColor CardTintAt(const CardTint &tint, float64 level);
[[nodiscard]] QColor CardStripInk(const CardTint &tint);

} // namespace Wallet
