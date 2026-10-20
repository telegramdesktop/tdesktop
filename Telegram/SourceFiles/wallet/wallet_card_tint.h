/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtGui/QColor>
#include <QtGui/QImage>

namespace Wallet {

struct CardTint {
	QColor light;
	QColor dark;

	friend inline bool operator==(const CardTint &, const CardTint &) = default;
};

struct CardMarkTint {
	QColor hue;
	float64 amount = 0.;

	friend inline bool operator==(
		const CardMarkTint &,
		const CardMarkTint &) = default;
};

class CardMarkTinter final {
public:
	[[nodiscard]] QImage apply(const QImage &frame, const CardMarkTint &tint);

private:
	QImage _result;
	CardMarkTint _tint;
	qint64 _key = 0;

};

[[nodiscard]] float64 CardLuminance(QRgb color);

// White text contrast is mandatory, surface contrast only when feasible.
[[nodiscard]] CardTint ComputeCardTint(
	const QColor &first,
	const QColor &second,
	const std::vector<QColor> &surfaces);
[[nodiscard]] QColor CardTintAt(const CardTint &tint, float64 level);
[[nodiscard]] QColor CardStripInk(const CardTint &tint);
[[nodiscard]] QColor CardInkSurround(const QColor &card, const QColor &ink);
[[nodiscard]] CardMarkTint MixCardMarkTint(
	const CardMarkTint &from,
	const CardMarkTint &to,
	float64 progress);

} // namespace Wallet
