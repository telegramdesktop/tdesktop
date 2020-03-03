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
	explicit FiltersMenu(not_null<SessionController*> session);

private:
	void setup();
	void refresh();

	const not_null<SessionController*> _session;
	Ui::SideBarMenu _widget;

};

} // namespace Window
