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
#include "window/window_filters_favorite.h"
#include "main/main_session.h"
#include "base/event_filter.h"
#include "base/options.h"
#include "core/ui_integration.h"
#include "data/data_session.h"
#include "data/data_chat_filters.h"
#include "data/data_user.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "data/data_unread_value.h"
#include "lang/lang_keys.h"
#include "ui/filter_icons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/boxes/confirm_box.h"
#include "ui/power_saving.h"
#include "ui/screen_reader_mode.h"
#include "ui/ui_utility.h"
#include "boxes/filters/edit_filter_box.h"
#include "boxes/choose_filter_box.h"
#include "boxes/premium_limits_box.h"
#include "settings/sections/settings_folders.h"
#include "storage/storage_media_prepare.h"
#include "api/api_chat_filters.h"
#include "apiwrap.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "styles/style_layers.h" // attentionBoxButton
#include "styles/style_menu_icons.h"

#include <QtGui/QtEvents>

namespace Window {
namespace {

// The folder tabs container, exposed as a list to screen readers.
class TabListLayout final : public Ui::VerticalLayout {
public:
	using Ui::VerticalLayout::VerticalLayout;

	QAccessible::Role accessibilityRole() override {
		return QAccessible::List;
	}
	Qt::FocusPolicy accessibilityFocusPolicy() override {
		// Let the accessibility layer decide focusability (like PopupMenu),
		// exposing the list as focusable in screen-reader mode.
		return Qt::ClickFocus;
	}
	std::optional<Qt::Orientation> accessibilityOrientation() const override {
		// The folders strip is a vertically stacked list.
		return Qt::Vertical;
	}
	bool accessibilitySelectionList() const override {
		// Opt in to the single-selection list behaviour (selection interface +
		// container focus forwarding to the active folder). Other List-role
		// widgets (e.g. message history) must not get this.
		return true;
	}
	std::vector<not_null<QWidget*>> accessibilityChildWidgets() const override {
		// Report the tab buttons in visual (row) order, which can differ from
		// the QObject child order after a drag-reorder. This override lives
		// here, on the one VerticalLayout that exposes an accessibility role,
		// rather than in the base class: a role-less VerticalLayout gets no
		// custom accessible interface, so it would never call this anyway, and
		// the widely-used base type keeps Qt's default child enumeration.
		auto result = std::vector<not_null<QWidget*>>();
		const auto rows = count();
		result.reserve(rows);
		for (auto i = 0; i != rows; ++i) {
			result.push_back(widgetAt(i).get());
		}
		return result;
	}
};

} // namespace

FiltersMenu::FiltersMenu(
	not_null<Ui::RpWidget*> parent,
	not_null<SessionController*> session)
: _session(session)
, _parent(parent)
, _outer(_parent)
, _menu(&_outer, TextWithEntities(), st::windowFiltersMainMenu)
, _scroll(&_outer)
, _container(
	_scroll.setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(&_scroll))) {

	_drag.timer.setCallback([=] {
		if (_drag.filterId >= 0) {
			_session->setActiveChatsFilter(_drag.filterId);
		}
	});
	setup();
}

FiltersMenu::~FiltersMenu() = default;

