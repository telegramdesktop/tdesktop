/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/slider_tick_labels.h"

namespace Ui {

std::vector<int> LayoutSliderTickLabels(
		const std::vector<int> &centers,
		const std::vector<int> &widths,
		int outer,
		int minGap) {
	Expects(centers.size() == widths.size());

	const auto count = int(centers.size());
	auto result = std::vector<int>(count);
	auto total = std::max(count - 1, 0) * minGap;
	for (auto i = 0; i != count; ++i) {
		result[i] = std::clamp(
			centers[i] - widths[i] / 2,
			0,
			std::max(outer - widths[i], 0));
		total += widths[i];
	}
	if (total > outer) {
		// WHY: packing labels that cannot all fit piles them at the left.
		return result;
	}
	for (auto i = 1; i < count; ++i) {
		result[i] = std::max(
			result[i],
			result[i - 1] + widths[i - 1] + minGap);
	}
	if (count > 0) {
		result[count - 1] = std::min(
			result[count - 1],
			outer - widths[count - 1]);
	}
	for (auto i = count - 1; i > 0; --i) {
		result[i - 1] = std::min(
			result[i - 1],
			result[i] - minGap - widths[i - 1]);
	}
	for (auto &left : result) {
		left = std::max(left, 0);
	}
	return result;
}

} // namespace Ui
