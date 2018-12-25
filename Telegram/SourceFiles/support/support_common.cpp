/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "support/support_common.h"

#include "core/shortcuts.h"

namespace Support {

bool ValidateAccount(const MTPUser &self) {
	//return true; AssertIsDebug();
	return self.match([](const MTPDuser &data) {
		DEBUG_LOG(("ValidateAccount: %1 %2"
			).arg(Logs::b(data.has_phone())
			).arg(data.has_phone() ? qs(data.vphone) : QString()));
		return data.has_phone() && qs(data.vphone).startsWith(qstr("424"));
	}, [](const MTPDuserEmpty &data) {
		return false;
	});
}

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
