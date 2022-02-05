/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "support/support_common.h"

#include "core/shortcuts.h"

namespace Support {

bool HandleSwitch(Qt::KeyboardModifiers modifiers) {
	return !(modifiers & Qt::ShiftModifier)
		|| (!(modifiers & Qt::ControlModifier)
			&& !(modifiers & Qt::MetaModifier));
}

Qt::KeyboardModifiers SkipSwitchModifiers() {
	return Qt::ControlModifier | Qt::ShiftModifier;
}

std::optional<Shortcuts::Command> GetSwitchCommand(SwitchSettings value) {
	switch (value) {
	case SwitchSettings::Next:
		return Shortcuts::Command::ChatNext;
	case SwitchSettings::Previous:
		return Shortcuts::Command::ChatPrevious;
	}
	return std::nullopt;
}

FnMut<bool()> GetSwitchMethod(SwitchSettings value) {
	const auto command = GetSwitchCommand(value);
	return command ? Shortcuts::RequestHandler(*command) : nullptr;
}

} // namespace Support
