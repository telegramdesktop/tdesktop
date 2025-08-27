/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/menu/menu_item_base.h"

class HistoryItem;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Menu {

class RateTranscribe final : public Ui::Menu::ItemBase {
public:
	RateTranscribe(
		not_null<Ui::PopupMenu*> parent,
		const style::Menu &st,
		Fn<void(bool)> rate);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

protected:
	int contentHeight() const override;

private:
	int _desiredHeight = 0;
	not_null<QAction*> _dummyAction;

};

bool HasRateTranscribeItem(not_null<HistoryItem*>);

} // namespace Menu
