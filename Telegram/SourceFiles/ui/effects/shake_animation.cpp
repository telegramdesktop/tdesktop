/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/shake_animation.h"

#include "styles/style_basic.h"

namespace Ui {

Fn<void(float64)> DefaultShakeCallback(Fn<void(int)> applyShift) {
	constexpr auto kShiftProgress = 6;
	constexpr auto kSegmentsCount = 5;
	return [=, applyShift = std::move(applyShift)](float64 value) {
		const auto fullProgress = value * kShiftProgress;
		const auto segment = std::clamp(
			int(std::floor(fullProgress)),
			0,
			kSegmentsCount);
		const auto part = fullProgress - segment;
		const auto from = (segment == 0)
			? 0.
			: (segment == 1 || segment == 3 || segment == 5)
			? 1.
			: -1.;
		const auto to = (segment == 0 || segment == 2 || segment == 4)
			? 1.
			: (segment == 1 || segment == 3)
			? -1.
			: 0.;
		const auto shift = from * (1. - part) + to * part;
		applyShift(int(base::SafeRound(shift * st::shakeShift)));
	};
}

} // namespace Ui
