/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_chat_filters_remove_manager.h"
#include "base/timer.h"
#include "base/weak_qptr.h"
#include "ui/effects/animations.h"
#include "ui/widgets/side_bar_button.h"
#include "ui/widgets/scroll_area.h"

namespace Data {
struct ChatFilterTitle;
} // namespace Data

namespace Ui {
class VerticalLayout;
class VerticalLayoutReorder;
enum class FilterIcon : uchar;
class PopupMenu;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Window {

class SessionController;
class FolderFavoriteButton;

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
	void updateFavorite();
	void createFavorite();
	void destroyFavorite();
	void applyReorder(
		not_null<Ui::RpWidget*> widget,
		int oldPosition,
		int newPosition);
	[[nodiscard]] bool premium() const;
	[[nodiscard]] base::unique_qptr<Ui::SideBarButton> prepareAll();
	[[nodiscard]] base::unique_qptr<Ui::SideBarButton> prepareButton(
		not_null<Ui::VerticalLayout*> container,
		FilterId id,
		Data::ChatFilterTitle title,
		Ui::FilterIcon icon,
		bool locked = false,
		bool toBeginning = false);
	void setupMainMenuIcon();
	void showMenu(QPoint position, FilterId id);
	void scrollToButton(not_null<Ui::RpWidget*> widget);
	void applyFilterAt(int start, int delta);
	void moveToFilter(int delta);
	void moveToFilterEdge(int delta);
	void setListTabStop(not_null<Ui::SideBarButton*> stop);
	[[nodiscard]] bool listFocused() const;
	void openFiltersSettings();
	void setupDragAndDrop();

	const not_null<SessionController*> _session;
	const not_null<Ui::RpWidget*> _parent;
	Ui::RpWidget _outer;
	Ui::SideBarButton _menu;
	Ui::ScrollArea _scroll;
	not_null<Ui::VerticalLayout*> _container;
	Ui::VerticalLayout *_list = nullptr;
	std::unique_ptr<Ui::VerticalLayoutReorder> _reorder;
	base::unique_qptr<Ui::SideBarButton> _setup;
	base::unique_qptr<Ui::SlideWrap<FolderFavoriteButton>> _favorite;
	base::flat_map<FilterId, base::unique_qptr<Ui::SideBarButton>> _filters;
	base::weak_qptr<Ui::SideBarButton> _tabStop;
	rpl::variable<bool> _includeMuted;
	FilterId _activeFilterId = 0;
	int _reordering = 0;
	bool _ignoreRefresh = false;
	bool _waitingSuggested = false;

	Api::RemoveComplexChatFilter _removeApi;

	base::unique_qptr<Ui::PopupMenu> _popupMenu;
	struct {
		base::Timer timer;
		FilterId filterId = FilterId(-1);
	} _drag;

	Ui::Animations::Simple _scrollToAnimation;

};

} // namespace Window
