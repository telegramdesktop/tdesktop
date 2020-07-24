/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/widgets/side_bar_button.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"

namespace Ui {
class VerticalLayoutReorder;
enum class FilterIcon : uchar;
class PopupMenu;
} // namespace Ui

namespace Data {
class ChatFilter;
} // namespace Data

namespace Window {

class SessionController;

class FiltersMenu final {
public:
	FiltersMenu(
		not_null<Ui::RpWidget*> parent,
		not_null<SessionController*> session);
	~FiltersMenu();

private:
	void setup();
	void refresh();
	void setupList();
	void applyReorder(
		not_null<Ui::RpWidget*> widget,
		int oldPosition,
		int newPosition);
	[[nodiscard]] base::unique_qptr<Ui::SideBarButton> prepareButton(
		not_null<Ui::VerticalLayout*> container,
		FilterId id,
		const QString &title,
		Ui::FilterIcon icon);
	void setupMainMenuIcon();
	void showMenu(QPoint position, FilterId id);
	void showEditBox(FilterId id);
	void showRemoveBox(FilterId id);
	void remove(FilterId id);
	void scrollToButton(not_null<Ui::RpWidget*> widget);

	const not_null<SessionController*> _session;
	const not_null<Ui::RpWidget*> _parent;
	Ui::RpWidget _outer;
	Ui::SideBarButton _menu;
	Ui::ScrollArea _scroll;
	not_null<Ui::VerticalLayout*> _container;
	Ui::VerticalLayout *_list = nullptr;
	std::unique_ptr<Ui::VerticalLayoutReorder> _reorder;
	base::unique_qptr<Ui::SideBarButton> _all;
	base::unique_qptr<Ui::SideBarButton> _setup;
	base::flat_map<FilterId, base::unique_qptr<Ui::SideBarButton>> _filters;
	FilterId _activeFilterId = 0;
	int _reordering = 0;
	bool _ignoreRefresh = false;
	bool _waitingSuggested = false;

	base::unique_qptr<Ui::PopupMenu> _popupMenu;

	Ui::Animations::Simple _scrollToAnimation;

};

} // namespace Window
