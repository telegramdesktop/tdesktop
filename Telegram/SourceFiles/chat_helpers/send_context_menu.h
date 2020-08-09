/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

enum class SendMenuType {
	Disabled,
	SilentOnly,
	Scheduled,
	ScheduledToUser, // For "Send when online".
	Reminder,
};

enum class FillMenuResult {
	Success,
	None,
};

FillMenuResult FillSendMenu(
	not_null<Ui::PopupMenu*> menu,
	Fn<SendMenuType()> type,
	Fn<void()> silent,
	Fn<void()> schedule);

void SetupSendMenuAndShortcuts(
	not_null<Ui::RpWidget*> button,
	Fn<SendMenuType()> type,
	Fn<void()> silent,
	Fn<void()> schedule);
