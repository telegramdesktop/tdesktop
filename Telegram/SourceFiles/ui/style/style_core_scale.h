/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QSize>

#include <algorithm>
#include <cmath>

namespace style {

inline constexpr auto kScaleAuto = 0;
inline constexpr auto kScaleMin = 75;
inline constexpr auto kScaleDefault = 100;
inline constexpr auto kScaleMax = 300;

[[nodiscard]] int DevicePixelRatio();
void SetDevicePixelRatio(int ratio);

[[nodiscard]] int Scale();
void SetScale(int scale);

[[nodiscard]] inline int CheckScale(int scale) {
	return (scale == kScaleAuto)
		? kScaleAuto
		: std::clamp(scale, kScaleMin, kScaleMax / DevicePixelRatio());
}

template <typename T>
[[nodiscard]] inline T ConvertScale(T value, int scale) {
	return (value < 0.)
		? (-ConvertScale(-value, scale))
		: T(std::round((double(value) * scale / 100.) - 0.01));
}

template <typename T>
[[nodiscard]] inline T ConvertScale(T value) {
	return ConvertScale(value, Scale());
}

[[nodiscard]] inline QSize ConvertScale(QSize size) {
	return QSize(ConvertScale(size.width()), ConvertScale(size.height()));
}

} // namespace style
