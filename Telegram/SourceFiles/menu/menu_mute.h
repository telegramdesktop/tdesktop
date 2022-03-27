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
} // namespace Ui

namespace MuteMenu {

void FillMuteMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<PeerData*> peer);

void SetupMuteMenu(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<> triggers,
	not_null<PeerData*> peer);

} // namespace MuteMenu
