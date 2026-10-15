/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/color_contrast.h"

#include <QtCore/QtMath>

namespace Ui {
namespace {

constexpr auto kGamutIterations = 30;
constexpr auto kGamutEpsilon = 1e-4;

[[nodiscard]] float64 ToLinear(int channel) {
	const auto value = channel / 255.;
	return (value <= 0.04045)
		? (value / 12.92)
		: std::pow((value + 0.055) / 1.055, 2.4);
}

[[nodiscard]] float64 FromLinear(float64 value) {
	value = std::clamp(value, 0., 1.);
	return (value <= 0.0031308)
		? (12.92 * value)
		: (1.055 * std::pow(value, 1. / 2.4) - 0.055);
}

[[nodiscard]] std::array<float64, 3> LinearFromLch(
		float64 lightness,
		float64 chroma,
		float64 hue) {
	const auto radians = hue * M_PI / 180.;
	const auto a = chroma * std::cos(radians);
	const auto b = chroma * std::sin(radians);
	const auto l = lightness + 0.3963377774 * a + 0.2158037573 * b;
	const auto m = lightness - 0.1055613458 * a - 0.0638541728 * b;
	const auto s = lightness - 0.0894841775 * a - 1.2914855480 * b;
	const auto l3 = l * l * l;
	const auto m3 = m * m * m;
	const auto s3 = s * s * s;
	return {
		4.0767416621 * l3 - 3.3077115913 * m3 + 0.2309699292 * s3,
		-1.2684380046 * l3 + 2.6097574011 * m3 - 0.3413193965 * s3,
		-0.0041960863 * l3 - 0.7034186147 * m3 + 1.7076147010 * s3,
	};
}

[[nodiscard]] bool InGamut(float64 lightness, float64 chroma, float64 hue) {
	for (const auto value : LinearFromLch(lightness, chroma, hue)) {
		if (value < -kGamutEpsilon || value > 1. + kGamutEpsilon) {
			return false;
		}
	}
	return true;
}

} // namespace

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

Lab ToLab(const QColor &color) {
	const auto r = ToLinear(color.red());
	const auto g = ToLinear(color.green());
	const auto b = ToLinear(color.blue());
	const auto l = std::cbrt(
		0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
	const auto m = std::cbrt(
		0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
	const auto s = std::cbrt(
		0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
	return {
		.lightness = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
		.a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
		.b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
	};
}

float64 Chroma(const Lab &lab) {
	return std::hypot(lab.a, lab.b);
}

float64 Hue(const Lab &lab) {
	const auto result = std::fmod(
		std::atan2(lab.b, lab.a) * 180. / M_PI,
		360.);
	return (result < 0.) ? (result + 360.) : result;
}

QColor FromLch(float64 lightness, float64 chroma, float64 hue) {
	if (!InGamut(lightness, chroma, hue)) {
		auto low = 0.;
		auto high = chroma;
		for (auto i = 0; i != kGamutIterations; ++i) {
			const auto middle = (low + high) / 2.;
			if (InGamut(lightness, middle, hue)) {
				low = middle;
			} else {
				high = middle;
			}
		}
		chroma = low;
	}
	const auto rgb = LinearFromLch(lightness, chroma, hue);
	const auto channel = [](float64 value) {
		return int(std::round(FromLinear(value) * 255.));
	};
	return QColor(channel(rgb[0]), channel(rgb[1]), channel(rgb[2]));
}

} // namespace Ui
