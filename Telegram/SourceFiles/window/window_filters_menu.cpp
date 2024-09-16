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
#include "core/application.h"
#include "core/core_settings.h"
#include "window/notifications_manager.h"
#include "data/data_session.h"
#include "data/data_chat_filters.h"
#include "data/data_folder.h"
#include "data/data_user.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "lang/lang_keys.h"
#include "ui/filter_icons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "ui/widgets/popup_menu.h"
#include "ui/boxes/confirm_box.h"
#include "ui/ui_utility.h"
#include "boxes/filters/edit_filter_box.h"
#include "boxes/premium_limits_box.h"
#include "settings/settings_folders.h"
#include "storage/storage_media_prepare.h"
#include "api/api_chat_filters.h"
#include "apiwrap.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "styles/style_layers.h" // attentionBoxButton
#include "styles/style_menu_icons.h"

namespace Window {
namespace {

[[nodiscard]] Dialogs::UnreadState MainListMapUnreadState(
		not_null<Main::Session*> session,
		const Dialogs::UnreadState &state) {
	const auto folderId = Data::Folder::kId;
	if (const auto folder = session->data().folderLoaded(folderId)) {
		return state - folder->chatsList()->unreadState();
	}
	return state;
}

[[nodiscard]] rpl::producer<Dialogs::UnreadState> MainListUnreadState(
		not_null<Dialogs::MainList*> list) {
	return rpl::single(rpl::empty) | rpl::then(
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
		return MainListMapUnreadState(session, state);
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
		object_ptr<Ui::VerticalLayout>(&_scroll)))
, _includeMuted(Core::App().settings().includeMutedCounterFolders()) {
	Core::App().notifications().settingsChanged(
	) | rpl::filter(
		rpl::mappers::_1 == Window::Notifications::ChangeType::IncludeMuted
	) | rpl::start_with_next([=] {
		_includeMuted = Core::App().settings().includeMutedCounterFolders();
	}, _outer.lifetime());

	_drag.timer.setCallback([=] {
		if (_drag.filterId >= 0) {
			_session->setActiveChatsFilter(_drag.filterId);
		}
	});
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

	auto premium = Data::AmPremiumValue(&_session->session());

	const auto filters = &_session->session().data().chatsFilters();
	rpl::combine(
		rpl::single(rpl::empty) | rpl::then(filters->changed()),
		std::move(premium)
	) | rpl::start_with_next([=] {
		refresh();
	}, _outer.lifetime());

	_activeFilterId = _session->activeChatsFilterCurrent();
	_session->activeChatsFilter(
	) | rpl::filter([=](FilterId id) {
		return (id != _activeFilterId);
	}) | rpl::start_with_next([=](FilterId id) {
		if (!_list) {
			_activeFilterId = id;
			return;
		}
		const auto i = _filters.find(_activeFilterId);
		if (i != end(_filters)) {
			i->second->setActive(false);
		}
		_activeFilterId = id;
		const auto j = _filters.find(_activeFilterId);
		if (j != end(_filters)) {
			j->second->setActive(true);
			scrollToButton(j->second);
		}
		_reorder->finishReordering();
	}, _outer.lifetime());

	_menu.setClickedCallback([=] {
		_session->widget()->showMainMenu();
	});
}

void FiltersMenu::setupMainMenuIcon() {
	OtherAccountsUnreadState(
		&_session->session().account()
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
	if (!filters->has() || _ignoreRefresh) {
		return;
	}
	const auto oldTop = _scroll.scrollTop();
	const auto reorderAll = premium();
	if (!_list) {
		setupList();
	}
	_reorder->cancel();

	_reorder->clearPinnedIntervals();
	const auto maxLimit = (reorderAll ? 1 : 0)
		+ Data::PremiumLimits(&_session->session()).dialogFiltersCurrent();
	const auto premiumFrom = (reorderAll ? 0 : 1) + maxLimit;
	if (!reorderAll) {
		_reorder->addPinnedInterval(0, 1);
	}
	_reorder->addPinnedInterval(
		premiumFrom,
		std::max(1, int(filters->list().size()) - maxLimit));

	auto now = base::flat_map<int, base::unique_qptr<Ui::SideBarButton>>();
	const auto &currentFilter = _session->activeChatsFilterCurrent();
	for (const auto &filter : filters->list()) {
		const auto nextIsLocked = (now.size() >= premiumFrom);
		if (nextIsLocked && (currentFilter == filter.id())) {
			_session->setActiveChatsFilter(FilterId(0));
		}
		auto button = prepareButton(
			_list,
			filter.id(),
			filter.title(),
			Ui::ComputeFilterIcon(filter));
		button->setLocked(nextIsLocked);
		now.emplace(filter.id(), std::move(button));
	}
	_filters = std::move(now);
	_reorder->start();

	_container->resizeToWidth(_outer.width());

	// After the filters are refreshed, the scroll is reset,
	// so we have to restore it.
	_scroll.scrollToY(oldTop);
}

void FiltersMenu::setupList() {
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

bool FiltersMenu::premium() const {
	return _session->session().user()->isPremium();
}

base::unique_qptr<Ui::SideBarButton> FiltersMenu::prepareAll() {
	return prepareButton(_container, 0, {}, Ui::FilterIcon::All, true);
}

base::unique_qptr<Ui::SideBarButton> FiltersMenu::prepareButton(
		not_null<Ui::VerticalLayout*> container,
		FilterId id,
		const QString &title,
		Ui::FilterIcon icon,
		bool toBeginning) {
	auto prepared = object_ptr<Ui::SideBarButton>(
		container,
		id ? title : tr::lng_filters_all(tr::now),
		st::windowFiltersButton);
	auto added = toBeginning
		? container->insert(0, std::move(prepared))
		: container->add(std::move(prepared));
	auto button = base::unique_qptr<Ui::SideBarButton>(std::move(added));
	const auto raw = button.get();
	const auto &icons = Ui::LookupFilterIcon(id
		? icon
		: Ui::FilterIcon::All);
	raw->setIconOverride(icons.normal, icons.active);
	if (id >= 0) {
		rpl::combine(
			UnreadStateValue(&_session->session(), id),
			_includeMuted.value()
		) | rpl::start_with_next([=](
				const Dialogs::UnreadState &state,
				bool includeMuted) {
			const auto muted = (state.chatsMuted + state.marksMuted);
			const auto count = (state.chats + state.marks)
				- (includeMuted ? 0 : muted);
			const auto string = !count
				? QString()
				: (count > 99)
				? "99+"
				: QString::number(count);
			raw->setBadge(string, includeMuted && (count == muted));
		}, raw->lifetime());
	}
	raw->setActive(_session->activeChatsFilterCurrent() == id);
	raw->setClickedCallback([=] {
		if (_reordering) {
			return;
		} else if (raw->locked()) {
			_session->show(Box(
				FiltersLimitBox,
				&_session->session(),
				std::nullopt));
		} else if (id >= 0) {
			_session->setActiveChatsFilter(id);
		} else {
			openFiltersSettings();
		}
	});
	if (id >= 0) {
		raw->setAcceptDrops(true);
		raw->events(
		) | rpl::filter([=](not_null<QEvent*> e) {
			return ((e->type() == QEvent::ContextMenu) && (id >= 0))
				|| e->type() == QEvent::DragEnter
				|| e->type() == QEvent::DragMove
				|| e->type() == QEvent::DragLeave;
		}) | rpl::start_with_next([=](not_null<QEvent*> e) {
			if (raw->locked()) {
				return;
			}
			if (e->type() == QEvent::ContextMenu) {
				showMenu(QCursor::pos(), id);
			} else if (e->type() == QEvent::DragEnter) {
				using namespace Storage;
				const auto d = static_cast<QDragEnterEvent*>(e.get());
				const auto data = d->mimeData();
				if (ComputeMimeDataState(data) != MimeDataState::None) {
					_drag.timer.callOnce(ChoosePeerByDragTimeout);
					_drag.filterId = id;
					d->setDropAction(Qt::CopyAction);
					d->accept();
				}
			} else if (e->type() == QEvent::DragMove) {
				_drag.timer.callOnce(ChoosePeerByDragTimeout);
			} else if (e->type() == QEvent::DragLeave) {
				_drag.filterId = FilterId(-1);
				_drag.timer.cancel();
			}
		}, raw->lifetime());
	}
	return button;
}

void FiltersMenu::openFiltersSettings() {
	const auto filters = &_session->session().data().chatsFilters();
	if (filters->suggestedLoaded()) {
		_session->showSettings(Settings::Folders::Id());
	} else if (!_waitingSuggested) {
		_waitingSuggested = true;
		filters->requestSuggested();
		filters->suggestedUpdated(
		) | rpl::take(1) | rpl::start_with_next([=] {
			_session->showSettings(Settings::Folders::Id());
		}, _outer.lifetime());
	}
}

void FiltersMenu::showMenu(QPoint position, FilterId id) {
	if (_popupMenu) {
		_popupMenu = nullptr;
		return;
	}
	const auto i = _filters.find(id);
	if ((i == end(_filters)) && id) {
		return;
	}
	_popupMenu = base::make_unique_q<Ui::PopupMenu>(
		i->second.get(),
		st::popupMenuWithIcons);
	const auto addAction = Window::PeerMenuCallback([&](
			Window::PeerMenuCallback::Args args) {
		return _popupMenu->addAction(
			args.text,
			crl::guard(&_outer, std::move(args.handler)),
			args.icon);
	});

	if (id) {
		addAction(
			tr::lng_filters_context_edit(tr::now),
			[=] { showEditBox(id); },
			&st::menuIconEdit);

		auto filteredChats = [=] {
			return _session->session().data().chatsFilters().chatsList(id);
		};
		Window::MenuAddMarkAsReadChatListAction(
			_session,
			std::move(filteredChats),
			addAction);

		addAction(
			tr::lng_filters_context_remove(tr::now),
			[=] { showRemoveBox(id); },
			&st::menuIconDelete);
	} else {
		auto customUnreadState = [=] {
			const auto session = &_session->session();
			return MainListMapUnreadState(
				session,
				session->data().chatsList()->unreadState());
		};
		Window::MenuAddMarkAsReadChatListAction(
			_session,
			[=] { return _session->session().data().chatsList(); },
			addAction,
			std::move(customUnreadState));

		addAction(
			tr::lng_filters_setup_menu(tr::now),
			[=] { openFiltersSettings(); },
			&st::menuIconEdit);
	}
	if (_popupMenu->empty()) {
		_popupMenu = nullptr;
		return;
	}
	_popupMenu->popup(position);
}

void FiltersMenu::showEditBox(FilterId id) {
	EditExistingFilter(_session, id);
}

void FiltersMenu::showRemoveBox(FilterId id) {
	const auto session = &_session->session();
	const auto &list = session->data().chatsFilters().list();
	const auto i = ranges::find(list, id, &Data::ChatFilter::id);
	const auto filter = (i != end(list)) ? *i : Data::ChatFilter();
	const auto has = filter.hasMyLinks();
	const auto confirm = [=](Fn<void()> action, bool onlyWhenHas = false) {
		if (!has && onlyWhenHas) {
			action();
			return;
		}
		_session->window().show(Ui::MakeConfirmBox({
			.text = (has
				? tr::lng_filters_delete_sure()
				: tr::lng_filters_remove_sure()),
			.confirmed = [=](Fn<void()> &&close) { close(); action(); },
			.confirmText = (has
				? tr::lng_box_delete()
				: tr::lng_filters_remove_yes()),
			.confirmStyle = &st::attentionBoxButton,
		}));
	};
	const auto simple = [=] {
		confirm([=] { remove(id); });
	};
	const auto suggestRemoving = Api::ExtractSuggestRemoving(filter);
	if (suggestRemoving.empty()) {
		simple();
		return;
	} else if (_removingRequestId) {
		if (_removingId == id) {
			return;
		}
		session->api().request(_removingRequestId).cancel();
	}
	_removingId = id;
	_removingRequestId = session->api().request(
		MTPchatlists_GetLeaveChatlistSuggestions(
			MTP_inputChatlistDialogFilter(
				MTP_int(id)))
	).done(crl::guard(&_outer, [=](const MTPVector<MTPPeer> &result) {
		_removingRequestId = 0;
		const auto suggestRemovePeers = ranges::views::all(
			result.v
		) | ranges::views::transform([=](const MTPPeer &peer) {
			return session->data().peer(peerFromMTP(peer));
		}) | ranges::to_vector;
		const auto chosen = crl::guard(&_outer, [=](
				std::vector<not_null<PeerData*>> peers) {
			remove(id, std::move(peers));
		});
		confirm(crl::guard(&_outer, [=] {
			Api::ProcessFilterRemove(
				_session,
				filter.title(),
				filter.iconEmoji(),
				suggestRemoving,
				suggestRemovePeers,
				chosen);
		}), true);
	})).fail(crl::guard(&_outer, [=] {
		_removingRequestId = 0;
		simple();
	})).send();
}

void FiltersMenu::remove(
		FilterId id,
		std::vector<not_null<PeerData*>> leave) {
	const auto session = &_session->session();
	const auto api = &session->api();
	session->data().chatsFilters().apply(MTP_updateDialogFilter(
		MTP_flags(MTPDupdateDialogFilter::Flag(0)),
		MTP_int(id),
		MTPDialogFilter()));
	if (leave.empty()) {
		api->request(MTPmessages_UpdateDialogFilter(
			MTP_flags(MTPmessages_UpdateDialogFilter::Flag(0)),
			MTP_int(id),
			MTPDialogFilter()
		)).send();
	} else {
		api->request(MTPchatlists_LeaveChatlist(
			MTP_inputChatlistDialogFilter(MTP_int(id)),
			MTP_vector<MTPInputPeer>(ranges::views::all(
				leave
			) | ranges::views::transform([](not_null<PeerData*> peer) {
				return MTPInputPeer(peer->input);
			}) | ranges::to<QVector<MTPInputPeer>>())
		)).done([=](const MTPUpdates &result) {
			api->applyUpdates(result);
		}).send();
	}
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
	if (!premium()) {
		if (list[0].id() != FilterId()) {
			filters->moveAllToFront();
		}
	}
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
