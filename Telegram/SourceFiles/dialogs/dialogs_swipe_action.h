/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Dialogs::Ui {
enum class SwipeDialogAction;
} // namespace Dialogs::Ui

namespace Dialogs {

void PerformSwipeDialogAction(
	not_null<PeerData*> peer,
	Ui::SwipeDialogAction action);

} // namespace Dialogs
