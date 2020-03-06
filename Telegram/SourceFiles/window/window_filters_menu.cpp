/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_filters_menu.h"

#include "mainwindow.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_chat_filters.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace Window {
namespace {

enum class Type {
	Unread,
	Unmuted,
	Custom,
};

[[nodiscard]] Type ComputeType(const Data::ChatFilter &filter) {
	using Flag = Data::ChatFilter::Flag;

	const auto all = Flag::Contacts
		| Flag::NonContacts
		| Flag::Groups
		| Flag::Broadcasts
		| Flag::Bots
		| Flag::NoArchive;
	if (!filter.always().empty()) {
		return Type::Custom;
	} else if (filter.flags() == (all | Flag::NoRead)) {
		return Type::Unread;
	} else if (filter.flags() == (all | Flag::NoMuted)) {
		return Type::Unmuted;
	}
	return Type::Custom;
}

[[nodiscard]] std::array<const style::icon*, 2> ComputeIcons(Type type) {
	switch (type) {
	case Type::Unread:
		return {
			&st::windowFiltersUnread,
			&st::windowFiltersUnreadActive
		};
	case Type::Unmuted:
		return {
			&st::windowFiltersUnmuted,
			&st::windowFiltersUnmutedActive
		};
	case Type::Custom:
		return {
			&st::windowFiltersCustom,
			&st::windowFiltersCustomActive
		};
	}
	Unexpected("Filter type in FiltersMenu::refresh.");
}

} // namespace

FiltersMenu::FiltersMenu(
	not_null<Ui::RpWidget*> parent,
	not_null<SessionController*> session)
: _session(session)
, _parent(parent)
, _widget(_parent, st::defaultSideBarMenu) {
	setup();
}

void FiltersMenu::setup() {
	_parent->heightValue(
	) | rpl::start_with_next([=](int height) {
		_widget.setGeometry({ 0, 0, st::windowFiltersWidth, height });
	}, _widget.lifetime());

	const auto filters = &_session->session().data().chatsFilters();
	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		filters->changed()
	) | rpl::start_with_next([=] {
		refresh();
	}, _widget.lifetime());

	_session->activeChatsFilter(
	) | rpl::start_with_next([=](FilterId id) {
		_widget.setActive(QString::number(id));
	}, _widget.lifetime());

	_widget.activateRequests(
	) | rpl::start_with_next([=](const QString &id) {
		if (id == "main_menu") {
			_session->widget()->showMainMenu();
		} else if (id == "setup") {
		} else if (const auto filterId = id.toInt()) {
			_session->setActiveChatsFilter(filterId);
		} else {
			_session->setActiveChatsFilter(0);
		}
	}, _widget.lifetime());
}

void FiltersMenu::refresh() {
	auto items = std::vector<Ui::SideBarMenu::Item>();
	items.push_back({
		"main_menu",
		QString(),
		QString(),
		&st::windowFiltersMainMenu,
		&st::windowFiltersMainMenu,
		st::windowFiltersMainMenuIconTop
	});
	items.push_back({
		QString::number(0),
		"All Chats",
		QString(),
		&st::windowFiltersAll,
		&st::windowFiltersAllActive,
		st::windowFiltersIconTop
	});
	const auto filters = &_session->session().data().chatsFilters();
	for (const auto &filter : filters->list()) {
		const auto type = ComputeType(filter);
		const auto icons = ComputeIcons(type);
		items.push_back({
			QString::number(filter.id()),
			filter.title(),
			QString(),
			icons[0],
			icons[1],
			st::windowFiltersIconTop
		});
	}
	items.push_back({
		"setup",
		"Setup",
		QString(),
		&st::windowFiltersSetup,
		&st::windowFiltersSetup,
		st::windowFiltersIconTop
	});
	_widget.setItems(items);
}

} // namespace Window
