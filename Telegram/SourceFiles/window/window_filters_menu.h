/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/side_bar_button.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"

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
	Ui::RpWidget _outer;
	Ui::SideBarButton _menu;
	Ui::ScrollArea _scroll;
	not_null<Ui::VerticalLayout*> _container;
	base::flat_map<FilterId, base::unique_qptr<Ui::SideBarButton>> _filters;
	FilterId _activeFilterId = 0;

};

} // namespace Window