void FiltersMenu::setup() {
	setupDragAndDrop();
	setupMainMenuIcon();
	_menu.setIsMenuButton(true);
	_menu.setAccessibleName(tr::lng_main_menu(tr::now));

	_outer.setAttribute(Qt::WA_OpaquePaintEvent);
	_outer.show();
	_outer.paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(&_outer);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowFiltersButton.textBg);
		p.drawRect(clip);
	}, _outer.lifetime());

	_parent->heightValue(
	) | rpl::on_next([=](int height) {
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
	) | rpl::on_next([=] {
		refresh();
	}, _outer.lifetime());

	_activeFilterId = _session->activeChatsFilterCurrent();
	_session->activeChatsFilter(
	) | rpl::filter([=](FilterId id) {
		return (id != _activeFilterId);
	}) | rpl::on_next([=](FilterId id) {
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

void FiltersMenu::setupDragAndDrop() {
	SetupFilterDragAndDrop(
		&_outer,
		&_session->session(),
		[=](QPoint globalPos) -> std::optional<FilterId> {
			if (!_list) {
				return std::nullopt;
			}
			const auto localPos = _list->mapFromGlobal(globalPos);
			for (const auto &[id, button] : _filters) {
				if (button->geometry().contains(localPos)) {
					return id;
				}
			}
			return std::nullopt;
		},
		[=] { return _activeFilterId; },
		[=](FilterId filterId) {
			for (const auto &[id, button] : _filters) {
				button->setForceRippled(id == filterId);
			}
		});
}

void FiltersMenu::setupMainMenuIcon() {
	OtherAccountsUnreadState(
		&_session->session().account()
	) | rpl::on_next([=](const OthersUnreadState &state) {
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

void FiltersMenu::applyFilterAt(int start, int delta) {
	const auto &list = _session->session().data().chatsFilters().list();
	const auto count = int(list.size());
	// Move focus to the folder at `start`, then in the `delta` direction,
	// stopping at the bounds (no wrap). Arrow keys only move focus; activation
	// (switching the chat list, or opening the Premium box for a locked folder)
	// happens when the user presses Enter on the focused folder.
	for (auto index = start; index >= 0 && index < count; index += delta) {
		const auto i = _filters.find(list[index].id());
		if (i != end(_filters)) {
			const auto raw = i->second.get();
			// Just move focus; the FocusIn handler makes this the list's single
			// Tab-stop (setListTabStop). Arrows only move focus, so - unlike the
			// old activate-on-arrow path - nothing scrolls the focused folder
			// into view; do that explicitly.
			raw->setFocus();
			scrollToButton(raw);
			return;
		}
	}
}

void FiltersMenu::moveToFilter(int delta) {
	const auto &list = _session->session().data().chatsFilters().list();
	const auto count = int(list.size());
	// Move relative to the currently focused folder, so navigation continues
	// from a locked one (which only takes focus, without becoming active); fall
	// back to the active folder when nothing is focused.
	auto current = 0;
	for (auto i = 0; i != count; ++i) {
		const auto it = _filters.find(list[i].id());
		if (it != end(_filters) && it->second->hasFocus()) {
			current = i;
			break;
		} else if (list[i].id() == _activeFilterId) {
			current = i;
		}
	}
	applyFilterAt(current + delta, delta);
}

void FiltersMenu::moveToFilterEdge(int delta) {
	const auto count = int(
		_session->session().data().chatsFilters().list().size());
	applyFilterAt((delta > 0) ? 0 : (count - 1), delta);
}

void FiltersMenu::setListTabStop(not_null<Ui::SideBarButton*> stop) {
	// Single source of truth for the list's roving Tab-stop, wired between the
	// main menu and the edit button. Promote `stop` to the only TabFocus item
	// and demote the previous one, so Tab/Shift+Tab always leave the list the
	// same way regardless of which folder is focused.
	if (const auto previous = _tabStop.get(); previous && previous != stop) {
		previous->setFocusPolicy(Qt::ClickFocus);
	}
	stop->setFocusPolicy(Qt::TabFocus);
	_tabStop = stop.get();
	QWidget::setTabOrder(&_menu, stop.get());
	const auto favorite = _favorite ? _favorite->entity() : nullptr;
	if (favorite) {
		QWidget::setTabOrder(stop.get(), favorite);
		QWidget::setTabOrder(favorite, _setup.get());
	} else {
		QWidget::setTabOrder(stop.get(), _setup.get());
	}
}

bool FiltersMenu::listFocused() const {
	for (const auto &[id, button] : _filters) {
		if (button->hasFocus()) {
			return true;
		}
	}
	return false;
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

	// Remember which folder holds keyboard focus so the roving Tab-stop can be
	// re-established on its replacement after the rebuild: the new buttons are
	// constructed while _filters still holds the old ones, so their own seeding
	// can't see the focus and would leave the list with no Tab-stop.
	auto focusedId = std::optional<FilterId>();
	for (const auto &[id, button] : _filters) {
		if (button->hasFocus()) {
			focusedId = id;
			break;
		}
	}

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
			Ui::ComputeFilterIcon(filter),
			nextIsLocked);
		now.emplace(filter.id(), std::move(button));
	}
	_filters = std::move(now);
	// Re-establish the list's Tab-stop on the folder that was focused (if it
	// survived the rebuild), else on the active one, so a refresh - rename,
	// deletion, Premium-state change - never leaves the list without a Tab-stop.
	auto refocus = (Ui::SideBarButton*)nullptr;
	if (Ui::ScreenReaderModeActive()) {
		auto i = focusedId ? _filters.find(*focusedId) : end(_filters);
		if (i == end(_filters)) {
			i = _filters.find(_activeFilterId);
		}
		if (i != end(_filters)) {
			setListTabStop(i->second.get());
			// setListTabStop only fixes the Tab order; the std::move above
			// destroyed the focused button, so Qt also dropped keyboard focus
			// (to the menu, the edit button or nowhere). If a folder held it,
			// restore focus to the replacement - or the active fallback - below,
			// once the scroll is restored, so it lands focused and visible and a
			// screen reader keeps reading a folder rather than where focus fell.
			if (focusedId) {
				refocus = i->second.get();
			}
		}
	}
	_reorder->start();

	_container->resizeToWidth(_outer.width());

	// After the filters are refreshed, the scroll is reset,
	// so we have to restore it.
	_scroll.scrollToY(oldTop);

	if (refocus) {
		refocus->setFocus();
		scrollToButton(refocus);
	}
}

void FiltersMenu::setupList() {
	_list = _container->add(object_ptr<TabListLayout>(_container));
	_list->setAccessibleName(tr::lng_filters_title(tr::now));
	_setup = prepareButton(
		_container,
		-1,
		{ TextWithEntities{ tr::lng_filters_setup(tr::now) } },
		Ui::FilterIcon::Edit);
	_reorder = std::make_unique<Ui::VerticalLayoutReorder>(_list, &_scroll);

	_reorder->updates(
	) | rpl::on_next([=](Ui::VerticalLayoutReorder::Single data) {
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

	base::options::lookup<QString>(kOptionFolderFavoriteLink).changes(
	) | rpl::on_next([=] {
		updateFavorite();
	}, _outer.lifetime());
	updateFavorite();
}

void FiltersMenu::updateFavorite() {
	const auto link = base::options::lookup<QString>(
		kOptionFolderFavoriteLink).value().trimmed();
	if (link.isEmpty()) {
		if (_favorite && _favorite->toggled()) {
			// Animate out; the finished callback drops it afterwards.
			_favorite->toggle(false, anim::type::normal);
		} else if (_favorite) {
			// Hidden already (e.g. still resolving), so nothing to animate.
			destroyFavorite();
		}
		return;
	}
	if (!_favorite) {
		createFavorite();
	}
	const auto button = _favorite->entity();
	button->setLink(link);
	if (button->shown() && !_favorite->toggled()) {
		_favorite->toggle(true, anim::type::normal);
	}
}

void FiltersMenu::createFavorite() {
	_favorite = base::unique_qptr<Ui::SlideWrap<FolderFavoriteButton>>(
		_container->insert(
			_container->count() - 1,
			object_ptr<Ui::SlideWrap<FolderFavoriteButton>>(
				_container,
				object_ptr<FolderFavoriteButton>(
					_container,
					_session,
					st::windowFiltersButton))));
	_favorite->toggle(false, anim::type::instant);
	_favorite->setFinishedCallback([=] {
		if (_favorite && !_favorite->toggled()) {
			destroyFavorite();
		}
	});
	_favorite->entity()->shownValue(
	) | rpl::on_next([=](bool shown) {
		if (_favorite && shown) {
			_favorite->toggle(true, anim::type::normal);
		}
	}, _favorite->lifetime());
}

void FiltersMenu::destroyFavorite() {
	Ui::PostponeCall(&_outer, [=] {
		const auto empty = base::options::lookup<QString>(
			kOptionFolderFavoriteLink).value().trimmed().isEmpty();
		if (_favorite && !_favorite->toggled() && empty) {
			_favorite = nullptr;
		}
	});
}

bool FiltersMenu::premium() const {
	return _session->session().user()->isPremium();
}

base::unique_qptr<Ui::SideBarButton> FiltersMenu::prepareAll() {
	return prepareButton(
		_container,
		0,
		{},
		Ui::FilterIcon::All,
		false,
		true);
}

base::unique_qptr<Ui::SideBarButton> FiltersMenu::prepareButton(
		not_null<Ui::VerticalLayout*> container,
		FilterId id,
		Data::ChatFilterTitle title,
		Ui::FilterIcon icon,
		bool locked,
		bool toBeginning) {
	const auto isStatic = title.isStatic;
	const auto paused = [=] {
		return On(PowerSaving::kEmojiChat)
			|| _session->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
	};
	// A real folder (id >= 0), locked or not, is a selectable list item; only
	// the "Edit" button (id < 0) stays a plain button. Establish this before
	// inserting the widget - insertion shows the child immediately, so
	// configuring the role up front avoids a transient or separately-announced
	// role change.
	const auto listItem = (id >= 0);
	auto prepared = object_ptr<Ui::SideBarButton>(
		container,
		id ? title.text : TextWithEntities{ tr::lng_filters_all(tr::now) },
		st::windowFiltersButton,
		Core::TextContext({
			.session = &_session->session(),
			.customEmojiLoopLimit = isStatic ? -1 : 0,
		}),
		paused);
	prepared->setLocked(locked);
	prepared->setIsListItem(listItem);
	auto added = toBeginning
		? container->insert(0, std::move(prepared))
		: container->add(std::move(prepared));
	auto button = base::unique_qptr<Ui::SideBarButton>(std::move(added));
	const auto raw = button.get();
	const auto nameText = id
		? title.text.text
		: tr::lng_filters_all(tr::now);
	const auto &icons = Ui::LookupFilterIcon(id
		? icon
		: Ui::FilterIcon::All);
	raw->setIconOverride(icons.normal, icons.active);
	if (id >= 0) {
		if (locked) {
			// Surface a locked folder's premium-gated status and what pressing
			// it does, which the visual lock glyph alone can't convey to a
			// screen reader.
			raw->setAccessibleName(
				tr::lng_sr_folder_locked(tr::now, lt_text, nameText));
			raw->setAccessibleDescription(
				tr::lng_sr_folder_locked_about(tr::now));
		}
		rpl::combine(
			Data::UnreadStateValue(&_session->session(), id),
			Data::IncludeMutedCounterFoldersValue()
		) | rpl::on_next([=](
				const Dialogs::UnreadState &state,
				bool includeMuted) {
			const auto chats = state.chats;
			const auto chatsMuted = state.chatsMuted;
			const auto muted = (chatsMuted + state.marksMuted);
			const auto count = (chats + state.marks)
				- (includeMuted ? 0 : muted);
			const auto string = !count
				? QString()
				: (count > 999)
				? "99+"
				: QString::number(count);
			raw->setBadge(string, includeMuted && (count == muted));
			if (!locked) {
				raw->setAccessibleName(count
					? tr::lng_filter_unread_chats(
						tr::now,
						lt_count,
						count,
						lt_text,
						nameText)
					: nameText);
			}
		}, raw->lifetime());
	}
	if (listItem) {
		// The list has one roving Tab-stop and it follows keyboard focus (the
		// FocusIn handler below makes the focused folder the single Tab-stop via
		// setListTabStop). Here we only track screen-reader mode - which toggles
		// whether the items are focusable at all - and seed the Tab-stop on the
		// active folder, so Tab reaches the list there while nothing is focused.
		rpl::combine(
			Ui::ScreenReaderModeActiveValue(),
			rpl::single(
				_session->activeChatsFilterCurrent()
			) | rpl::then(
				_session->activeChatsFilter()
			) | rpl::map([=](FilterId active) {
				return (active == id);
			}) | rpl::distinct_until_changed()
		) | rpl::on_next([=](bool screenReaderActive, bool selected) {
			if (!screenReaderActive) {
				raw->setFocusPolicy(Qt::NoFocus);
			} else if (selected && !listFocused()) {
				setListTabStop(raw);
			} else if (raw->focusPolicy() == Qt::NoFocus) {
				raw->setFocusPolicy(Qt::ClickFocus);
			}
		}, raw->lifetime());
		// Up/Down move focus to the previous/next folder, Home/End to the
		// first/last one (the items are only focusable in screen-reader mode, so
		// this is scoped to it). Activation - switching the chat list, or opening
		// the Premium box for a locked folder - happens on Enter. Focusing an
		// item (by arrow, mouse or UIA SetFocus) makes it the list's Tab-stop.
		base::install_event_filter(raw, [=](not_null<QEvent*> event) {
			if (event->type() == QEvent::FocusIn) {
				setListTabStop(raw);
				return base::EventFilterResult::Continue;
			} else if (event->type() != QEvent::KeyPress) {
				return base::EventFilterResult::Continue;
			}
			switch (static_cast<QKeyEvent*>(event.get())->key()) {
			case Qt::Key_Up: moveToFilter(-1); break;
			case Qt::Key_Down: moveToFilter(1); break;
			case Qt::Key_Home: moveToFilterEdge(1); break;
			case Qt::Key_End: moveToFilterEdge(-1); break;
			default: return base::EventFilterResult::Continue;
			}
			return base::EventFilterResult::Cancel;
		});
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
		}) | rpl::on_next([=](not_null<QEvent*> e) {
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
		_session->showSettings(Settings::FoldersId());
	} else if (!_waitingSuggested) {
		_waitingSuggested = true;
		filters->requestSuggested();
		filters->suggestedUpdated(
		) | rpl::take(1) | rpl::on_next([=] {
			_session->showSettings(Settings::FoldersId());
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
	const auto addAction = Ui::Menu::CreateAddActionCallback(_popupMenu);
	if (id) {
		addAction(
			tr::lng_filters_context_edit(tr::now),
			crl::guard(&_outer, [=] { EditExistingFilter(_session, id); }),
			&st::menuIconEdit);

		auto filteredChats = [=] {
			return _session->session().data().chatsFilters().chatsList(id);
		};
		Window::MenuAddMarkAsReadChatListAction(
			_session,
			std::move(filteredChats),
			addAction);

		addAction({
			.text = tr::lng_filters_context_remove(tr::now),
			.handler = crl::guard(&_outer, [=, this] {
				_removeApi.request(base::make_weak(&_outer), _session, id);
			}),
			.icon = &st::menuIconDeleteAttention,
			.isAttention = true,
		});
	} else {
		auto customUnreadState = [=] {
			const auto session = &_session->session();
			return Data::MainListMapUnreadState(
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
			crl::guard(&_outer, [=] { openFiltersSettings(); }),
			&st::menuIconEdit);
	}
	if (_popupMenu->empty()) {
		_popupMenu = nullptr;
		return;
	}
	_popupMenu->popup(position);
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
