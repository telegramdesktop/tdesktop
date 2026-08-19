/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "ui/style/style_core_types.h"

class QAction;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Menu {

not_null<QAction*> AddCheckedAction(
	not_null<Ui::PopupMenu*> menu,
	const QString &text,
	Fn<void()> callback,
	const style::icon *icon,
	bool checked);

// Like a usual menu action, but when `active` the whole item (icon, text and
// shortcut) is painted in st::windowActiveTextFg instead of showing a check.
// When `premiumStarSize` is positive a gradient premium star of that size is
// painted right-aligned, with any shortcut text shifted to the star's left.
not_null<QAction*> AddActiveColorAction(
	not_null<Ui::PopupMenu*> menu,
	const QString &text,
	Fn<void()> callback,
	const style::icon *icon,
	bool active,
	int premiumStarSize = 0);

} // namespace Menu
