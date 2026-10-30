/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/glass/glass_tokens.h"

#include "ui/color_contrast.h"

#include <QtGui/QColor>

#include <algorithm>

#include "styles/palette.h"
#include "styles/style_glass.h"

namespace Glass {
namespace {

constexpr auto kMinReadableContrast = 4.5;

// WHY: durations stay C++ constants as the house rules require, and easing
// stays a token for the caller to map onto the shared animation engine.
constexpr auto kMotionFast = crl::time(120);
constexpr auto kMotionNormal = crl::time(180);
constexpr auto kMotionSlow = crl::time(260);

[[nodiscard]] QColor WithOpacity(style::color color, float64 opacity) {
	auto result = color->c;
	result.setAlphaF(result.alphaF() * std::clamp(opacity, 0., 1.));
	return result;
}

[[nodiscard]] const style::GlassMaterial &Raw(Weight weight) {
	const auto light = Ui::IsLightBackground(st::windowBg->c);
	if (weight == Weight::HighContrast) {
		return light ? st::glassHighContrastLight : st::glassHighContrastDark;
	} else if (weight == Weight::Opaque) {
		return light ? st::glassOpaqueLight : st::glassOpaqueDark;
	}
	return light ? st::glassTintedLight : st::glassTintedDark;
}

} // namespace

SurfaceMaterial Material(Weight weight) {
	const auto &raw = Raw(weight);
	return {
		.bg = WithOpacity(raw.bg, raw.bgOpacity),
		.stroke = WithOpacity(raw.stroke, raw.strokeOpacity),
		.highlight = WithOpacity(raw.highlight, raw.highlightOpacity),
		.shadow = WithOpacity(raw.shadow, raw.shadowOpacity),
		.strokeWidth = raw.strokeWidth,
		.highlightWidth = raw.highlightWidth,
		.shadowSpread = raw.shadowSpread,
		.shadowOffsetY = raw.shadowOffsetY,
	};
}

SurfaceMaterial CurrentMaterial() {
	if (Ui::CountContrast(st::windowBg->c, st::windowFg->c)
		< kMinReadableContrast) {
		return Material(Weight::HighContrast);
	}
	return Material(Current().weight);
}

RadiusTokens Radii() {
	return {
		.small = st::glassRadiusSmall,
		.medium = st::glassRadiusMedium,
		.large = st::glassRadiusLarge,
	};
}

SpacingTokens Spacing() {
	return {
		.tiny = st::glassSpacingTiny,
		.small = st::glassSpacingSmall,
		.medium = st::glassSpacingMedium,
		.large = st::glassSpacingLarge,
	};
}

MotionTokens Motion() {
	return {
		.fast = kMotionFast,
		.normal = kMotionNormal,
		.slow = kMotionSlow,
		.easing = Easing::Standard,
	};
}

} // namespace Glass
