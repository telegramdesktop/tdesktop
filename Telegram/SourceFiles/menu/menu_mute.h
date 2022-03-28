/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Ui {
class PopupMenu;
class RpWidget;
class Show;
} // namespace Ui

namespace MuteMenu {

struct Args {
	not_null<PeerData*> peer;
	std::shared_ptr<Ui::Show> show;
};

void FillMuteMenu(
	not_null<Ui::PopupMenu*> menu,
	Args args);

void SetupMuteMenu(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<> triggers,
	Args args);

} // namespace MuteMenu
