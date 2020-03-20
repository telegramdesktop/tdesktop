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
#include "ui/filter_icons.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "api/api_chat_filters.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace Window {

FiltersMenu::FiltersMenu(
	not_null<Ui::RpWidget*> parent,
	not_null<SessionController*> session)
: _session(session)
, _parent(parent)
, _manage(std::make_unique<ManageFiltersPrepare>(_session))
, _outer(_parent)
, _menu(&_outer, QString(), st::windowFiltersMainMenu)
, _scroll(&_outer)
, _container(
	_scroll.setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(&_scroll))) {
	setup();
}

FiltersMenu::~FiltersMenu() = default;

void FiltersMenu::setup() {
	_outer.setAttribute(Qt::WA_OpaquePaintEvent);
	_outer.show();
	_outer.paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(&_outer);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowFiltersButton.textBg);
		p.drawRect(clip);
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
		} else if (!_activeFilterId) {
			_all->setActive(false);
		}
		_activeFilterId = id;
		const auto j = _filters.find(_activeFilterId);
		if (j != end(_filters)) {
			j->second->setActive(true);
		} else if (!_activeFilterId) {
			_all->setActive(true);
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

	if (!_list) {
		setupList();
	}
	_reorder->cancel();
	auto now = base::flat_map<int, base::unique_qptr<Ui::SideBarButton>>();
	for (const auto filter : filters->list()) {
		now.emplace(
			filter.id(),
			prepareButton(
				_list,
				filter.id(),
				filter.title(),
				Ui::ComputeFilterIcon(filter)));
	}
	_filters = std::move(now);
	_reorder->start();

	_container->resizeToWidth(_outer.width());
}

void FiltersMenu::setupList() {
	_all = prepareButton(
		_container,
		0,
		tr::lng_filters_all(tr::now),
		Ui::FilterIcon::All);
	_list = _container->add(object_ptr<Ui::VerticalLayout>(_container));
	_setup = prepareButton(
		_container,
		-1,
		tr::lng_filters_setup(tr::now),
		Ui::FilterIcon::Setup);
	_reorder = std::make_unique<Ui::VerticalLayoutReorder>(_list);

	_reorder->updates(
		) | rpl::start_with_next([=](Ui::VerticalLayoutReorder::Single data) {
		using State = Ui::VerticalLayoutReorder::State;
		if (data.state == State::Started) {
			++_reordering;
		} else {
			Ui::PostponeCall(&_outer, [=] {
				--_reordering;
			});
			if (data.state == State::Applied) {
				applyReorder(data.widget, data.oldPosition, data.newPosition);
			}
		}
	}, _outer.lifetime());
}

base::unique_qptr<Ui::SideBarButton> FiltersMenu::prepareButton(
		not_null<Ui::VerticalLayout*> container,
		FilterId id,
		const QString &title,
		Ui::FilterIcon icon) {
	auto button = base::unique_qptr<Ui::SideBarButton>(container->add(
		object_ptr<Ui::SideBarButton>(
			container,
			title,
			st::windowFiltersButton)));
	const auto raw = button.get();
	const auto &icons = Ui::LookupFilterIcon(icon);
	raw->setIconOverride(icons.normal, icons.active);
	if (id > 0) {
		const auto filters = &_session->session().data().chatsFilters();
		const auto list = filters->chatsList(id);
		rpl::single(rpl::empty_value()) | rpl::then(
			list->unreadStateChanges(
			) | rpl::map([] { return rpl::empty_value(); })
		) | rpl::start_with_next([=] {
			const auto &state = list->unreadState();
			const auto count = (state.chats + state.marks);
			const auto muted = (state.chatsMuted + state.marksMuted);
			const auto string = !count
				? QString()
				: (count > 99)
				? "..."
				: QString::number(count);
			raw->setBadge(string, count == muted);
		}, raw->lifetime());
	}
	raw->setActive(_session->activeChatsFilterCurrent() == id);
	raw->setClickedCallback([=] {
		if (_reordering) {
			return;
		} else if (id >= 0) {
			_session->setActiveChatsFilter(id);
		} else {
			_manage->showBox();
		}
	});
	return button;
}

void FiltersMenu::applyReorder(
		not_null<Ui::RpWidget*> widget,
		int oldPosition,
		int newPosition) {
	if (newPosition == oldPosition) {
		return;
	}

	const auto filters = &_session->session().data().chatsFilters();
	const auto &list = filters->list();
	Assert(oldPosition >= 0 && oldPosition < list.size());
	Assert(newPosition >= 0 && newPosition < list.size());
	const auto id = list[oldPosition].id();
	const auto i = _filters.find(id);
	Assert(i != end(_filters));
	Assert(i->second == widget);

	auto order = ranges::view::all(
		list
	) | ranges::view::transform(
		&Data::ChatFilter::id
	) | ranges::to_vector;
	base::reorder(order, oldPosition, newPosition);
	Api::SaveNewOrder(&_session->session(), order);
}

} // namespace Window
