/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Support {

enum class SwitchSettings {
	None,
	Next,
	Previous,
};

Qt::KeyboardModifiers SkipSwitchModifiers();
bool HandleSwitch(Qt::KeyboardModifiers modifiers);
FnMut<bool()> GetSwitchMethod(SwitchSettings value);

} // namespace Support
