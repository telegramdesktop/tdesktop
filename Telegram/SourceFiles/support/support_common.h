/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Shortcuts {
enum class Command;
} // namespace Shortcuts

namespace Support {

enum class SwitchSettings {
	None,
	Next,
	Previous,
};

[[nodiscard]] Qt::KeyboardModifiers SkipSwitchModifiers();
[[nodiscard]] bool HandleSwitch(Qt::KeyboardModifiers modifiers);
[[nodiscard]] std::optional<Shortcuts::Command> GetSwitchCommand(
	SwitchSettings value);
[[nodiscard]] FnMut<bool()> GetSwitchMethod(SwitchSettings value);

} // namespace Support
