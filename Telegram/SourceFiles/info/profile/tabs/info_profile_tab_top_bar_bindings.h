/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_wrap_widget.h"

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Info::Profile {

struct TabTopBarBindings {
	rpl::producer<TextWithEntities> title;
	rpl::producer<TextWithEntities> subtitle;
	Fn<void(const Ui::Menu::MenuCallback&)> fillMenu;
	rpl::producer<SelectedItems> selectedItems;
	rpl::producer<bool> searchEnabledByContent;
	Fn<void(SelectionAction)> selectionAction;
	Fn<void(QString)> applySearchQuery;
};

} // namespace Info::Profile
