/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Statistic {

struct Limits final {
	float64 min = 0;
	float64 max = 0;
};

enum class ChartViewType {
	Linear,
	Stack,
	DoubleLinear,
	StackLinear,
};

[[nodiscard]] inline Limits FindNearestElements(
		const std::vector<float64> &vector,
		const Limits &limit) {
	const auto find = [&](float64 raw) -> float64 {
		const auto it = ranges::lower_bound(vector, raw);
		const auto left = raw - (*(it - 1));
		const auto right = (*it) - raw;
		const auto nearestXPercentageIt = ((right) > (left)) ? (it - 1) : it;
		return std::distance(
			begin(vector),
			nearestXPercentageIt);
	};
	return { find(limit.min), find(limit.max) };
}

} // namespace Statistic
