/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_3d_support.h"

#include "ui/gl/gl_detection.h"
#include "ui/power_saving.h"

namespace Ui::Premium {
namespace {

constexpr auto kBezierIterations = 40;
constexpr auto kBezierEpsilon = 0.00001;

} // namespace

float64 CubicBezier(
		float64 x1,
		float64 y1,
		float64 x2,
		float64 y2,
		float64 x) {
	const auto curve = [](float64 t, float64 a, float64 b) {
		return (((1. - 3. * b + 3. * a) * t
			+ (3. * b - 6. * a)) * t
			+ (3. * a)) * t;
	};
	if (x <= 0.) {
		return 0.;
	} else if (x >= 1.) {
		return 1.;
	}
	auto start = 0.;
	auto end = 1.;
	auto t = x;
	for (auto i = 0; i != kBezierIterations; ++i) {
		t = (start + end) / 2.;
		const auto estimate = curve(t, x1, x2);
		if (std::abs(x - estimate) < kBezierEpsilon) {
			break;
		} else if (estimate < x) {
			start = t;
		} else {
			end = t;
		}
	}
	return curve(t, y1, y2);
}

bool Object3dSupported() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	if (PowerSaving::On(PowerSaving::kAnimations)) {
		return false;
	}
	return GL::WidgetsRhiSupported();
#else
	return false;
#endif
}

} // namespace Ui::Premium
