/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_filters_menu.h"

#include "mainwindow.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_chat_filters.h"
#include "boxes/filters/manage_filters_box.h"
#include "lang/lang_keys.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace Window {
namespace {

enum class Type {
	Unread,
	Unmuted,
	People,
	Groups,
	Channels,
	Bots,
	Custom,
};

[[nodiscard]] Type ComputeType(const Data::ChatFilter &filter) {
	using Flag = Data::ChatFilter::Flag;

	const auto all = Flag::Contacts
		| Flag::NonContacts
		| Flag::Groups
		| Flag::Channels
		| Flag::Bots;
	const auto removed = Flag::NoRead | Flag::NoMuted;
	const auto people = Flag::Contacts | Flag::NonContacts;
	const auto allNoArchive = all | Flag::NoArchived;
	if (!filter.always().empty()
		|| !filter.never().empty()
		|| !(filter.flags() & all)) {
		return Type::Custom;
	} else if ((filter.flags() & all) == Flag::Contacts
		|| (filter.flags() & all) == Flag::NonContacts
		|| (filter.flags() & all) == people) {
		return Type::People;
	} else if ((filter.flags() & all) == Flag::Groups) {
		return Type::Groups;
	} else if ((filter.flags() & all) == Flag::Channels) {
		return Type::Channels;
	} else if ((filter.flags() & all) == Flag::Bots) {
		return Type::Bots;
	} else if ((filter.flags() & removed) == Flag::NoRead) {
		return Type::Unread;
	} else if ((filter.flags() & removed) == Flag::NoMuted) {
		return Type::Unmuted;
	}
	return Type::Custom;
}

[[nodiscard]] const style::SideBarButton &ComputeStyle(Type type) {
	switch (type) {
	case Type::Unread: return st::windowFiltersUnread;
	case Type::Unmuted: return st::windowFiltersUnmuted;
	case Type::People: return st::windowFiltersPrivate;
	case Type::Groups: return st::windowFiltersGroups;
	case Type::Channels: return st::windowFiltersChannels;
	case Type::Bots: return st::windowFiltersBots;
	case Type::Custom: return st::windowFiltersCustom;
	}
	Unexpected("Filter type in FiltersMenu::refresh.");
}

} // namespace

FiltersMenu::FiltersMenu(
	not_null<Ui::RpWidget*> parent,
	not_null<SessionController*> session)
: _session(session)
, _parent(parent)
, _outer(_parent)
, _menu(&_outer, QString(), st::windowFiltersMainMenu)
, _scroll(&_outer)
, _container(
	_scroll.setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(&_scroll))) {
	setup();
}

void FiltersMenu::setup() {
	_outer.setAttribute(Qt::WA_OpaquePaintEvent);
	_outer.show();
	_outer.paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		const auto bottom = _scroll.y() + _container->height();
		const auto height = _outer.height() - bottom;
		if (height <= 0) {
			return;
		}
		const auto fill = clip.intersected(
			QRect(0, bottom, _outer.width(), height));
		if (!fill.isEmpty()) {
			auto p = QPainter(&_outer);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowFiltersAll.textBg);
			p.drawRect(fill);
		}
	}, _outer.lifetime());

	_parent->heightValue(
	) | rpl::start_with_next([=](int height) {
		const auto width = st::windowFiltersWidth;
		_outer.setGeometry({ 0, 0, width, height });
		_menu.resizeToWidth(width);
		_menu.move(0, 0);
		_scroll.setGeometry(
			{ 0, _menu.height(), width, height - _menu.height() });
		_container->resizeToWidth(width);
		_container->move(0, 0);
	}, _outer.lifetime());

	const auto filters = &_session->session().data().chatsFilters();
	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		filters->changed()
	) | rpl::start_with_next([=] {
		refresh();
	}, _outer.lifetime());

	_activeFilterId = _session->activeChatsFilterCurrent();
	_session->activeChatsFilter(
	) | rpl::filter([=](FilterId id) {
		return id != _activeFilterId;
	}) | rpl::start_with_next([=](FilterId id) {
		const auto i = _filters.find(_activeFilterId);
		if (i != end(_filters)) {
			i->second->setActive(false);
		}
		_activeFilterId = id;
		const auto j = _filters.find(_activeFilterId);
		if (j != end(_filters)) {
			j->second->setActive(true);
		}
	}, _outer.lifetime());

	_menu.setClickedCallback([=] {
		_session->widget()->showMainMenu();
	});
}

void FiltersMenu::refresh() {
	const auto filters = &_session->session().data().chatsFilters();
	if (filters->list().empty()) {
		return;
	}
	const auto manage = _outer.lifetime().make_state<ManageFiltersPrepare>(
		_session);
	auto now = base::flat_map<int, base::unique_qptr<Ui::SideBarButton>>();
	const auto prepare = [&](
			FilterId id,
			const QString &title,
			const style::SideBarButton &st,
			const QString &badge) {
		auto button = base::unique_qptr<Ui::SideBarButton>(_container->add(
			object_ptr<Ui::SideBarButton>(
				_container,
				title,
				st)));
		button->setBadge(badge);
		button->setActive(_session->activeChatsFilterCurrent() == id);
		button->setClickedCallback([=] {
			if (id >= 0) {
				_session->setActiveChatsFilter(id);
			} else {
				manage->showBox();
			}
		});
		now.emplace(id, std::move(button));
	};
	prepare(0, tr::lng_filters_all(tr::now), st::windowFiltersAll, {});
	for (const auto filter : filters->list()) {
		prepare(
			filter.id(),
			filter.title(),
			ComputeStyle(ComputeType(filter)),
			QString());
	}
	prepare(-1, tr::lng_filters_setup(tr::now), st::windowFiltersSetup, {});
	_filters = std::move(now);
}

} // namespace Window
