/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

// Left edges for labels centered under their ticks, kept inside [0, outer]
// and at least minGap apart, moving as few labels as needed.
[[nodiscard]] std::vector<int> LayoutSliderTickLabels(
	const std::vector<int> &centers,
	const std::vector<int> &widths,
	int outer,
	int minGap);

} // namespace Ui
