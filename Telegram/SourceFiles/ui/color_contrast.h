/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QColor;

namespace Ui {

struct Lab {
	float64 lightness = 0.;
	float64 a = 0.;
	float64 b = 0.;
};

[[nodiscard]] float64 CountContrast(const QColor &a, const QColor &b);
[[nodiscard]] float64 CountPerceivedBrightness(const QColor &color);
[[nodiscard]] bool IsLightBackground(const QColor &background);

[[nodiscard]] Lab ToLab(const QColor &color);
[[nodiscard]] float64 Chroma(const Lab &lab);
[[nodiscard]] float64 Hue(const Lab &lab);
[[nodiscard]] QColor FromLch(float64 lightness, float64 chroma, float64 hue);

} // namespace Ui
