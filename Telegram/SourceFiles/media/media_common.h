/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/algorithm.h"

#include <compare>

namespace Media {

enum class RepeatMode {
	None,
	One,
	All,
};

enum class OrderMode {
	Default,
	Reverse,
	Shuffle,
};

struct VideoQuality {
	uint32 manual : 1 = 0;
	uint32 height : 31 = 0;

	friend inline constexpr auto operator<=>(
		VideoQuality,
		VideoQuality) = default;
	friend inline constexpr bool operator==(
		VideoQuality,
		VideoQuality) = default;
};

inline constexpr auto kSpeedMin = 0.5;
inline constexpr auto kSpeedMax = 2.5;
inline constexpr auto kSpedUpDefault = 1.7;

[[nodiscard]] inline bool EqualSpeeds(float64 a, float64 b) {
	return int(base::SafeRound(a * 10.)) == int(base::SafeRound(b * 10.));
}

} // namespace Media
