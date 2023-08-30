/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "styles/style_dialogs.h"

namespace Dialogs {

[[nodiscard]] inline const style::icon &ThreeStateIcon(
		const style::ThreeStateIcon &icons,
		bool active,
		bool over) {
	return active ? icons.active : over ? icons.over : icons.icon;
}

} // namespace Dialogs
