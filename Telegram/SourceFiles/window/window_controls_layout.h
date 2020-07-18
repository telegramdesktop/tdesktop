/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {

enum class Control {
	Unknown,
	Minimize,
	Maximize,
	Close,
};

struct ControlsLayout {
	std::vector<Control> left;
	std::vector<Control> right;
};

} // namespace Window
