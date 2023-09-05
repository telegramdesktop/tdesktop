/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "base/algorithm.h"

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

inline constexpr auto kSpeedMin = 0.5;
inline constexpr auto kSpeedMax = 2.5;
inline constexpr auto kSpedUpDefault = 1.7;

[[nodiscard]] inline bool EqualSpeeds(float64 a, float64 b) {
	return int(base::SafeRound(a * 10.)) == int(base::SafeRound(b * 10.));
}

} // namespace Media
