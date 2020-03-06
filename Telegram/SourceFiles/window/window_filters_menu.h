/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/side_bar_menu.h"

namespace Window {

class SessionController;

class FiltersMenu final {
public:
	FiltersMenu(
		not_null<Ui::RpWidget*> parent,
		not_null<SessionController*> session);

private:
	void setup();
	void refresh();

	const not_null<SessionController*> _session;
	const not_null<Ui::RpWidget*> _parent;
	Ui::SideBarMenu _widget;

};

} // namespace Window
