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

FnMut<bool()> GetSwitchMethod(SwitchSettings value) {
	switch (value) {
	case SwitchSettings::Next:
		return Shortcuts::RequestHandler(Shortcuts::Command::ChatNext);
	case SwitchSettings::Previous:
		return Shortcuts::RequestHandler(Shortcuts::Command::ChatPrevious);
	}
	return nullptr;
}

} // namespace Support
