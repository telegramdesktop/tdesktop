/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

// Left edges for labels centered under their ticks, kept inside [0, outer]
// and gap apart, or closer when the row is short, moving as few as needed.
[[nodiscard]] std::vector<int> LayoutSliderTickLabels(
	const std::vector<int> &centers,
	const std::vector<int> &widths,
	int outer,
	int gap);

} // namespace Ui
