/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class PopupMenu;
class RpWidget;
class Show;
} // namespace Ui

namespace TTLMenu {

struct Args {
	std::shared_ptr<Ui::Show> show;
	TimeId startTtl;
	rpl::producer<QString> about;
	Fn<void(TimeId)> callback;
};

void FillTTLMenu(not_null<Ui::PopupMenu*> menu, Args args);

void SetupTTLMenu(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<> triggers,
	Args args);

} // namespace TTLMenu
