/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/controls/swipe_handler_data.h"

namespace Lottie {
class Icon;
} // namespace Lottie

namespace Dialogs::Ui {

using namespace ::Ui;

enum class QuickDialogAction {
	Mute,
	Pin,
	Read,
	Archive,
	Delete,
	Disabled,
};

enum class QuickDialogActionLabel {
	Mute,
	Unmute,
	Pin,
	Unpin,
	Read,
	Unread,
	Archive,
	Unarchive,
	Delete,
	Disabled,
};

struct QuickActionContext {
	::Ui::Controls::SwipeContextData data;
	Lottie::Icon *icon = nullptr;
	QuickDialogAction action;
};

} // namespace Dialogs::Ui
