/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/color_contrast.h"

namespace Ui {

// https://stackoverflow.com/a/9733420
float64 CountContrast(const QColor &a, const QColor &b) {
	const auto luminance = [](const QColor &c) {
		const auto map = [](double value) {
			return (value <= 0.03928)
				? (value / 12.92)
				: std::pow((value + 0.055) / 1.055, 2.4);
		};
		return map(c.redF()) * 0.2126
			+ map(c.greenF()) * 0.7152
			+ map(c.blueF()) * 0.0722;
	};
	const auto luminance1 = luminance(a);
	const auto luminance2 = luminance(b);
	const auto brightest = std::max(luminance1, luminance2);
	const auto darkest = std::min(luminance1, luminance2);
	return (brightest + 0.05) / (darkest + 0.05);
}

float64 CountPerceivedBrightness(const QColor &color) {
	return (color.red() * 0.2126
		+ color.green() * 0.7152
		+ color.blue() * 0.0722) / 255.;
}

bool IsLightBackground(const QColor &background) {
	constexpr auto kLightBrightness = 0.72;
	return CountPerceivedBrightness(background) > kLightBrightness;
}

} // namespace Ui
