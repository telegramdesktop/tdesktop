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
#include "window/window_main_menu.h"
#include "window/window_peer_menu.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_chat_filters.h"
#include "data/data_folder.h"
#include "lang/lang_keys.h"
#include "ui/filter_icons.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "ui/widgets/popup_menu.h"
#include "boxes/confirm_box.h"
#include "boxes/filters/edit_filter_box.h"
#include "settings/settings_common.h"
#include "api/api_chat_filters.h"
#include "apiwrap.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace Window {
namespace {

[[nodiscard]] rpl::producer<Dialogs::UnreadState> MainListUnreadState(
		not_null<Dialogs::MainList*> list) {
	return rpl::single(rpl::empty_value()) | rpl::then(
		list->unreadStateChanges() | rpl::to_empty
	) | rpl::map([=] {
		return list->unreadState();
	});
}

[[nodiscard]] rpl::producer<Dialogs::UnreadState> UnreadStateValue(
		not_null<Main::Session*> session,
		FilterId filterId) {
	if (filterId > 0) {
		const auto filters = &session->data().chatsFilters();
		return MainListUnreadState(filters->chatsList(filterId));
	}
	return MainListUnreadState(
		session->data().chatsList()
	) | rpl::map([=](const Dialogs::UnreadState &state) {
		const auto folderId = Data::Folder::kId;
		if (const auto folder = session->data().folderLoaded(folderId)) {
			return state - folder->chatsList()->unreadState();
		}
		return state;
	});
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

FiltersMenu::~FiltersMenu() = default;

void FiltersMenu::setup() {
	setupMainMenuIcon();

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
			scrollToButton(j->second);
		} else if (!_activeFilterId) {
			_all->setActive(true);
			scrollToButton(_all);
		}
		_reorder->finishReordering();
	}, _outer.lifetime());

	_menu.setClickedCallback([=] {
		_session->widget()->showMainMenu();
	});
}

void FiltersMenu::setupMainMenuIcon() {
	OtherAccountsUnreadState(
	) | rpl::start_with_next([=](const OthersUnreadState &state) {
		const auto icon = !state.count
			? nullptr
			: !state.allMuted
			? &st::windowFiltersMainMenuUnread
			: &st::windowFiltersMainMenuUnreadMuted;
		_menu.setIconOverride(icon, icon);
	}, _outer.lifetime());
}

void FiltersMenu::scrollToButton(not_null<Ui::RpWidget*> widget) {
	const auto globalPosition = widget->mapToGlobal(QPoint(0, 0));
	const auto localTop = _scroll.mapFromGlobal(globalPosition).y();
	const auto localBottom = localTop + widget->height() - _scroll.height();
	const auto isTopEdge = (localTop < 0);
	const auto isBottomEdge = (localBottom > 0);
	if (!isTopEdge && !isBottomEdge) {
		return;
	}

	_scrollToAnimation.stop();
	const auto scrollTop = _scroll.scrollTop();
	const auto scrollTo = scrollTop + (isBottomEdge ? localBottom : localTop);

	auto scroll = [=] {
		_scroll.scrollToY(qRound(_scrollToAnimation.value(scrollTo)));
	};

	_scrollToAnimation.start(
		std::move(scroll),
		scrollTop,
		scrollTo,
		st::slideDuration,
		anim::sineInOut);
}

void FiltersMenu::refresh() {
	const auto filters = &_session->session().data().chatsFilters();
	if (filters->list().empty() || _ignoreRefresh) {
		return;
	}
	const auto oldTop = _scroll.scrollTop();

	if (!_list) {
		setupList();
	}
	_reorder->cancel();
	auto now = base::flat_map<int, base::unique_qptr<Ui::SideBarButton>>();
	for (const auto &filter : filters->list()) {
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

	// After the filters are refreshed, the scroll is reset,
	// so we have to restore it.
	_scroll.scrollToY(oldTop);
	const auto i = _filters.find(_activeFilterId);
	scrollToButton((i != end(_filters)) ? i->second : _all);
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
		Ui::FilterIcon::Edit);
	_reorder = std::make_unique<Ui::VerticalLayoutReorder>(_list, &_scroll);

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
	if (id >= 0) {
		UnreadStateValue(
			&_session->session(),
			id
		) | rpl::start_with_next([=](const Dialogs::UnreadState &state) {
			const auto count = (state.chats + state.marks);
			const auto muted = (state.chatsMuted + state.marksMuted);
			const auto string = !count
				? QString()
				: (count > 99)
				? "99+"
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
			const auto filters = &_session->session().data().chatsFilters();
			if (filters->suggestedLoaded()) {
				_session->showSettings(Settings::Type::Folders);
			} else if (!_waitingSuggested) {
				_waitingSuggested = true;
				filters->requestSuggested();
				filters->suggestedUpdated(
				) | rpl::take(1) | rpl::start_with_next([=] {
					_session->showSettings(Settings::Type::Folders);
				}, _outer.lifetime());
			}
		}
	});
	if (id > 0) {
		raw->events(
		) | rpl::filter([=](not_null<QEvent*> e) {
			return e->type() == QEvent::ContextMenu;
		}) | rpl::start_with_next([=] {
			showMenu(QCursor::pos(), id);
		}, raw->lifetime());
	}
	return button;
}

void FiltersMenu::showMenu(QPoint position, FilterId id) {
	if (_popupMenu) {
		_popupMenu = nullptr;
		return;
	}
	const auto i = _filters.find(id);
	if (i == end(_filters)) {
		return;
	}
	_popupMenu = base::make_unique_q<Ui::PopupMenu>(i->second.get());
	const auto addAction = [&](const QString &text, Fn<void()> callback) {
		return _popupMenu->addAction(
			text,
			crl::guard(&_outer, std::move(callback)));
	};

	addAction(
		tr::lng_filters_context_edit(tr::now),
		[=] { showEditBox(id); });

	auto filteredChats = [=] {
		return _session->session().data().chatsFilters().chatsList(id);
	};
	Window::MenuAddMarkAsReadChatListAction(
		std::move(filteredChats),
		addAction);

	addAction(
		tr::lng_filters_context_remove(tr::now),
		[=] { showRemoveBox(id); });
	_popupMenu->popup(position);
}

void FiltersMenu::showEditBox(FilterId id) {
	EditExistingFilter(_session, id);
}

void FiltersMenu::showRemoveBox(FilterId id) {
	_session->window().show(Box<ConfirmBox>(
		tr::lng_filters_remove_sure(tr::now),
		tr::lng_filters_remove_yes(tr::now),
		[=](Fn<void()> &&close) { close(); remove(id); }));
}

void FiltersMenu::remove(FilterId id) {
	_session->session().data().chatsFilters().apply(MTP_updateDialogFilter(
		MTP_flags(MTPDupdateDialogFilter::Flag(0)),
		MTP_int(id),
		MTPDialogFilter()));
	_session->session().api().request(MTPmessages_UpdateDialogFilter(
		MTP_flags(MTPmessages_UpdateDialogFilter::Flag(0)),
		MTP_int(id),
		MTPDialogFilter()
	)).send();
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

	auto order = ranges::views::all(
		list
	) | ranges::views::transform(
		&Data::ChatFilter::id
	) | ranges::to_vector;
	base::reorder(order, oldPosition, newPosition);

	_ignoreRefresh = true;
	filters->saveOrder(order);
	_ignoreRefresh = false;
}

} // namespace Window
