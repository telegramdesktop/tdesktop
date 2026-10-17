/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
struct PopupMenu;
} // namespace style

namespace Ui {

class PopupMenu;
class RippleButton;

void KeepHoveredWhileShown(
	not_null<RippleButton*> button,
	not_null<PopupMenu*> menu);

void SetupButtonContextMenu(
	not_null<RippleButton*> button,
	not_null<const style::PopupMenu*> st,
	Fn<void(not_null<PopupMenu*>)> fill);

} // namespace Ui
