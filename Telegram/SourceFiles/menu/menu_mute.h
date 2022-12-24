/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
class Thread;
} // namespace Data

namespace Ui {
class PopupMenu;
class RpWidget;
class Show;
} // namespace Ui

namespace MuteMenu {

void FillMuteMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<Data::Thread*> thread,
	std::shared_ptr<Ui::Show> show);

void SetupMuteMenu(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<> triggers,
	Fn<Data::Thread*()> makeThread,
	std::shared_ptr<Ui::Show> show);

} // namespace MuteMenu
