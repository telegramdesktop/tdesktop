/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "support/support_common.h"

#include "shortcuts.h"

namespace Support {

void PerformSwitch(SwitchSettings value) {
	switch (value) {
	case SwitchSettings::Next:
		Shortcuts::launch("next_chat");
		break;
	case SwitchSettings::Previous:
		Shortcuts::launch("previous_chat");
		break;
	default:
		break;
	}
}

} // namespace Support
