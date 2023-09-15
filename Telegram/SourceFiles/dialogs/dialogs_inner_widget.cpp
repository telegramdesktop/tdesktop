/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_inner_widget.h"

#include "dialogs/dialogs_three_state_icon.h"
#include "dialogs/ui/dialogs_layout.h"
#include "dialogs/ui/dialogs_stories_content.h"
#include "dialogs/ui/dialogs_stories_list.h"
#include "dialogs/ui/dialogs_video_userpic.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_widget.h"
#include "dialogs/dialogs_search_from_controllers.h"
#include "history/history.h"
#include "history/history_item.h"
#include "core/shortcuts.h"
#include "core/application.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "data/data_drafts.h"
#include "data/data_folder.h"
#include "data/data_forum.h"
#include "data/data_forum_icons.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_peer_values.h"
#include "data/data_histories.h"
#include "data/data_chat_filters.h"
#include "data/data_cloud_file.h"
#include "data/data_changes.h"
#include "data/data_stories.h"
#include "data/stickers/data_stickers.h"
#include "data/data_send_action.h"
#include "base/unixtime.h"
#include "base/options.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "storage/storage_account.h"
#include "apiwrap.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "window/notifications_manager.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/empty_userpic.h"
#include "ui/unread_badge.h"
#include "boxes/filters/edit_filter_box.h"
#include "boxes/peers/edit_forum_topic_box.h"
#include "api/api_chat_filters.h"
#include "base/qt/qt_common_adapters.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat.h" // popupMenuExpandedSeparator
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"
#include "styles/style_menu_icons.h"

namespace Dialogs {
namespace {

constexpr auto kHashtagResultsLimit = 5;
constexpr auto kStartReorderThreshold = 30;

int FixedOnTopDialogsCount(not_null<Dialogs::IndexedList*> list) {
	auto result = 0;
	for (const auto &row : *list) {
		if (!row->entry()->fixedOnTopIndex()) {
			break;
		}
		++result;
	}
	return result;
}

int PinnedDialogsCount(
		FilterId filterId,
		not_null<Dialogs::IndexedList*> list) {
	auto result = 0;
	for (const auto &row : *list) {
		if (row->entry()->fixedOnTopIndex()) {
			continue;
		} else if (!row->entry()->isPinnedDialog(filterId)) {
			break;
		}
		++result;
	}
	return result;
}

} // namespace

struct InnerWidget::CollapsedRow {
	CollapsedRow(Data::Folder *folder) : folder(folder) {
	}

	Data::Folder *folder = nullptr;
	BasicRow row;
};

struct InnerWidget::HashtagResult {
	HashtagResult(const QString &tag) : tag(tag) {
	}
	QString tag;
	BasicRow row;
};

struct InnerWidget::PeerSearchResult {
	explicit PeerSearchResult(not_null<PeerData*> peer) : peer(peer) {
	}
	not_null<PeerData*> peer;
	mutable Ui::Text::String name;
	mutable Ui::PeerBadge badge;
	BasicRow row;
};

Key InnerWidget::FilterResult::key() const {
	return row->key();
}

int InnerWidget::FilterResult::bottom() const {
	return top + row->height();
}

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	rpl::producer<ChildListShown> childListShown)
: RpWidget(parent)
, _controller(controller)
, _shownList(controller->session().data().chatsList()->indexed())
, _st(&st::defaultDialogRow)
, _pinnedShiftAnimation([=](crl::time now) {
	return pinnedShiftAnimationCallback(now);
})
, _narrowWidth(st::defaultDialogRow.padding.left()
	+ st::defaultDialogRow.photoSize
	+ st::defaultDialogRow.padding.left())
, _cancelSearchInChat(this, st::dialogsCancelSearchInPeer)
, _cancelSearchFromUser(this, st::dialogsCancelSearchInPeer)
, _childListShown(std::move(childListShown)) {
	setAttribute(Qt::WA_OpaquePaintEvent, true);

	_cancelSearchInChat->hide();
	_cancelSearchFromUser->hide();

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_topicJumpCache = nullptr;
	}, lifetime());

	session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	Core::App().notifications().settingsChanged(
	) | rpl::start_with_next([=](Window::Notifications::ChangeType change) {
		if (change == Window::Notifications::ChangeType::CountMessages) {
			// Folder rows change their unread badge with this setting.
			update();
		}
	}, lifetime());

	session().data().contactsLoaded().changes(
	) | rpl::start_with_next([=] {
		refresh();
		refreshEmptyLabel();
	}, lifetime());

	session().data().itemRemoved(
	) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		itemRemoved(item);
	}, lifetime());

	session().data().dialogsRowReplacements(
	) | rpl::start_with_next([=](Data::Session::DialogsRowReplacement r) {
		dialogRowReplaced(r.old, r.now);
	}, lifetime());

	session().data().sendActionManager().animationUpdated(
	) | rpl::start_with_next([=](
			const Data::SendActionManager::AnimationUpdate &update) {
		const auto updateRect = Ui::RowPainter::SendActionAnimationRect(
			_st,
			update.left,
			update.width,
			update.height,
			width(),
			update.textUpdated);
		updateDialogRow(
			RowDescriptor(update.thread, FullMsgId()),
			updateRect,
			UpdateRowSection::Default | UpdateRowSection::Filtered);
	}, lifetime());

	session().data().sendActionManager().speakingAnimationUpdated(
	) | rpl::start_with_next([=](not_null<History*> history) {
		repaintDialogRowCornerStatus(history);
	}, lifetime());

	setupOnlineStatusCheck();

	rpl::merge(
		session().data().chatsListChanges(),
		session().data().chatsListLoadedEvents()
	) | rpl::filter([=](Data::Folder *folder) {
		return !_openedForum && (folder == _openedFolder);
	}) | rpl::start_with_next([=] {
		refresh();
	}, lifetime());

	rpl::merge(
		session().settings().archiveCollapsedChanges() | rpl::to_empty,
		session().data().chatsFilters().changed()
	) | rpl::start_with_next([=] {
		refreshWithCollapsedRows();
	}, lifetime());

	session().settings().archiveInMainMenuChanges(
	) | rpl::start_with_next([=] {
		refresh();
	}, lifetime());

	session().changes().historyUpdates(
		Data::HistoryUpdate::Flag::IsPinned
		| Data::HistoryUpdate::Flag::ChatOccupied
	) | rpl::start_with_next([=](const Data::HistoryUpdate &update) {
		if (update.flags & Data::HistoryUpdate::Flag::IsPinned) {
			stopReorderPinned();
		}
		if (update.flags & Data::HistoryUpdate::Flag::ChatOccupied) {
			this->update();
			_updated.fire({});
		}
	}, lifetime());

	using UpdateFlag = Data::PeerUpdate::Flag;
	session().changes().peerUpdates(
		UpdateFlag::Name
		| UpdateFlag::Photo
		| UpdateFlag::IsContact
		| UpdateFlag::FullInfo
		| UpdateFlag::EmojiStatus
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		if (update.flags
			& (UpdateFlag::Name
				| UpdateFlag::Photo
				| UpdateFlag::FullInfo
				| UpdateFlag::EmojiStatus)) {
			const auto peer = update.peer;
			const auto history = peer->owner().historyLoaded(peer);
			if (_state == WidgetState::Default) {
				if (history) {
					updateDialogRow({ history, FullMsgId() });
				}
			} else {
				this->update();
			}
			_updated.fire({});
		}
		if (update.flags & UpdateFlag::IsContact) {
			// contactsNoChatsList could've changed.
			Ui::PostponeCall(this, [=] { refresh(); });
		}
	}, lifetime());

	session().changes().messageUpdates(
		Data::MessageUpdate::Flag::DialogRowRefresh
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		refreshDialogRow({ update.item->history(), update.item->fullId() });
	}, lifetime());

	session().changes().entryUpdates(
		Data::EntryUpdate::Flag::Repaint
		| Data::EntryUpdate::Flag::Height
	) | rpl::start_with_next([=](const Data::EntryUpdate &update) {
		const auto entry = update.entry;
		if (update.flags & Data::EntryUpdate::Flag::Height) {
			if (updateEntryHeight(entry)) {
				refresh();
			}
			return;
		}
		const auto repaintId = (_state == WidgetState::Default)
			? _filterId
			: 0;
		if (const auto links = entry->chatListLinks(repaintId)) {
			repaintDialogRow(repaintId, links->main);
		}
		if (session().supportMode()
			&& !session().settings().supportAllSearchResults()) {
			repaintDialogRow({ entry, FullMsgId() });
		}
	}, lifetime());

	_controller->activeChatEntryValue(
	) | rpl::combine_previous(
	) | rpl::start_with_next([=](
			RowDescriptor previous,
			RowDescriptor next) {
		updateDialogRow(previous);
		updateDialogRow(next);
	}, lifetime());

	_controller->activeChatsFilter(
	) | rpl::start_with_next([=](FilterId filterId) {
		switchToFilter(filterId);
	}, lifetime());

	session().data().stories().incrementPreloadingMainSources();

	handleChatListEntryRefreshes();

	refreshWithCollapsedRows(true);

	setupShortcuts();
}

bool InnerWidget::updateEntryHeight(not_null<Entry*> entry) {
	if (!_geometryInited) {
		return false;
	}
	auto changing = false;
	auto top = 0;
	for (auto &result : _filterResults) {
		if (changing) {
			result.top = top;
		}
		if (result.row->key().entry() == entry) {
			result.row->recountHeight(_narrowRatio);
			changing = true;
			top = result.top;
		}
		if (changing) {
			top += result.row->height();
		}
	}
	return _shownList->updateHeight(entry, _narrowRatio) || changing;
}

void InnerWidget::setNarrowRatio(float64 narrowRatio) {
	if (_geometryInited && _narrowRatio == narrowRatio) {
		return;
	}
	_geometryInited = true;
	_narrowRatio = narrowRatio;
	if (_shownList->updateHeights(_narrowRatio) || !height()) {
		refresh();
	}
}

Main::Session &InnerWidget::session() const {
	return _controller->session();
}

void InnerWidget::refreshWithCollapsedRows(bool toTop) {
	const auto pressed = _collapsedPressed;
	const auto selected = _collapsedSelected;

	setCollapsedPressed(-1);
	_collapsedSelected = -1;

	_collapsedRows.clear();
	const auto archive = !_shownList->empty()
		? _shownList->begin()->get()->folder()
		: nullptr;
	const auto inMainMenu = session().settings().archiveInMainMenu();
	if (archive && (session().settings().archiveCollapsed() || inMainMenu)) {
		if (_selected && _selected->folder() == archive) {
			_selected = nullptr;
		}
		if (_pressed && _pressed->folder() == archive) {
			clearPressed();
		}
		_skipTopDialog = true;
		if (!inMainMenu && !_filterId) {
			_collapsedRows.push_back(
				std::make_unique<CollapsedRow>(archive));
		}
	} else {
		_skipTopDialog = false;
	}

	Assert(!needCollapsedRowsRefresh());
	refresh(toTop);

	if (selected >= 0 && selected < _collapsedRows.size()) {
		_collapsedSelected = selected;
	}
	if (pressed >= 0 && pressed < _collapsedRows.size()) {
		setCollapsedPressed(pressed);
	}
}

int InnerWidget::skipTopHeight() const {
	return (_skipTopDialog && !_shownList->empty())
		? _shownList->begin()->get()->height()
		: 0;
}

int InnerWidget::collapsedRowsOffset() const {
	return 0;
}

int InnerWidget::dialogsOffset() const {
	return collapsedRowsOffset()
		+ (_collapsedRows.size() * st::dialogsImportantBarHeight)
		- skipTopHeight();
}

int InnerWidget::fixedOnTopCount() const {
	auto result = 0;
	for (const auto &row : *_shownList) {
		if (row->entry()->fixedOnTopIndex()) {
			++result;
		} else {
			break;
		}
	}
	return result;
}

int InnerWidget::shownHeight(int till) const {
	return !till
		? 0
		: (till > 0 && till < _shownList->size())
		? (_shownList->begin() + till)->get()->top()
		: _shownList->height();
}

int InnerWidget::pinnedOffset() const {
	return dialogsOffset() + shownHeight(fixedOnTopCount());
}

int InnerWidget::filteredOffset() const {
	return _hashtagResults.size() * st::mentionHeight;
}

int InnerWidget::filteredIndex(int y) const {
	return ranges::lower_bound(
		_filterResults,
		y,
		ranges::less(),
		&FilterResult::bottom
	) - begin(_filterResults);
}

int InnerWidget::filteredHeight(int till) const {
	return (!till || _filterResults.empty())
		? 0
		: (till > 0 && till < _filterResults.size())
		? _filterResults[till].top
		: (_filterResults.back().top + _filterResults.back().row->height());
}

int InnerWidget::peerSearchOffset() const {
	return filteredOffset()
		+ filteredHeight()
		+ st::searchedBarHeight;
}

int InnerWidget::searchedOffset() const {
	auto result = peerSearchOffset();
	if (!_peerSearchResults.empty()) {
		result += (_peerSearchResults.size() * st::dialogsRowHeight)
			+ st::searchedBarHeight;
	}
	result += searchInChatSkip();
	return result;
}

int InnerWidget::searchInChatSkip() const {
	auto result = 0;
	if (_searchInChat) {
		result += st::searchedBarHeight + st::dialogsSearchInHeight;
	}
	if (_searchFromPeer) {
		if (_searchInChat) {
			result += st::lineWidth;
		}
		result += st::dialogsSearchInHeight;
	}
	return result;
}

void InnerWidget::changeOpenedFolder(Data::Folder *folder) {
	if (_openedFolder == folder) {
		return;
	}
	stopReorderPinned();
	clearSelection();
	_openedFolder = folder;
	refreshShownList();
	refreshWithCollapsedRows(true);
	if (_loadMoreCallback) {
		_loadMoreCallback();
	}
}

void InnerWidget::changeOpenedForum(Data::Forum *forum) {
	if (_openedForum == forum) {
		return;
	}
	stopReorderPinned();
	clearSelection();

	if (forum) {
		saveChatsFilterScrollState(_filterId);
	}
	_filterId = forum
		? 0
		: _controller->activeChatsFilterCurrent();
	if (_openedForum) {
		// If we close it inside forum destruction we should not schedule.
		session().data().forumIcons().scheduleUserpicsReset(_openedForum);
	}
	_openedForum = forum;
	_st = forum ? &st::forumTopicRow : &st::defaultDialogRow;
	refreshShownList();

	_openedForumLifetime.destroy();
	if (forum) {
		rpl::merge(
			forum->chatsListChanges(),
			forum->chatsListLoadedEvents()
		) | rpl::start_with_next([=] {
			refresh();
		}, _openedForumLifetime);
	}

	refreshWithCollapsedRows(true);
	if (_loadMoreCallback) {
		_loadMoreCallback();
	}

	if (!forum) {
		restoreChatsFilterScrollState(_filterId);
	}
}

void InnerWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setInactive(
		_controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any));
	if (_controller->contentOverlapped(this, e)) {
		return;
	}
	const auto activeEntry = _controller->activeChatEntryCurrent();
	const auto videoPaused = _controller->isGifPausedAtLeastFor(
		Window::GifPauseReason::Any);
	auto fullWidth = width();
	const auto r = e->rect();
	auto dialogsClip = r;
	const auto ms = crl::now();
	const auto childListShown = _childListShown.current();
	auto context = Ui::PaintContext{
		.st = _st,
		.topicJumpCache = _topicJumpCache.get(),
		.folder = _openedFolder,
		.forum = _openedForum,
		.currentBg = currentBg(),
		.filter = _filterId,
		.now = ms,
		.width = fullWidth,
		.paused = videoPaused,
		.narrow = (fullWidth < st::columnMinimalWidthLeft / 2),
	};
	const auto fillGuard = gsl::finally([&] {
		// We translate painter down, but it'll be cropped below rect.
		p.fillRect(rect(), context.currentBg);
	});
	const auto paintRow = [&](
			not_null<Row*> row,
			bool selected,
			bool mayBeActive) {
		const auto key = row->key();
		const auto active = mayBeActive && (activeEntry.key == key);
		const auto forum = key.history() && key.history()->isForum();
		if (forum && !_topicJumpCache) {
			_topicJumpCache = std::make_unique<Ui::TopicJumpCache>();
		}
		const auto expanding = forum
			&& (key.history()->peer->id == childListShown.peerId);

		context.st = (forum ? &st::forumDialogRow : _st.get());
		context.topicsExpanded = (expanding && !active)
			? childListShown.shown
			: 0.;
		context.active = active;
		context.selected = _menuRow.key
			? (row->key() == _menuRow.key)
			: selected;
		context.topicJumpSelected = selected
			&& _selectedTopicJump
			&& (!_pressed || _pressedTopicJump);
		Ui::RowPainter::Paint(p, row, validateVideoUserpic(row), context);
	};
	if (_state == WidgetState::Default) {
		const auto collapsedSkip = collapsedRowsOffset();
		p.translate(0, collapsedSkip);
		paintCollapsedRows(p, r.translated(0, -collapsedSkip));

		const auto &list = _shownList->all();
		const auto shownBottom = _shownList->height() - skipTopHeight();
		const auto selected = isPressed()
			? (_pressed ? _pressed->key() : Key())
			: (_selected ? _selected->key() : Key());
		if (shownBottom) {
			const auto skip = dialogsOffset();
			const auto promoted = fixedOnTopCount();
			const auto reorderingPinned = (_aboveIndex >= 0)
				&& !_pinnedRows.empty();
			const auto reorderingIndex = reorderingPinned
				? (promoted + _aboveIndex)
				: -1;
			const auto reorderingRow = (reorderingIndex >= 0
				&& reorderingIndex < list.size())
				? (list.cbegin() + reorderingIndex)->get()
				: nullptr;
			if (reorderingRow) {
				dialogsClip = dialogsClip.marginsAdded({
					0,
					reorderingRow->height(),
					0,
					reorderingRow->height(),
				});
			}
			const auto skippedTop = skipTopHeight();
			const auto paintDialog = [&](not_null<Row*> row) {
				const auto pinned = row->index() - promoted;
				const auto count = _pinnedRows.size();
				const auto xadd = 0;
				const auto yadd = base::in_range(pinned, 0, count)
					? qRound(_pinnedRows[pinned].yadd.current())
					: 0;
				if (xadd || yadd) {
					p.translate(xadd, yadd);
				}
				paintRow(row, (row->key() == selected), true);
				if (xadd || yadd) {
					p.translate(-xadd, -yadd);
				}
			};

			auto i = list.findByY(dialogsClip.top() - skip);
			if (_skipTopDialog && i != list.cend() && !(*i)->index()) {
				++i;
			}
			if (i != list.cend()) {
				auto top = (*i)->top();

				// If we're reordering pinned chats we need to fill this area background first.
				if (reorderingPinned) {
					const auto pinnedBottom = shownHeight(promoted + _pinnedRows.size());
					const auto pinnedTop = shownHeight(promoted);
					p.fillRect(0, pinnedTop - skippedTop, fullWidth, pinnedBottom - pinnedTop, currentBg());
				}

				p.translate(0, top - skippedTop);
				for (auto e = list.cend(); i != e; ++i) {
					auto row = (*i);
					if (top >= dialogsClip.top() - skip + dialogsClip.height()) {
						break;
					}

					// Skip currently dragged chat to paint it above others after.
					if (row->index() != promoted + _aboveIndex || _aboveIndex < 0) {
						paintDialog(row);
					}

					p.translate(0, row->height());
					top += row->height();
				}

				// Paint the dragged chat above all others.
				if (reorderingRow) {
					p.translate(0, reorderingRow->top() - top);
					paintDialog(reorderingRow);
					p.translate(0, top - reorderingRow->top());
				}
			}
		} else {
			p.fillRect(dialogsClip, currentBg());
		}
	} else if (_state == WidgetState::Filtered) {
		if (!_hashtagResults.empty()) {
			auto from = floorclamp(r.y(), st::mentionHeight, 0, _hashtagResults.size());
			auto to = ceilclamp(r.y() + r.height(), st::mentionHeight, 0, _hashtagResults.size());
			p.translate(0, from * st::mentionHeight);
			if (from < _hashtagResults.size()) {
				const auto htagleft = st::defaultDialogRow.padding.left();
				auto htagwidth = fullWidth
					- htagleft
					- st::defaultDialogRow.padding.right();

				p.setFont(st::mentionFont);
				for (; from < to; ++from) {
					auto &result = _hashtagResults[from];
					bool selected = (from == (isPressed() ? _hashtagPressed : _hashtagSelected));
					p.fillRect(0, 0, fullWidth, st::mentionHeight, selected ? st::mentionBgOver : currentBg());
					result->row.paintRipple(p, 0, 0, fullWidth);
					auto &tag = result->tag;
					if (selected) {
						int skip = (st::mentionHeight - st::smallCloseIconOver.height()) / 2;
						st::smallCloseIconOver.paint(p, QPoint(fullWidth - st::smallCloseIconOver.width() - skip, skip), width());
					}
					auto first = (_hashtagFilter.size() < 2) ? QString() : ('#' + tag.mid(0, _hashtagFilter.size() - 1));
					auto second = (_hashtagFilter.size() < 2) ? ('#' + tag) : tag.mid(_hashtagFilter.size() - 1);
					auto firstwidth = st::mentionFont->width(first);
					auto secondwidth = st::mentionFont->width(second);
					if (htagwidth < firstwidth + secondwidth) {
						if (htagwidth < firstwidth + st::mentionFont->elidew) {
							first = st::mentionFont->elided(first + second, htagwidth);
							second = QString();
						} else {
							second = st::mentionFont->elided(second, htagwidth - firstwidth);
						}
					}

					p.setFont(st::mentionFont);
					if (!first.isEmpty()) {
						p.setPen(selected ? st::mentionFgOverActive : st::mentionFgActive);
						p.drawText(htagleft, st::mentionTop + st::mentionFont->ascent, first);
					}
					if (!second.isEmpty()) {
						p.setPen(selected ? st::mentionFgOver : st::mentionFg);
						p.drawText(htagleft + firstwidth, st::mentionTop + st::mentionFont->ascent, second);
					}
					p.translate(0, st::mentionHeight);
				}
			}
		}
		if (!_filterResults.empty()) {
			auto skip = filteredOffset();
			auto from = filteredIndex(r.y() - skip);
			auto to = std::min(
				filteredIndex(r.y() + r.height() - skip) + 1,
				int(_filterResults.size()));
			p.translate(0, filteredHeight(from));
			for (; from < to; ++from) {
				const auto selected = isPressed()
					? (from == _filteredPressed)
					: (from == _filteredSelected);
				const auto row = _filterResults[from].row;
				paintRow(row, selected, !activeEntry.fullId);
				p.translate(0, row->height());
			}
		}

		if (!_peerSearchResults.empty()) {
			p.fillRect(0, 0, fullWidth, st::searchedBarHeight, st::searchedBarBg);
			p.setFont(st::searchedBarFont);
			p.setPen(st::searchedBarFg);
			p.drawTextLeft(st::searchedBarPosition.x(), st::searchedBarPosition.y(), width(), tr::lng_search_global_results(tr::now));
			p.translate(0, st::searchedBarHeight);

			auto skip = peerSearchOffset();
			auto from = floorclamp(r.y() - skip, st::dialogsRowHeight, 0, _peerSearchResults.size());
			auto to = ceilclamp(r.y() + r.height() - skip, st::dialogsRowHeight, 0, _peerSearchResults.size());
			p.translate(0, from * st::dialogsRowHeight);
			if (from < _peerSearchResults.size()) {
				const auto activePeer = activeEntry.key.peer();
				for (; from < to; ++from) {
					const auto &result = _peerSearchResults[from];
					const auto peer = result->peer;
					const auto active = !activeEntry.fullId
						&& activePeer
						&& ((peer == activePeer)
							|| (peer->migrateTo() == activePeer));
					const auto selected = (from == (isPressed()
						? _peerSearchPressed
						: _peerSearchSelected));
					paintPeerSearchResult(p, result.get(), {
						.st = &st::defaultDialogRow,
						.currentBg = currentBg(),
						.now = ms,
						.width = fullWidth,
						.active = active,
						.selected = selected,
						.paused = videoPaused,
					});
					p.translate(0, st::dialogsRowHeight);
				}
			}
		}

		if (_searchInChat || _searchFromPeer) {
			paintSearchInChat(p, {
				.st = &st::forumTopicRow,
				.currentBg = currentBg(),
				.now = ms,
				.width = fullWidth,
				.paused = videoPaused,
			});
			p.translate(0, searchInChatSkip());
			if (_waitingForSearch && _searchResults.empty()) {
				p.fillRect(
					0,
					0,
					fullWidth,
					st::searchedBarHeight,
					st::searchedBarBg);
				p.setFont(st::searchedBarFont);
				p.setPen(st::searchedBarFg);
				p.drawTextLeft(
					st::searchedBarPosition.x(),
					st::searchedBarPosition.y(),
					width(),
					tr::lng_dlg_search_for_messages(tr::now));
				p.translate(0, st::searchedBarHeight);
			}
		}

		const auto showUnreadInSearchResults = uniqueSearchResults();
		if (!_waitingForSearch || !_searchResults.empty()) {
			const auto text = _searchResults.empty()
				? tr::lng_search_no_results(tr::now)
				: showUnreadInSearchResults
				? u"Search results"_q
				: tr::lng_search_found_results(
					tr::now,
					lt_count,
					_searchedMigratedCount + _searchedCount);
			p.fillRect(0, 0, fullWidth, st::searchedBarHeight, st::searchedBarBg);
			p.setFont(st::searchedBarFont);
			p.setPen(st::searchedBarFg);
			p.drawTextLeft(st::searchedBarPosition.x(), st::searchedBarPosition.y(), width(), text);
			p.translate(0, st::searchedBarHeight);

			auto skip = searchedOffset();
			auto from = floorclamp(r.y() - skip, _st->height, 0, _searchResults.size());
			auto to = ceilclamp(r.y() + r.height() - skip, _st->height, 0, _searchResults.size());
			p.translate(0, from * _st->height);
			if (from < _searchResults.size()) {
				for (; from < to; ++from) {
					const auto &result = _searchResults[from];
					const auto active = isSearchResultActive(result.get(), activeEntry);
					const auto selected = _menuRow.key
						? isSearchResultActive(result.get(), _menuRow)
						: (from == (isPressed()
							? _searchedPressed
							: _searchedSelected));
					Ui::RowPainter::Paint(p, result.get(), {
						.st = _st,
						.folder = _openedFolder,
						.forum = _openedForum,
						.currentBg = currentBg(),
						.filter = _filterId,
						.now = ms,
						.width = fullWidth,
						.active = active,
						.selected = selected,
						.paused = videoPaused,
						.search = true,
						.narrow = (fullWidth < st::columnMinimalWidthLeft / 2),
						.displayUnreadInfo = showUnreadInSearchResults,
					});
					p.translate(0, _st->height);
				}
			}
		}
	}
}

Ui::VideoUserpic *InnerWidget::validateVideoUserpic(not_null<Row*> row) {
	const auto history = row->history();
	return history ? validateVideoUserpic(history) : nullptr;
}

Ui::VideoUserpic *InnerWidget::validateVideoUserpic(
		not_null<History*> history) {
	const auto peer = history->peer;
	if (!peer->isPremium()
		|| peer->userpicPhotoUnknown()
		|| !peer->userpicHasVideo()) {
		_videoUserpics.remove(peer);
		return nullptr;
	}
	const auto i = _videoUserpics.find(peer);
	if (i != end(_videoUserpics)) {
		return i->second.get();
	}
	const auto repaint = [=] {
		updateDialogRow({ history, FullMsgId() });
		updateSearchResult(history->peer);
	};
	return _videoUserpics.emplace(peer, std::make_unique<Ui::VideoUserpic>(
		peer,
		repaint
	)).first->second.get();
}

void InnerWidget::paintCollapsedRows(Painter &p, QRect clip) const {
	auto index = 0;
	const auto rowHeight = st::dialogsImportantBarHeight;
	for (const auto &row : _collapsedRows) {
		const auto increment = gsl::finally([&] {
			p.translate(0, rowHeight);
			++index;
		});

		const auto y = index * rowHeight;
		if (!clip.intersects(QRect(0, y, width(), rowHeight))) {
			continue;
		}
		const auto selected = (index == _collapsedSelected)
			|| (index == _collapsedPressed);
		paintCollapsedRow(p, row.get(), selected);
	}
}

void InnerWidget::paintCollapsedRow(
		Painter &p,
		not_null<const CollapsedRow*> row,
		bool selected) const {
	Expects(row->folder != nullptr);

	const auto text = row->folder->chatListName();
	const auto unread = row->folder->chatListBadgesState().unreadCounter;
	const auto fullWidth = width();
	Ui::PaintCollapsedRow(p, row->row, row->folder, text, unread, {
		.st = _st,
		.currentBg = currentBg(),
		.width = fullWidth,
		.selected = selected,
		.narrow = (fullWidth < st::columnMinimalWidthLeft / 2),
	});
}

bool InnerWidget::isSearchResultActive(
		not_null<FakeRow*> result,
		const RowDescriptor &entry) const {
	const auto item = result->item();
	const auto peer = item->history()->peer;
	return (item->fullId() == entry.fullId)
		|| (peer->migrateTo()
			&& (peer->migrateTo()->id == entry.fullId.peer)
			&& (item->id == -entry.fullId.msg))
		|| (uniqueSearchResults() && peer == entry.key.peer());
}

void InnerWidget::paintPeerSearchResult(
		Painter &p,
		not_null<const PeerSearchResult*> result,
		const Ui::PaintContext &context) {
	QRect fullRect(0, 0, context.width, st::dialogsRowHeight);
	p.fillRect(
		fullRect,
		(context.active
			? st::dialogsBgActive
			: context.selected
			? st::dialogsBgOver
			: currentBg()));
	if (!context.active) {
		result->row.paintRipple(p, 0, 0, context.width);
	}

	auto peer = result->peer;
	auto userpicPeer = (peer->migrateTo() ? peer->migrateTo() : peer);
	userpicPeer->paintUserpicLeft(
		p,
		result->row.userpicView(),
		context.st->padding.left(),
		context.st->padding.top(),
		width(),
		context.st->photoSize);

	auto nameleft = context.st->nameLeft;
	auto namewidth = context.width - nameleft - context.st->padding.right();
	QRect rectForName(nameleft, context.st->nameTop, namewidth, st::semiboldFont->height);

	if (result->name.isEmpty()) {
		result->name.setText(
			st::semiboldTextStyle,
			peer->name(),
			Ui::NameTextOptions());
	}

	// draw chat icon
	if (const auto chatTypeIcon = Ui::ChatTypeIcon(peer, context)) {
		chatTypeIcon->paint(p, rectForName.topLeft(), context.width);
		rectForName.setLeft(rectForName.left()
			+ chatTypeIcon->width()
			+ st::dialogsChatTypeSkip);
	}
	const auto badgeWidth = result->badge.drawGetWidth(
		p,
		rectForName,
		result->name.maxWidth(),
		context.width,
		{
			.peer = peer,
			.verified = (context.active
				? &st::dialogsVerifiedIconActive
				: context.selected
				? &st::dialogsVerifiedIconOver
				: &st::dialogsVerifiedIcon),
			.premium = &ThreeStateIcon(
				st::dialogsPremiumIcon,
				context.active,
				context.selected),
			.scam = (context.active
				? &st::dialogsScamFgActive
				: context.selected
				? &st::dialogsScamFgOver
				: &st::dialogsScamFg),
			.premiumFg = (context.active
				? &st::dialogsVerifiedIconBgActive
				: context.selected
				? &st::dialogsVerifiedIconBgOver
				: &st::dialogsVerifiedIconBg),
			.customEmojiRepaint = [=] { updateSearchResult(peer); },
			.now = context.now,
			.paused = context.paused,
		});
	rectForName.setWidth(rectForName.width() - badgeWidth);

	QRect tr(context.st->textLeft, context.st->textTop, namewidth, st::dialogsTextFont->height);
	p.setFont(st::dialogsTextFont);
	QString username = peer->userName();
	if (!context.active && username.startsWith(_peerSearchQuery, Qt::CaseInsensitive)) {
		auto first = '@' + username.mid(0, _peerSearchQuery.size());
		auto second = username.mid(_peerSearchQuery.size());
		auto w = st::dialogsTextFont->width(first);
		if (w >= tr.width()) {
			p.setPen(st::dialogsTextFgService);
			p.drawText(tr.left(), tr.top() + st::dialogsTextFont->ascent, st::dialogsTextFont->elided(first, tr.width()));
		} else {
			p.setPen(st::dialogsTextFgService);
			p.drawText(tr.left(), tr.top() + st::dialogsTextFont->ascent, first);
			p.setPen(st::dialogsTextFg);
			p.drawText(tr.left() + w, tr.top() + st::dialogsTextFont->ascent, st::dialogsTextFont->elided(second, tr.width() - w));
		}
	} else {
		p.setPen(context.active ? st::dialogsTextFgActive : st::dialogsTextFgService);
		p.drawText(tr.left(), tr.top() + st::dialogsTextFont->ascent, st::dialogsTextFont->elided('@' + username, tr.width()));
	}

	p.setPen(context.active ? st::dialogsTextFgActive : st::dialogsNameFg);
	result->name.drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
}

QBrush InnerWidget::currentBg() const {
	return anim::brush(
		st::dialogsBg,
		st::dialogsBgOver,
		_childListShown.current().shown);
}

void InnerWidget::paintSearchInChat(
		Painter &p,
		const Ui::PaintContext &context) const {
	auto height = searchInChatSkip();

	auto top = 0;
	p.setFont(st::searchedBarFont);
	if (_searchInChat) {
		top += st::searchedBarHeight;
		p.fillRect(0, 0, width(), top, st::searchedBarBg);
		p.setPen(st::searchedBarFg);
		p.drawTextLeft(st::searchedBarPosition.x(), st::searchedBarPosition.y(), width(), tr::lng_dlg_search_in(tr::now));
	}
	auto fullRect = QRect(0, top, width(), height - top);
	p.fillRect(fullRect, currentBg());
	if (_searchInChat) {
		if (_searchFromPeer) {
			p.fillRect(QRect(0, top + st::dialogsSearchInHeight, width(), st::lineWidth), st::shadowFg);
		}
		p.setPen(st::dialogsNameFg);
		if (const auto topic = _searchInChat.topic()) {
			paintSearchInTopic(p, context, topic, _searchInChatUserpic, top, _searchInChatText);
		} else if (const auto peer = _searchInChat.peer()) {
			if (peer->isSelf()) {
				paintSearchInSaved(p, top, _searchInChatText);
			} else if (peer->isRepliesChat()) {
				paintSearchInReplies(p, top, _searchInChatText);
			} else {
				paintSearchInPeer(p, peer, _searchInChatUserpic, top, _searchInChatText);
			}
		} else {
			Unexpected("Empty Key in paintSearchInChat.");
		}
		top += st::dialogsSearchInHeight + st::lineWidth;
	}
	if (_searchFromPeer) {
		p.setPen(st::dialogsTextFg);
		p.setTextPalette(st::dialogsSearchFromPalette);
		paintSearchInPeer(p, _searchFromPeer, _searchFromUserUserpic, top, _searchFromUserText);
		p.restoreTextPalette();
	}
}
template <typename PaintUserpic>
void InnerWidget::paintSearchInFilter(
		Painter &p,
		PaintUserpic paintUserpic,
		int top,
		const style::icon *icon,
		const Ui::Text::String &text) const {
	const auto savedPen = p.pen();
	const auto userpicLeft = st::defaultDialogRow.padding.left();
	const auto userpicTop = top
		+ (st::dialogsSearchInHeight - st::dialogsSearchInPhotoSize) / 2;
	paintUserpic(p, userpicLeft, userpicTop, st::dialogsSearchInPhotoSize);

	const auto nameleft = st::defaultDialogRow.padding.left()
		+ st::dialogsSearchInPhotoSize
		+ st::dialogsSearchInPhotoPadding;
	const auto namewidth = width()
		- nameleft
		- st::defaultDialogRow.padding.left()
		- st::defaultDialogRow.padding.right()
		- st::dialogsCancelSearch.width;
	auto rectForName = QRect(
		nameleft,
		top + (st::dialogsSearchInHeight - st::semiboldFont->height) / 2,
		namewidth,
		st::semiboldFont->height);
	if (icon) {
		icon->paint(p, rectForName.topLeft(), width());
		rectForName.setLeft(rectForName.left()
			+ icon->width()
			+ st::dialogsChatTypeSkip);
	}
	p.setPen(savedPen);
	text.drawLeftElided(
		p,
		rectForName.left(),
		rectForName.top(),
		rectForName.width(),
		width());
}

void InnerWidget::paintSearchInPeer(
		Painter &p,
		not_null<PeerData*> peer,
		Ui::PeerUserpicView &userpic,
		int top,
		const Ui::Text::String &text) const {
	const auto paintUserpic = [&](Painter &p, int x, int y, int size) {
		peer->paintUserpicLeft(p, userpic, x, y, width(), size);
	};
	const auto icon = Ui::ChatTypeIcon(peer);
	paintSearchInFilter(p, paintUserpic, top, icon, text);
}

void InnerWidget::paintSearchInSaved(
		Painter &p,
		int top,
		const Ui::Text::String &text) const {
	const auto paintUserpic = [&](Painter &p, int x, int y, int size) {
		Ui::EmptyUserpic::PaintSavedMessages(p, x, y, width(), size);
	};
	paintSearchInFilter(p, paintUserpic, top, nullptr, text);
}

void InnerWidget::paintSearchInReplies(
		Painter &p,
		int top,
		const Ui::Text::String &text) const {
	const auto paintUserpic = [&](Painter &p, int x, int y, int size) {
		Ui::EmptyUserpic::PaintRepliesMessages(p, x, y, width(), size);
	};
	paintSearchInFilter(p, paintUserpic, top, nullptr, text);
}

void InnerWidget::paintSearchInTopic(
		Painter &p,
		const Ui::PaintContext &context,
		not_null<Data::ForumTopic*> topic,
		Ui::PeerUserpicView &userpic,
		int top,
		const Ui::Text::String &text) const {
	const auto paintUserpic = [&](Painter &p, int x, int y, int size) {
		p.translate(x, y);
		topic->paintUserpic(p, userpic, context);
		p.translate(-x, -y);
	};
	paintSearchInFilter(p, paintUserpic, top, nullptr, text);
}

void InnerWidget::mouseMoveEvent(QMouseEvent *e) {
	const auto globalPosition = e->globalPos();
	if (!_lastMousePosition) {
		_lastMousePosition = globalPosition;
		return;
	} else if (!_mouseSelection
		&& *_lastMousePosition == globalPosition) {
		return;
	}
	selectByMouse(globalPosition);
}

void InnerWidget::clearIrrelevantState() {
	if (_state == WidgetState::Default) {
		_hashtagSelected = -1;
		setHashtagPressed(-1);
		_hashtagDeleteSelected = _hashtagDeletePressed = false;
		_filteredSelected = -1;
		setFilteredPressed(-1, false);
		_peerSearchSelected = -1;
		setPeerSearchPressed(-1);
		_searchedSelected = -1;
		setSearchedPressed(-1);
	} else if (_state == WidgetState::Filtered) {
		_collapsedSelected = -1;
		setCollapsedPressed(-1);
		_selected = nullptr;
		clearPressed();
	}
}

void InnerWidget::selectByMouse(QPoint globalPosition) {
	const auto local = mapFromGlobal(globalPosition);
	if (updateReorderPinned(local)) {
		return;
	}
	_mouseSelection = true;
	_lastMousePosition = globalPosition;
	_lastRowLocalMouseX = local.x();

	const auto w = width();
	const auto mouseY = local.y();
	clearIrrelevantState();
	if (_state == WidgetState::Default) {
		const auto offset = dialogsOffset();
		const auto collapsedSelected = (mouseY >= 0
			&& mouseY < _collapsedRows.size() * st::dialogsImportantBarHeight)
			? (mouseY / st::dialogsImportantBarHeight)
			: -1;
		const auto selected = (collapsedSelected >= 0)
			? nullptr
			: (mouseY >= offset)
			? _shownList->rowAtY(mouseY - offset)
			: nullptr;
		const auto selectedTopicJump = selected
			&& selected->lookupIsInTopicJump(
				local.x(),
				mouseY - offset - selected->top());
		if (_collapsedSelected != collapsedSelected
			|| _selected != selected
			|| _selectedTopicJump != selectedTopicJump) {
			updateSelectedRow();
			_selected = selected;
			_selectedTopicJump = selectedTopicJump;
			_collapsedSelected = collapsedSelected;
			updateSelectedRow();
			setCursor((_selected || _collapsedSelected >= 0)
				? style::cur_pointer
				: style::cur_default);
		}
	} else if (_state == WidgetState::Filtered) {
		auto wasSelected = isSelected();
		if (_hashtagResults.empty()) {
			_hashtagSelected = -1;
			_hashtagDeleteSelected = false;
		} else {
			auto skip = 0;
			auto hashtagSelected = (mouseY >= skip) ? ((mouseY - skip) / st::mentionHeight) : -1;
			if (hashtagSelected < 0 || hashtagSelected >= _hashtagResults.size()) {
				hashtagSelected = -1;
			}
			if (_hashtagSelected != hashtagSelected) {
				updateSelectedRow();
				_hashtagSelected = hashtagSelected;
				updateSelectedRow();
			}
			_hashtagDeleteSelected = (_hashtagSelected >= 0) && (local.x() >= w - st::mentionHeight);
		}
		if (!_filterResults.empty()) {
			auto skip = filteredOffset();
			auto filteredSelected = (mouseY >= skip)
				? filteredIndex(mouseY - skip)
				: -1;
			if (filteredSelected < 0 || filteredSelected >= _filterResults.size()) {
				filteredSelected = -1;
			}
			const auto selectedTopicJump = (filteredSelected >= 0)
				&& _filterResults[filteredSelected].row->lookupIsInTopicJump(
					local.x(),
					mouseY - skip - _filterResults[filteredSelected].top);
			if (_filteredSelected != filteredSelected
				|| _selectedTopicJump != selectedTopicJump) {
				updateSelectedRow();
				_filteredSelected = filteredSelected;
				_selectedTopicJump = selectedTopicJump;
				updateSelectedRow();
			}
		}
		if (!_peerSearchResults.empty()) {
			auto skip = peerSearchOffset();
			auto peerSearchSelected = (mouseY >= skip) ? ((mouseY - skip) / st::dialogsRowHeight) : -1;
			if (peerSearchSelected < 0 || peerSearchSelected >= _peerSearchResults.size()) {
				peerSearchSelected = -1;
			}
			if (_peerSearchSelected != peerSearchSelected) {
				updateSelectedRow();
				_peerSearchSelected = peerSearchSelected;
				updateSelectedRow();
			}
		}
		if (!_waitingForSearch && !_searchResults.empty()) {
			auto skip = searchedOffset();
			auto searchedSelected = (mouseY >= skip) ? ((mouseY - skip) / _st->height) : -1;
			if (searchedSelected < 0 || searchedSelected >= _searchResults.size()) {
				searchedSelected = -1;
			}
			if (_searchedSelected != searchedSelected) {
				updateSelectedRow();
				_searchedSelected = searchedSelected;
				updateSelectedRow();
			}
		}
		if (wasSelected != isSelected()) {
			setCursor(wasSelected ? style::cur_default : style::cur_pointer);
		}
	}
}

void InnerWidget::mousePressEvent(QMouseEvent *e) {
	selectByMouse(e->globalPos());

	_pressButton = e->button();
	setPressed(_selected, _selectedTopicJump);
	setCollapsedPressed(_collapsedSelected);
	setHashtagPressed(_hashtagSelected);
	_hashtagDeletePressed = _hashtagDeleteSelected;
	setFilteredPressed(_filteredSelected, _selectedTopicJump);
	setPeerSearchPressed(_peerSearchSelected);
	setSearchedPressed(_searchedSelected);
	if (base::in_range(_collapsedSelected, 0, _collapsedRows.size())) {
		auto row = &_collapsedRows[_collapsedSelected]->row;
		row->addRipple(e->pos(), QSize(width(), st::dialogsImportantBarHeight), [this, index = _collapsedSelected] {
			update(0, (index * st::dialogsImportantBarHeight), width(), st::dialogsImportantBarHeight);
		});
	} else if (_pressed) {
		auto row = _pressed;
		const auto updateCallback = [this, row] {
			if (!_pinnedShiftAnimation.animating()) {
				row->entry()->updateChatListEntry();
			}
		};
		const auto origin = e->pos()
			- QPoint(0, dialogsOffset() + _pressed->top());
		if (_pressedTopicJump) {
			row->addTopicJumpRipple(
				origin,
				_topicJumpCache.get(),
				updateCallback);
		} else {
			row->clearTopicJumpRipple();
			row->addRipple(
				origin,
				QSize(width(), _pressed->height()),
				updateCallback);
		}
		_dragStart = e->pos();
	} else if (base::in_range(_hashtagPressed, 0, _hashtagResults.size()) && !_hashtagDeletePressed) {
		auto row = &_hashtagResults[_hashtagPressed]->row;
		row->addRipple(e->pos(), QSize(width(), st::mentionHeight), [this, index = _hashtagPressed] {
			update(0, index * st::mentionHeight, width(), st::mentionHeight);
		});
	} else if (base::in_range(_filteredPressed, 0, _filterResults.size())) {
		const auto &result = _filterResults[_filteredPressed];
		const auto row = result.row;
		const auto filterId = _filterId;
		const auto origin = e->pos()
			- QPoint(0, filteredOffset() + result.top);
		const auto updateCallback = [=] { repaintDialogRow(filterId, row); };
		if (_pressedTopicJump) {
			row->addTopicJumpRipple(
				origin,
				_topicJumpCache.get(),
				updateCallback);
		} else {
			row->clearTopicJumpRipple();
			row->addRipple(
				origin,
				QSize(width(), row->height()),
				updateCallback);
		}
	} else if (base::in_range(_peerSearchPressed, 0, _peerSearchResults.size())) {
		auto &result = _peerSearchResults[_peerSearchPressed];
		auto row = &result->row;
		row->addRipple(
			e->pos() - QPoint(0, peerSearchOffset() + _peerSearchPressed * st::dialogsRowHeight),
			QSize(width(), st::dialogsRowHeight),
			[this, peer = result->peer] { updateSearchResult(peer); });
	} else if (base::in_range(_searchedPressed, 0, _searchResults.size())) {
		auto &row = _searchResults[_searchedPressed];
		row->addRipple(
			e->pos() - QPoint(0, searchedOffset() + _searchedPressed * _st->height),
			QSize(width(), _st->height),
			row->repaint());
	}
	if (anim::Disabled()
		&& (!_pressed || !_pressed->entry()->isPinnedDialog(_filterId))) {
		mousePressReleased(e->globalPos(), e->button(), e->modifiers());
	}
}
const std::vector<Key> &InnerWidget::pinnedChatsOrder() const {
	return _openedForum
		? session().data().pinnedChatsOrder(_openedForum)
		: _filterId
		? session().data().pinnedChatsOrder(_filterId)
		: session().data().pinnedChatsOrder(_openedFolder);
}

void InnerWidget::checkReorderPinnedStart(QPoint localPosition) {
	if (!_pressed || _dragging || _state != WidgetState::Default) {
		return;
	} else if (qAbs(localPosition.y() - _dragStart.y())
		< style::ConvertScale(kStartReorderThreshold)) {
		return;
	}
	_dragging = _pressed;
	if (updateReorderIndexGetCount() < 2) {
		_dragging = nullptr;
	} else {
		const auto &order = pinnedChatsOrder();
		_pinnedOnDragStart = base::flat_set<Key>{
			order.begin(),
			order.end()
		};
		_pinnedRows[_draggingIndex].yadd = anim::value(0, localPosition.y() - _dragStart.y());
		_pinnedRows[_draggingIndex].animStartTime = crl::now();
		_pinnedShiftAnimation.start();
	}
}

int InnerWidget::countPinnedIndex(Row *ofRow) {
	if (!ofRow || !ofRow->entry()->isPinnedDialog(_filterId)) {
		return -1;
	}
	auto result = 0;
	for (const auto &row : *_shownList) {
		if (row->entry()->fixedOnTopIndex()) {
			continue;
		} else if (!row->entry()->isPinnedDialog(_filterId)) {
			break;
		} else if (row == ofRow) {
			return result;
		}
		++result;
	}
	return -1;
}

void InnerWidget::savePinnedOrder() {
	const auto &newOrder = pinnedChatsOrder();
	if (newOrder.size() != _pinnedOnDragStart.size()) {
		return; // Something has changed in the set of pinned chats.
	}
	for (const auto &key : newOrder) {
		if (!_pinnedOnDragStart.contains(key)) {
			return; // Something has changed in the set of pinned chats.
		}
	}
	if (_openedForum) {
		session().api().savePinnedOrder(_openedForum);
	} else if (_filterId) {
		Api::SaveNewFilterPinned(&session(), _filterId);
	} else {
		session().api().savePinnedOrder(_openedFolder);
	}
}

void InnerWidget::finishReorderPinned() {
	auto wasDragging = (_dragging != nullptr);
	if (wasDragging) {
		savePinnedOrder();
		_dragging = nullptr;
	}

	_draggingIndex = -1;
	if (!_pinnedShiftAnimation.animating()) {
		_pinnedRows.clear();
		_aboveIndex = -1;
	}
	if (wasDragging) {
		_draggingScroll.cancel();
	}
}

void InnerWidget::stopReorderPinned() {
	_pinnedShiftAnimation.stop();
	finishReorderPinned();
}

int InnerWidget::updateReorderIndexGetCount() {
	auto index = countPinnedIndex(_dragging);
	if (index < 0) {
		finishReorderPinned();
		return 0;
	}

	const auto count = Dialogs::PinnedDialogsCount(_filterId, _shownList);
	Assert(index < count);
	if (count < 2) {
		stopReorderPinned();
		return 0;
	}

	_draggingIndex = index;
	_aboveIndex = _draggingIndex;
	while (count > _pinnedRows.size()) {
		_pinnedRows.emplace_back();
	}
	while (count < _pinnedRows.size()) {
		_pinnedRows.pop_back();
	}
	return count;
}

bool InnerWidget::updateReorderPinned(QPoint localPosition) {
	checkReorderPinnedStart(localPosition);
	auto pinnedCount = updateReorderIndexGetCount();
	if (pinnedCount < 2) {
		return false;
	}

	const auto draggingHeight = _dragging->height();
	auto yaddWas = _pinnedRows[_draggingIndex].yadd.current();
	auto shift = 0;
	auto now = crl::now();
	if (_dragStart.y() > localPosition.y() && _draggingIndex > 0) {
		shift = -floorclamp(_dragStart.y() - localPosition.y() + (draggingHeight / 2), draggingHeight, 0, _draggingIndex);

		for (auto from = _draggingIndex, to = _draggingIndex + shift; from > to; --from) {
			_shownList->movePinned(_dragging, -1);
			std::swap(_pinnedRows[from], _pinnedRows[from - 1]);
			_pinnedRows[from].yadd = anim::value(_pinnedRows[from].yadd.current() - draggingHeight, 0);
			_pinnedRows[from].animStartTime = now;
		}
	} else if (_dragStart.y() < localPosition.y() && _draggingIndex + 1 < pinnedCount) {
		shift = floorclamp(localPosition.y() - _dragStart.y() + (draggingHeight / 2), draggingHeight, 0, pinnedCount - _draggingIndex - 1);

		for (auto from = _draggingIndex, to = _draggingIndex + shift; from < to; ++from) {
			_shownList->movePinned(_dragging, 1);
			std::swap(_pinnedRows[from], _pinnedRows[from + 1]);
			_pinnedRows[from].yadd = anim::value(_pinnedRows[from].yadd.current() + draggingHeight, 0);
			_pinnedRows[from].animStartTime = now;
		}
	}
	if (shift) {
		_draggingIndex += shift;
		_aboveIndex = _draggingIndex;
		_dragStart.setY(_dragStart.y() + shift * _st->height);
		if (!_pinnedShiftAnimation.animating()) {
			_pinnedShiftAnimation.start();
		}
	}
	_aboveTopShift = qCeil(_pinnedRows[_aboveIndex].yadd.current());
	_pinnedRows[_draggingIndex].yadd = anim::value(yaddWas - shift * _st->height, localPosition.y() - _dragStart.y());
	if (!_pinnedRows[_draggingIndex].animStartTime) {
		_pinnedRows[_draggingIndex].yadd.finish();
	}
	pinnedShiftAnimationCallback(now);

	const auto delta = [&] {
		if (localPosition.y() < _visibleTop) {
			return localPosition.y() - _visibleTop;
		} else if ((_openedFolder || _openedForum || _filterId)
			&& localPosition.y() > _visibleBottom) {
			return localPosition.y() - _visibleBottom;
		}
		return 0;
	}();

	_draggingScroll.checkDeltaScroll(delta);
	return true;
}

bool InnerWidget::pinnedShiftAnimationCallback(crl::time now) {
	if (anim::Disabled()) {
		now += st::stickersRowDuration;
	}

	auto animating = false;
	auto updateMin = -1;
	auto updateMax = 0;
	for (auto i = 0, l = static_cast<int>(_pinnedRows.size()); i != l; ++i) {
		auto start = _pinnedRows[i].animStartTime;
		if (start) {
			if (updateMin < 0) updateMin = i;
			updateMax = i;
			if (start + st::stickersRowDuration > now && now >= start) {
				_pinnedRows[i].yadd.update(float64(now - start) / st::stickersRowDuration, anim::sineInOut);
				animating = true;
			} else {
				_pinnedRows[i].yadd.finish();
				_pinnedRows[i].animStartTime = 0;
			}
		}
	}
	updateReorderIndexGetCount();
	if (_draggingIndex >= 0) {
		if (updateMin < 0 || updateMin > _draggingIndex) {
			updateMin = _draggingIndex;
		}
		if (updateMax < _draggingIndex) updateMax = _draggingIndex;
	}
	if (updateMin >= 0) {
		const auto minHeight = _st->height;
		const auto maxHeight = st::forumDialogRow.height;
		auto top = pinnedOffset();
		auto updateFrom = top + minHeight * (updateMin - 1);
		auto updateHeight = maxHeight * (updateMax - updateMin + 3);
		if (base::in_range(_aboveIndex, 0, _pinnedRows.size())) {
			// Always include currently dragged chat in its current and old positions.
			auto aboveRowBottom = top + (_aboveIndex + 1) * maxHeight;
			auto aboveTopShift = qCeil(_pinnedRows[_aboveIndex].yadd.current());
			accumulate_max(updateHeight, (aboveRowBottom - updateFrom) + _aboveTopShift);
			accumulate_max(updateHeight, (aboveRowBottom - updateFrom) + aboveTopShift);
			_aboveTopShift = aboveTopShift;
		}
		update(0, updateFrom, width(), updateHeight);
	}
	if (!animating) {
		_aboveIndex = _draggingIndex;
	}
	return animating;
}

void InnerWidget::mouseReleaseEvent(QMouseEvent *e) {
	mousePressReleased(e->globalPos(), e->button(), e->modifiers());
}

void InnerWidget::mousePressReleased(
		QPoint globalPosition,
		Qt::MouseButton button,
		Qt::KeyboardModifiers modifiers) {
	auto wasDragging = (_dragging != nullptr);
	if (wasDragging) {
		updateReorderIndexGetCount();
		if (_draggingIndex >= 0) {
			_pinnedRows[_draggingIndex].yadd.start(0.);
			_pinnedRows[_draggingIndex].animStartTime = crl::now();
			if (!_pinnedShiftAnimation.animating()) {
				_pinnedShiftAnimation.start();
			}
		}
		finishReorderPinned();
	}

	auto collapsedPressed = _collapsedPressed;
	setCollapsedPressed(-1);
	const auto pressedTopicRootId = _pressedTopicJumpRootId;
	const auto pressedTopicJump = _pressedTopicJump;
	auto pressed = _pressed;
	clearPressed();
	auto hashtagPressed = _hashtagPressed;
	setHashtagPressed(-1);
	auto hashtagDeletePressed = _hashtagDeletePressed;
	_hashtagDeletePressed = false;
	auto filteredPressed = _filteredPressed;
	setFilteredPressed(-1, false);
	auto peerSearchPressed = _peerSearchPressed;
	setPeerSearchPressed(-1);
	auto searchedPressed = _searchedPressed;
	setSearchedPressed(-1);
	if (wasDragging) {
		selectByMouse(globalPosition);
	}
	updateSelectedRow();
	if (!wasDragging && button == Qt::LeftButton) {
		if ((collapsedPressed >= 0 && collapsedPressed == _collapsedSelected)
			|| (pressed
				&& pressed == _selected
				&& pressedTopicJump == _selectedTopicJump)
			|| (hashtagPressed >= 0
				&& hashtagPressed == _hashtagSelected
				&& hashtagDeletePressed == _hashtagDeleteSelected)
			|| (filteredPressed >= 0 && filteredPressed == _filteredSelected)
			|| (peerSearchPressed >= 0
				&& peerSearchPressed == _peerSearchSelected)
			|| (searchedPressed >= 0
				&& searchedPressed == _searchedSelected)) {
			chooseRow(modifiers, pressedTopicRootId);
		}
	}
}

void InnerWidget::setCollapsedPressed(int pressed) {
	if (_collapsedPressed != pressed) {
		if (_collapsedPressed >= 0) {
			_collapsedRows[_collapsedPressed]->row.stopLastRipple();
		}
		_collapsedPressed = pressed;
	}
}

void InnerWidget::setPressed(Row *pressed, bool pressedTopicJump) {
	if (_pressed != pressed || (pressed && _pressedTopicJump != pressedTopicJump)) {
		if (_pressed) {
			_pressed->stopLastRipple();
		}
		_pressed = pressed;
		if (pressed || !pressedTopicJump) {
			_pressedTopicJump = pressedTopicJump;
			const auto history = pressedTopicJump
				? pressed->history()
				: nullptr;
			const auto item = history ? history->chatListMessage() : nullptr;
			_pressedTopicJumpRootId = item ? item->topicRootId() : MsgId();
		}
	}
}

void InnerWidget::clearPressed() {
	setPressed(nullptr, false);
}

void InnerWidget::setHashtagPressed(int pressed) {
	if (base::in_range(_hashtagPressed, 0, _hashtagResults.size())) {
		_hashtagResults[_hashtagPressed]->row.stopLastRipple();
	}
	_hashtagPressed = pressed;
}

void InnerWidget::setFilteredPressed(int pressed, bool pressedTopicJump) {
	if (_filteredPressed != pressed
		|| (pressed >= 0 && _pressedTopicJump != pressedTopicJump)) {
		if (base::in_range(_filteredPressed, 0, _filterResults.size())) {
			_filterResults[_filteredPressed].row->stopLastRipple();
		}
		_filteredPressed = pressed;
		if (pressed >= 0 || !pressedTopicJump) {
			_pressedTopicJump = pressedTopicJump;
			const auto history = pressedTopicJump
				? _filterResults[pressed].row->history()
				: nullptr;
			const auto item = history ? history->chatListMessage() : nullptr;
			_pressedTopicJumpRootId = item ? item->topicRootId() : MsgId();
		}
	}
}

void InnerWidget::setPeerSearchPressed(int pressed) {
	if (base::in_range(_peerSearchPressed, 0, _peerSearchResults.size())) {
		_peerSearchResults[_peerSearchPressed]->row.stopLastRipple();
	}
	_peerSearchPressed = pressed;
}

void InnerWidget::setSearchedPressed(int pressed) {
	if (base::in_range(_searchedPressed, 0, _searchResults.size())) {
		_searchResults[_searchedPressed]->stopLastRipple();
	}
	_searchedPressed = pressed;
}

void InnerWidget::resizeEvent(QResizeEvent *e) {
	resizeEmptyLabel();
	moveCancelSearchButtons();
}

void InnerWidget::moveCancelSearchButtons() {
	const auto widthForCancelButton = qMax(
		width(),
		st::columnMinimalWidthLeft - _narrowWidth);
	const auto left = widthForCancelButton - st::dialogsSearchInSkip - _cancelSearchInChat->width();
	const auto top = (st::dialogsSearchInHeight - st::dialogsCancelSearchInPeer.height) / 2;
	_cancelSearchInChat->moveToLeft(left, st::searchedBarHeight + top);
	const auto skip = _searchInChat ? (st::searchedBarHeight + st::dialogsSearchInHeight + st::lineWidth) : 0;
	_cancelSearchFromUser->moveToLeft(left, skip + top);
}

void InnerWidget::dialogRowReplaced(
		Row *oldRow,
		Row *newRow) {
	auto found = false;
	if (_state == WidgetState::Filtered) {
		auto top = 0;
		for (auto i = _filterResults.begin(); i != _filterResults.end();) {
			if (i->row == oldRow) { // this row is shown in filtered and maybe is in contacts!
				found = true;
				top = i->top;
				if (!newRow) {
					i = _filterResults.erase(i);
					continue;
				}
				i->row = newRow;
			}
			if (found) {
				i->top = top;
				top += i->row->height();
			}
			++i;
		}
	}
	if (_selected == oldRow) {
		_selected = newRow;
	}
	if (_pressed == oldRow) {
		setPressed(newRow, _pressedTopicJump);
	}
	if (_dragging == oldRow) {
		if (newRow) {
			_dragging = newRow;
		} else {
			stopReorderPinned();
		}
	}
	if (found) {
		refresh();
	}
}

void InnerWidget::handleChatListEntryRefreshes() {
	using Event = Data::Session::ChatListEntryRefresh;
	session().data().chatListEntryRefreshes(
	) | rpl::filter([=](const Event &event) {
		if (event.filterId != _filterId) {
			return false;
		} else if (const auto topic = event.key.topic()) {
			return (topic->forum() == _openedForum);
		} else {
			return !_openedForum;
		}
	}) | rpl::start_with_next([=](const Event &event) {
		const auto offset = dialogsOffset();
		const auto from = offset + event.moved.from;
		const auto to = offset + event.moved.to;
		const auto &key = event.key;
		const auto entry = key.entry();

		// Don't jump in chats list scroll position while dragging.
		if (!_dragging
			&& (from != to)
			&& (_state == WidgetState::Default)
			&& (key.topic()
				? (key.topic()->forum() == _openedForum)
				: (entry->folder() == _openedFolder))) {
			_dialogMoved.fire({ from, to });
		}

		if (event.existenceChanged) {
			if (!entry->inChatList()) {
				if (key == _menuRow.key && _menu) {
					InvokeQueued(this, [=] { _menu = nullptr; });
				}
				if (_selected && _selected->key() == key) {
					_selected = nullptr;
				}
				if (_pressed && _pressed->key() == key) {
					clearPressed();
				}
				const auto i = ranges::find(
					_filterResults,
					key,
					&FilterResult::key);
				if (i != _filterResults.end()) {
					if (_filteredSelected == (i - _filterResults.begin())
						&& (i + 1) == _filterResults.end()) {
						_filteredSelected = -1;
					}
					_filterResults.erase(i);
				}
				_updated.fire({});
			}
			refresh();
		} else if (_state == WidgetState::Default && from != to) {
			update(
				0,
				std::min(from, to),
				width(),
				std::abs(from - to) + event.moved.height);
		}
	}, lifetime());
}

void InnerWidget::repaintCollapsedFolderRow(not_null<Data::Folder*> folder) {
	for (auto i = 0, l = int(_collapsedRows.size()); i != l; ++i) {
		if (_collapsedRows[i]->folder == folder) {
			update(0, i * st::dialogsImportantBarHeight, width(), st::dialogsImportantBarHeight);
			return;
		}
	}
}

int InnerWidget::defaultRowTop(not_null<Row*> row) const {
	const auto index = row->index();
	auto top = dialogsOffset();
	if (base::in_range(index, 0, _pinnedRows.size())) {
		top += qRound(_pinnedRows[index].yadd.current());
	}
	return top + row->top();
}

void InnerWidget::repaintDialogRow(
		FilterId filterId,
		not_null<Row*> row) {
	if (_state == WidgetState::Default) {
		if (_filterId == filterId) {
			if (const auto folder = row->folder()) {
				repaintCollapsedFolderRow(folder);
			}
			update(0, defaultRowTop(row), width(), row->height());
		}
	} else if (_state == WidgetState::Filtered) {
		if (!filterId) {
			for (auto i = 0, l = int(_filterResults.size()); i != l; ++i) {
				const auto &result = _filterResults[i];
				if (result.key() == row->key()) {
					update(
						0,
						filteredOffset() + result.top,
						width(),
						result.row->height());
					break;
				}
			}
		}
	}
}

void InnerWidget::repaintDialogRow(RowDescriptor row) {
	updateDialogRow(row);
}

void InnerWidget::refreshDialogRow(RowDescriptor row) {
	if (row.fullId) {
		for (const auto &result : _searchResults) {
			if (result->item()->fullId() == row.fullId) {
				result->itemView().itemInvalidated(result->item());
			}
		}
	}
	repaintDialogRow(row);
}

void InnerWidget::updateSearchResult(not_null<PeerData*> peer) {
	if (_state == WidgetState::Filtered) {
		const auto i = ranges::find(
			_peerSearchResults,
			peer,
			&PeerSearchResult::peer);
		if (i != end(_peerSearchResults)) {
			const auto top = peerSearchOffset();
			const auto index = (i - begin(_peerSearchResults));
			rtlupdate(
				0,
				top + index * st::dialogsRowHeight,
				width(),
				st::dialogsRowHeight);
		}
	}
}

void InnerWidget::updateDialogRow(
		RowDescriptor row,
		QRect updateRect,
		UpdateRowSections sections) {
	if (IsServerMsgId(-row.fullId.msg)) {
		if (const auto peer = row.key.peer()) {
			if (const auto from = peer->migrateFrom()) {
				if (const auto migrated = from->owner().historyLoaded(from)) {
					row = RowDescriptor(
						migrated,
						FullMsgId(from->id, -row.fullId.msg));
				}
			}
		}
	}

	const auto updateRow = [&](int rowTop, int rowHeight) {
		if (!updateRect.isEmpty()) {
			rtlupdate(updateRect.translated(0, rowTop));
		} else {
			rtlupdate(0, rowTop, width(), rowHeight);
		}
	};
	if (_state == WidgetState::Default) {
		if (sections & UpdateRowSection::Default) {
			if (const auto folder = row.key.folder()) {
				repaintCollapsedFolderRow(folder);
			}
			if (const auto dialog = _shownList->getRow(row.key)) {
				const auto position = dialog->index();
				auto top = dialogsOffset();
				if (base::in_range(position, 0, _pinnedRows.size())) {
					top += qRound(_pinnedRows[position].yadd.current());
				}
				updateRow(top + dialog->top(), dialog->height());
			}
		}
	} else if (_state == WidgetState::Filtered) {
		if ((sections & UpdateRowSection::Filtered)
			&& !_filterResults.empty()) {
			for (const auto &result : _filterResults) {
				if (result.key() == row.key) {
					updateRow(
						filteredOffset() + result.top,
						result.row->height());
					break;
				}
			}
		}
		if ((sections & UpdateRowSection::PeerSearch)
			&& !_peerSearchResults.empty()) {
			if (const auto peer = row.key.peer()) {
				const auto rowHeight = st::dialogsRowHeight;
				auto index = 0;
				for (const auto &result : _peerSearchResults) {
					if (result->peer == peer) {
						updateRow(
							peerSearchOffset() + index * rowHeight,
							rowHeight);
						break;
					}
					++index;
				}
			}
		}
		if ((sections & UpdateRowSection::MessageSearch)
			&& !_searchResults.empty()) {
			const auto add = searchedOffset();
			auto index = 0;
			for (const auto &result : _searchResults) {
				if (isSearchResultActive(result.get(), row)) {
					updateRow(add + index * _st->height, _st->height);
					break;
				}
				++index;
			}
		}
	}
}

void InnerWidget::enterEventHook(QEnterEvent *e) {
	setMouseTracking(true);
}

Row *InnerWidget::shownRowByKey(Key key) {
	const auto entry = key.entry();
	if (_openedForum) {
		const auto topic = entry->asTopic();
		if (!topic || topic->forum() != _openedForum) {
			return nullptr;
		}
	} else if (_openedFolder) {
		const auto history = entry->asHistory();
		if (!history || history->folder() != _openedFolder) {
			return nullptr;
		}
	} else {
		const auto history = entry->asHistory();
		if (!entry->asFolder() && (!history || history->folder())) {
			return nullptr;
		}
	}
	const auto links = entry->chatListLinks(FilterId());
	return links ? links->main.get() : nullptr;
}

void InnerWidget::updateSelectedRow(Key key) {
	if (_state == WidgetState::Default) {
		if (key) {
			const auto row = shownRowByKey(key);
			if (!row) {
				return;
			}
			auto position = row->index();
			auto top = dialogsOffset();
			if (base::in_range(position, 0, _pinnedRows.size())) {
				top += qRound(_pinnedRows[position].yadd.current());
			}
			update(0, top + row->top(), width(), row->height());
		} else if (_selected) {
			update(0, dialogsOffset() + _selected->top(), width(), _selected->height());
		} else if (_collapsedSelected >= 0) {
			update(0, _collapsedSelected * st::dialogsImportantBarHeight, width(), st::dialogsImportantBarHeight);
		}
	} else if (_state == WidgetState::Filtered) {
		if (key) {
			for (auto i = 0, l = int(_filterResults.size()); i != l; ++i) {
				const auto &result = _filterResults[i];
				if (result.key() == key) {
					update(0, filteredOffset() + result.top, width(), result.row->height());
					break;
				}
			}
		} else if (_hashtagSelected >= 0) {
			update(0, _hashtagSelected * st::mentionHeight, width(), st::mentionHeight);
		} else if (_filteredSelected >= 0) {
			if (_filteredSelected < _filterResults.size()) {
				const auto &result = _filterResults[_filteredSelected];
				update(0, filteredOffset() + result.top, width(), result.row->height());
			}
		} else if (_peerSearchSelected >= 0) {
			update(0, peerSearchOffset() + _peerSearchSelected * st::dialogsRowHeight, width(), st::dialogsRowHeight);
		} else if (_searchedSelected >= 0) {
			update(0, searchedOffset() + _searchedSelected * _st->height, width(), _st->height);
		}
	}
}

void InnerWidget::refreshShownList() {
	const auto list = _openedForum
		? _openedForum->topicsList()->indexed()
		: _filterId
		? session().data().chatsFilters().chatsList(_filterId)->indexed()
		: session().data().chatsList(_openedFolder)->indexed();
	if (_shownList != list) {
		_shownList = list;
		_shownList->updateHeights(_narrowRatio);
	}
}

void InnerWidget::leaveEventHook(QEvent *e) {
	setMouseTracking(false);
	clearSelection();
}

void InnerWidget::dragLeft() {
	setMouseTracking(false);
	clearSelection();
}

FilterId InnerWidget::filterId() const {
	return _filterId;
}

void InnerWidget::clearSelection() {
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;
	_lastRowLocalMouseX = -1;
	if (isSelected()) {
		updateSelectedRow();
		_collapsedSelected = -1;
		_selected = nullptr;
		_filteredSelected
			= _searchedSelected
			= _peerSearchSelected
			= _hashtagSelected
			= -1;
		setCursor(style::cur_default);
	}
}

void InnerWidget::fillSupportSearchMenu(not_null<Ui::PopupMenu*> menu) {
	const auto all = session().settings().supportAllSearchResults();
	const auto text = all ? "Only one from chat" : "Show all messages";
	menu->addAction(text, [=] {
		session().settings().setSupportAllSearchResults(!all);
		session().saveSettingsDelayed();
	});
}

void InnerWidget::fillArchiveSearchMenu(not_null<Ui::PopupMenu*> menu) {
	const auto folder = session().data().folderLoaded(Data::Folder::kId);
	if (!folder
		|| !folder->chatsList()->fullSize().current()
		|| _searchInChat) {
		return;
	}
	const auto skip = session().settings().skipArchiveInSearch();
	const auto text = skip
		? tr::lng_dialogs_show_archive_in_search(tr::now)
		: tr::lng_dialogs_skip_archive_in_search(tr::now);
	menu->addAction(text, [=] {
		session().settings().setSkipArchiveInSearch(!skip);
		session().saveSettingsDelayed();
	});
}

void InnerWidget::contextMenuEvent(QContextMenuEvent *e) {
	_menu = nullptr;

	if (e->reason() == QContextMenuEvent::Mouse) {
		selectByMouse(e->globalPos());
	}

	const auto row = [&]() -> RowDescriptor {
		if (_state == WidgetState::Default) {
			if (_selected) {
				return { _selected->key(), FullMsgId() };
			} else if (base::in_range(_collapsedSelected, 0, _collapsedRows.size())) {
				if (const auto folder = _collapsedRows[_collapsedSelected]->folder) {
					return { folder, FullMsgId() };
				}
			}
		} else if (_state == WidgetState::Filtered) {
			if (base::in_range(_filteredSelected, 0, _filterResults.size())) {
				return { _filterResults[_filteredSelected].key(), FullMsgId() };
			} else if (base::in_range(_searchedSelected, 0, _searchResults.size())) {
				return {
					_searchResults[_searchedSelected]->item()->history(),
					_searchResults[_searchedSelected]->item()->fullId()
				};
			}
		}
		return RowDescriptor();
	}();
	if (!row.key) return;

	_menuRow = row;
	if (_pressButton != Qt::LeftButton) {
		mousePressReleased(e->globalPos(), _pressButton, e->modifiers());
	}

	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		row.fullId ? st::defaultPopupMenu : st::popupMenuExpandedSeparator);
	if (row.fullId) {
		if (session().supportMode()) {
			fillSupportSearchMenu(_menu.get());
		} else {
			fillArchiveSearchMenu(_menu.get());
		}
	} else {
		const auto addAction = Ui::Menu::CreateAddActionCallback(_menu);
		Window::FillDialogsEntryMenu(
			_controller,
			Dialogs::EntryState{
				.key = row.key,
				.section = Dialogs::EntryState::Section::ContextMenu,
				.filterId = _filterId,
			},
			addAction);
	}
	QObject::connect(_menu.get(), &QObject::destroyed, [=] {
		if (_menuRow.key) {
			updateDialogRow(base::take(_menuRow));
		}
		const auto globalPosition = QCursor::pos();
		if (rect().contains(mapFromGlobal(globalPosition))) {
			setMouseTracking(true);
			selectByMouse(globalPosition);
		}
	});
	if (_menu->empty()) {
		_menu = nullptr;
	} else {
		_menu->popup(e->globalPos());
		e->accept();
	}
}

void InnerWidget::parentGeometryChanged() {
	const auto globalPosition = QCursor::pos();
	if (rect().contains(mapFromGlobal(globalPosition))) {
		setMouseTracking(true);
		if (_mouseSelection) {
			selectByMouse(globalPosition);
		}
	}
}

void InnerWidget::applyFilterUpdate(QString newFilter, bool force) {
	const auto mentionsSearch = (newFilter == u"@"_q);
	const auto words = mentionsSearch
		? QStringList(newFilter)
		: TextUtilities::PrepareSearchWords(newFilter);
	newFilter = words.isEmpty() ? QString() : words.join(' ');
	if (newFilter != _filter || force) {
		_filter = newFilter;
		if (_filter.isEmpty() && !_searchFromPeer) {
			clearFilter();
		} else {
			setState(WidgetState::Filtered);
			_waitingForSearch = true;
			_filterResults.clear();
			_filterResultsGlobal.clear();
			const auto append = [&](not_null<IndexedList*> list) {
				const auto results = list->filtered(words);
				auto top = filteredHeight();
				auto i = _filterResults.insert(
					end(_filterResults),
					begin(results),
					end(results));
				for (const auto e = end(_filterResults); i != e; ++i) {
					i->top = top;
					i->row->recountHeight(_narrowRatio);
					top += i->row->height();
				}
			};
			if (!_searchInChat && !_searchFromPeer && !words.isEmpty()) {
				if (_openedForum) {
					append(_openedForum->topicsList()->indexed());
				} else {
					append(session().data().chatsList()->indexed());
					const auto id = Data::Folder::kId;
					if (const auto add = session().data().folderLoaded(id)) {
						append(add->chatsList()->indexed());
					}
					append(session().data().contactsNoChatsList());
				}
			}
			refresh(true);
		}
		clearMouseSelection(true);
	}
	if (_state != WidgetState::Default) {
		_searchMessages.fire({});
	}
}

void InnerWidget::onHashtagFilterUpdate(QStringView newFilter) {
	if (newFilter.isEmpty() || newFilter.at(0) != '#' || _searchInChat) {
		_hashtagFilter = QString();
		if (!_hashtagResults.empty()) {
			_hashtagResults.clear();
			refresh(true);
			clearMouseSelection(true);
		}
		return;
	}
	_hashtagFilter = newFilter.toString();
	if (cRecentSearchHashtags().isEmpty() && cRecentWriteHashtags().isEmpty()) {
		session().local().readRecentHashtagsAndBots();
	}
	auto &recent = cRecentSearchHashtags();
	_hashtagResults.clear();
	if (!recent.isEmpty()) {
		_hashtagResults.reserve(qMin(recent.size(), kHashtagResultsLimit));
		for (const auto &tag : recent) {
			if (tag.first.startsWith(base::StringViewMid(_hashtagFilter, 1), Qt::CaseInsensitive)
				&& tag.first.size() + 1 != newFilter.size()) {
				_hashtagResults.push_back(std::make_unique<HashtagResult>(tag.first));
				if (_hashtagResults.size() == kHashtagResultsLimit) break;
			}
		}
	}
	refresh(true);
	clearMouseSelection(true);
}

void InnerWidget::appendToFiltered(Key key) {
	for (const auto &row : _filterResults) {
		if (row.key() == key) {
			return;
		}
	}
	auto row = std::make_unique<Row>(key, 0, 0);
	row->recountHeight(_narrowRatio);
	const auto [i, ok] = _filterResultsGlobal.emplace(key, std::move(row));
	const auto height = filteredHeight();
	_filterResults.emplace_back(i->second.get());
	_filterResults.back().top = height;
	trackSearchResultsHistory(key.owningHistory());
}

InnerWidget::~InnerWidget() {
	session().data().stories().decrementPreloadingMainSources();
	clearSearchResults();
}

void InnerWidget::clearSearchResults(bool clearPeerSearchResults) {
	if (clearPeerSearchResults) _peerSearchResults.clear();
	_searchResults.clear();
	_searchResultsLifetime.destroy();
	_searchResultsHistories.clear();
	_searchedCount = _searchedMigratedCount = 0;
}

void InnerWidget::trackSearchResultsHistory(not_null<History*> history) {
	if (!_searchResultsHistories.emplace(history).second) {
		return;
	}
	const auto channel = history->peer->asChannel();
	if (!channel || channel->isBroadcast()) {
		return;
	}
	channel->flagsValue(
	) | rpl::skip(
		1
	) | rpl::filter([=](const ChannelData::Flags::Change &change) {
		return (change.diff & ChannelDataFlag::Forum);
	}) | rpl::start_with_next([=] {
		for (const auto &row : _searchResults) {
			if (row->item()->history()->peer == channel) {
				row->invalidateTopic();
			}
		}
		auto removed = false;
		for (auto i = begin(_filterResultsGlobal)
			; i != end(_filterResultsGlobal);) {
			if (const auto topic = i->first.topic()) {
				if (topic->channel() == channel) {
					removed = true;
					_filterResults.erase(
						ranges::remove(
							_filterResults,
							i->first,
							&FilterResult::key),
						end(_filterResults));
					i = _filterResultsGlobal.erase(i);
					continue;
				}
			}
			++i;
		}
		if (removed) {
			refresh();
			clearMouseSelection(true);
		}
		update();
	}, _searchResultsLifetime);

	if (const auto forum = channel->forum()) {
		forum->topicDestroyed(
		) | rpl::start_with_next([=](not_null<Data::ForumTopic*> topic) {
			auto removed = false;
			const auto sfrom = ranges::remove(
				_searchResults,
				topic.get(),
				&FakeRow::topic);
			if (sfrom != end(_searchResults)) {
				_searchResults.erase(sfrom, end(_searchResults));
				removed = true;
			}
			const auto ffrom = ranges::remove(
				_filterResults,
				Key(topic),
				&FilterResult::key);
			if (ffrom != end(_filterResults)) {
				_filterResults.erase(ffrom, end(_filterResults));
				removed = true;
			}
			_filterResultsGlobal.erase(Key(topic));
			if (removed) {
				refresh();
				clearMouseSelection(true);
			}
		}, _searchResultsLifetime);
	}
}

Data::Thread *InnerWidget::updateFromParentDrag(QPoint globalPosition) {
	selectByMouse(globalPosition);

	const auto fromRow = [](Row *row) {
		return row ? row->thread() : nullptr;
	};
	if (_state == WidgetState::Default) {
		return fromRow(_selected);
	} else if (_state == WidgetState::Filtered) {
		if (base::in_range(_filteredSelected, 0, _filterResults.size())) {
			return fromRow(_filterResults[_filteredSelected].row);
		} else if (base::in_range(_peerSearchSelected, 0, _peerSearchResults.size())) {
			return session().data().history(
				_peerSearchResults[_peerSearchSelected]->peer);
		} else if (base::in_range(_searchedSelected, 0, _searchResults.size())) {
			if (const auto item = _searchResults[_searchedSelected]->item()) {
				if (const auto topic = item->topic()) {
					return topic;
				}
				return item->history();
			}
		}
	}
	return nullptr;
}

void InnerWidget::setLoadMoreCallback(Fn<void()> callback) {
	_loadMoreCallback = std::move(callback);
}

void InnerWidget::setLoadMoreFilteredCallback(Fn<void()> callback) {
	_loadMoreFilteredCallback = std::move(callback);
}

auto InnerWidget::chosenRow() const -> rpl::producer<ChosenRow> {
	return _chosenRow.events();
}

rpl::producer<> InnerWidget::updated() const {
	return _updated.events();
}

rpl::producer<int> InnerWidget::scrollByDeltaRequests() const {
	return _draggingScroll.scrolls();
}

rpl::producer<> InnerWidget::listBottomReached() const {
	return _listBottomReached.events();
}

rpl::producer<> InnerWidget::cancelSearchFromUserRequests() const {
	return _cancelSearchFromUser->clicks() | rpl::to_empty;
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::mustScrollTo() const {
	return _mustScrollTo.events();
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::dialogMoved() const {
	return _dialogMoved.events();
}

rpl::producer<> InnerWidget::searchMessages() const {
	return _searchMessages.events();
}

rpl::producer<> InnerWidget::cancelSearchInChatRequests() const {
	return _cancelSearchInChat->clicks() | rpl::to_empty;
}

rpl::producer<QString> InnerWidget::completeHashtagRequests() const {
	return _completeHashtagRequests.events();
}

rpl::producer<> InnerWidget::refreshHashtagsRequests() const {
	return _refreshHashtagsRequests.events();
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	preloadRowsData();
	const auto loadTill = _visibleTop
		+ PreloadHeightsCount * (_visibleBottom - _visibleTop);
	if (_state == WidgetState::Filtered && loadTill >= peerSearchOffset()) {
		if (_loadMoreFilteredCallback) {
			_loadMoreFilteredCallback();
		}
	}
	if (loadTill >= height()) {
		if (_loadMoreCallback) {
			_loadMoreCallback();
		}
	}
}

void InnerWidget::itemRemoved(not_null<const HistoryItem*> item) {
	int wasCount = _searchResults.size();
	for (auto i = _searchResults.begin(); i != _searchResults.end();) {
		if ((*i)->item() == item) {
			i = _searchResults.erase(i);
			if (item->history() == _searchInMigrated) {
				if (_searchedMigratedCount > 0) --_searchedMigratedCount;
			} else {
				if (_searchedCount > 0) --_searchedCount;
			}
		} else {
			++i;
		}
	}
	if (wasCount != _searchResults.size()) {
		refresh();
	}
}

bool InnerWidget::uniqueSearchResults() const {
	return _controller->uniqueChatsInSearchResults();
}

bool InnerWidget::hasHistoryInResults(not_null<History*> history) const {
	using Result = std::unique_ptr<FakeRow>;
	const auto inSearchResults = ranges::find(
		_searchResults,
		history,
		[](const Result &result) { return result->item()->history(); }
	) != end(_searchResults);
	if (inSearchResults) {
		return true;
	}
	const auto inFilteredResults = ranges::find(
		_filterResults,
		Key(history),
		&FilterResult::key
	) != end(_filterResults);
	if (inFilteredResults) {
		return true;
	}
	const auto inPeerSearchResults = ranges::find(
		_peerSearchResults,
		history->peer,
		[](const auto &result) { return result->peer; }
	) != end(_peerSearchResults);
	if (inPeerSearchResults) {
		return true;
	}
	return false;
}

void InnerWidget::searchReceived(
		std::vector<not_null<HistoryItem*>> messages,
		HistoryItem *inject,
		SearchRequestType type,
		int fullCount) {
	const auto uniquePeers = uniqueSearchResults();
	if (type == SearchRequestType::FromStart || type == SearchRequestType::PeerFromStart) {
		clearSearchResults(false);
	}
	const auto isMigratedSearch = (type == SearchRequestType::MigratedFromStart)
		|| (type == SearchRequestType::MigratedFromOffset);

	const auto key = (!_openedForum || _searchInChat.topic())
		? _searchInChat
		: Key(_openedForum->history());
	if (inject
		&& (!_searchInChat
			|| inject->history() == _searchInChat.history())) {
		Assert(_searchResults.empty());
		const auto index = int(_searchResults.size());
		_searchResults.push_back(
			std::make_unique<FakeRow>(
				key,
				inject,
				[=] { repaintSearchResult(index); }));
		trackSearchResultsHistory(inject->history());
		++fullCount;
	}
	for (const auto &item : messages) {
		const auto history = item->history();
		if (!uniquePeers || !hasHistoryInResults(history)) {
			const auto index = int(_searchResults.size());
			_searchResults.push_back(
				std::make_unique<FakeRow>(
					key,
					item,
					[=] { repaintSearchResult(index); }));
			trackSearchResultsHistory(history);
			if (uniquePeers && !history->unreadCountKnown()) {
				history->owner().histories().requestDialogEntry(history);
			}
		}
	}
	if (isMigratedSearch) {
		_searchedMigratedCount = fullCount;
	} else {
		_searchedCount = fullCount;
	}
	if (_waitingForSearch
		&& (!_searchResults.empty()
			|| !_searchInMigrated
			|| type == SearchRequestType::MigratedFromStart
			|| type == SearchRequestType::MigratedFromOffset)) {
		_waitingForSearch = false;
	}

	refresh();
}

void InnerWidget::peerSearchReceived(
		const QString &query,
		const QVector<MTPPeer> &my,
		const QVector<MTPPeer> &result) {
	if (_state != WidgetState::Filtered) {
		return;
	}

	_peerSearchQuery = query.toLower().trimmed();
	_peerSearchResults.clear();
	_peerSearchResults.reserve(result.size());
	for	(const auto &mtpPeer : my) {
		if (const auto peer = session().data().peerLoaded(peerFromMTP(mtpPeer))) {
			appendToFiltered(peer->owner().history(peer));
		} else {
			LOG(("API Error: "
				"user %1 was not loaded in InnerWidget::peopleReceived()"
				).arg(peerFromMTP(mtpPeer).value));
		}
	}
	for (const auto &mtpPeer : result) {
		if (const auto peer = session().data().peerLoaded(peerFromMTP(mtpPeer))) {
			if (const auto history = peer->owner().historyLoaded(peer)) {
				if (history->inChatList()) {
					continue; // skip existing chats
				}
			}
			_peerSearchResults.push_back(std::make_unique<PeerSearchResult>(
				peer));
		} else {
			LOG(("API Error: "
				"user %1 was not loaded in InnerWidget::peopleReceived()"
				).arg(peerFromMTP(mtpPeer).value));
		}
	}
	refresh();
}

Data::Folder *InnerWidget::shownFolder() const {
	return _openedFolder;
}

Data::Forum *InnerWidget::shownForum() const {
	return _openedForum;
}

bool InnerWidget::needCollapsedRowsRefresh() const {
	const auto archive = !_shownList->empty()
		? _shownList->begin()->get()->folder()
		: nullptr;
	const auto collapsedHasArchive = !_collapsedRows.empty()
		&& (_collapsedRows.back()->folder != nullptr);
	const auto archiveIsCollapsed = (archive != nullptr)
		&& session().settings().archiveCollapsed();
	const auto archiveIsInMainMenu = (archive != nullptr)
		&& session().settings().archiveInMainMenu();
	return archiveIsInMainMenu
		? (collapsedHasArchive || !_skipTopDialog)
		: archiveIsCollapsed
			? (!collapsedHasArchive || !_skipTopDialog)
			: (collapsedHasArchive || _skipTopDialog);
}

void InnerWidget::editOpenedFilter() {
	if (_filterId > 0) {
		EditExistingFilter(_controller, _filterId);
	}
}

void InnerWidget::refresh(bool toTop) {
	if (!_geometryInited) {
		return;
	} else if (needCollapsedRowsRefresh()) {
		return refreshWithCollapsedRows(toTop);
	}
	refreshEmptyLabel();
	auto h = 0;
	if (_state == WidgetState::Default) {
		if (_shownList->empty()) {
			h = st::dialogsEmptyHeight;
		} else {
			h = dialogsOffset() + _shownList->height();
		}
	} else if (_state == WidgetState::Filtered) {
		if (_waitingForSearch) {
			h = searchedOffset() + (_searchResults.size() * _st->height) + ((_searchResults.empty() && !_searchInChat) ? -st::searchedBarHeight : 0);
		} else {
			h = searchedOffset() + (_searchResults.size() * _st->height);
		}
	}
	resize(width(), h);
	if (toTop) {
		stopReorderPinned();
		jumpToTop();
		preloadRowsData();
	}
	_controller->setDialogsListDisplayForced(
		_searchInChat || !_filter.isEmpty());
	update();
}

void InnerWidget::refreshEmptyLabel() {
	const auto data = &session().data();
	const auto state = !_shownList->empty()
		? EmptyState::None
		: _openedForum
		? (_openedForum->topicsList()->loaded()
			? EmptyState::EmptyForum
			: EmptyState::Loading)
		: (!_filterId && data->contactsLoaded().current())
		? EmptyState::NoContacts
		: (_filterId > 0) && data->chatsList()->loaded()
		? EmptyState::EmptyFolder
		: EmptyState::Loading;
	if (state == EmptyState::None) {
		_emptyState = state;
		_empty.destroy();
		return;
	} else if (_emptyState == state) {
		_empty->setVisible(_state == WidgetState::Default);
		return;
	}
	_emptyState = state;
	auto phrase = (state == EmptyState::NoContacts)
		? tr::lng_no_chats()
		: (state == EmptyState::EmptyFolder)
		? tr::lng_no_chats_filter()
		: (state == EmptyState::EmptyForum)
		? tr::lng_forum_no_topics()
		: tr::lng_contacts_loading();
	auto link = (state == EmptyState::NoContacts)
		? tr::lng_add_contact_button()
		: (state == EmptyState::EmptyFolder)
		? tr::lng_filters_context_edit()
		: (state == EmptyState::EmptyForum)
		? tr::lng_forum_create_topic()
		: rpl::single(QString());
	auto full = rpl::combine(
		std::move(phrase),
		std::move(link)
	) | rpl::map([](const QString &phrase, const QString &link) {
		auto result = Ui::Text::WithEntities(phrase);
		if (!link.isEmpty()) {
			result.append("\n\n").append(Ui::Text::Link(link));
		}
		return result;
	});
	_empty.create(this, std::move(full), st::dialogsEmptyLabel);
	resizeEmptyLabel();
	_empty->overrideLinkClickHandler([=] {
		if (_emptyState == EmptyState::NoContacts) {
			_controller->showAddContact();
		} else if (_emptyState == EmptyState::EmptyFolder) {
			editOpenedFilter();
		} else if (_emptyState == EmptyState::EmptyForum) {
			_controller->show(
				Box(NewForumTopicBox, _controller, _openedForum->history()));
		}
	});
	_empty->setVisible(_state == WidgetState::Default);
}

void InnerWidget::resizeEmptyLabel() {
	if (!_empty) {
		return;
	}
	const auto skip = st::dialogsEmptySkip;
	_empty->resizeToWidth(width() - 2 * skip);
	_empty->move(skip, (st::dialogsEmptyHeight - _empty->height()) / 2);
}

void InnerWidget::clearMouseSelection(bool clearSelection) {
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;
	_lastRowLocalMouseX = -1;
	if (clearSelection) {
		if (_state == WidgetState::Default) {
			_collapsedSelected = -1;
			_selected = nullptr;
		} else if (_state == WidgetState::Filtered) {
			_filteredSelected
				= _peerSearchSelected
				= _searchedSelected
				= _hashtagSelected = -1;
		}
		setCursor(style::cur_default);
	}
}

WidgetState InnerWidget::state() const {
	return _state;
}

bool InnerWidget::hasFilteredResults() const {
	return !_filterResults.empty() && _hashtagResults.empty();
}

void InnerWidget::searchInChat(Key key, PeerData *from) {
	_searchInMigrated = nullptr;
	if (const auto peer = key.peer()) {
		if (const auto migrateTo = peer->migrateTo()) {
			return searchInChat(peer->owner().history(migrateTo), from);
		} else if (const auto migrateFrom = peer->migrateFrom()) {
			_searchInMigrated = peer->owner().history(migrateFrom);
		}
	}
	_searchInChat = key;
	_searchFromPeer = from;
	if (_searchInChat) {
		onHashtagFilterUpdate(QStringView());
		_cancelSearchInChat->show();
	} else {
		_cancelSearchInChat->hide();
	}
	if (_searchFromPeer) {
		_cancelSearchFromUser->show();
		_searchFromUserUserpic = _searchFromPeer->createUserpicView();
	} else {
		_cancelSearchFromUser->hide();
		_searchFromUserUserpic = {};
	}
	if (_searchInChat || _searchFromPeer) {
		refreshSearchInChatLabel();
	}

	if (const auto peer = _searchInChat.peer()) {
		_searchInChatUserpic = peer->createUserpicView();
	} else {
		_searchInChatUserpic = {};
	}
	moveCancelSearchButtons();

	_controller->setDialogsListDisplayForced(
		_searchInChat || !_filter.isEmpty());
}

void InnerWidget::refreshSearchInChatLabel() {
	const auto dialog = [&] {
		if (const auto topic = _searchInChat.topic()) {
			return topic->title();
		} else if (const auto peer = _searchInChat.peer()) {
			if (peer->isSelf()) {
				return tr::lng_saved_messages(tr::now);
			} else if (peer->isRepliesChat()) {
				return tr::lng_replies_messages(tr::now);
			}
			return peer->name();
		}
		return QString();
	}();
	if (!dialog.isEmpty()) {
		_searchInChatText.setText(
			st::semiboldTextStyle,
			dialog,
			Ui::DialogTextOptions());
	}
	const auto from = _searchFromPeer ? _searchFromPeer->name() : QString();
	if (!from.isEmpty()) {
		const auto fromUserText = tr::lng_dlg_search_from(
			tr::now,
			lt_user,
			Ui::Text::Link(from),
			Ui::Text::WithEntities);
		_searchFromUserText.setMarkedText(
			st::dialogsSearchFromStyle,
			fromUserText,
			Ui::DialogTextOptions());
	}
}

void InnerWidget::repaintSearchResult(int index) {
	rtlupdate(
		0,
		searchedOffset() + index * _st->height,
		width(),
		_st->height);
}

void InnerWidget::clearFilter() {
	if (_state == WidgetState::Filtered || _searchInChat) {
		if (_searchInChat) {
			setState(WidgetState::Filtered);
			_waitingForSearch = true;
		} else {
			setState(WidgetState::Default);
		}
		_hashtagResults.clear();
		_filterResults.clear();
		_filterResultsGlobal.clear();
		_peerSearchResults.clear();
		_searchResults.clear();
		_filter = QString();
		refresh(true);
	}
}

void InnerWidget::setState(WidgetState state) {
	_state = state;
}

void InnerWidget::selectSkip(int32 direction) {
	clearMouseSelection();
	if (_state == WidgetState::Default) {
		const auto skip = _skipTopDialog ? 1 : 0;
		if (_collapsedRows.empty() && _shownList->size() <= skip) {
			return;
		}
		if (_collapsedSelected < 0 && !_selected) {
			if (!_collapsedRows.empty()) {
				_collapsedSelected = 0;
			} else {
				_selected = (_shownList->cbegin() + skip)->get();
			}
		} else {
			auto cur = (_collapsedSelected >= 0)
				? _collapsedSelected
				: int(_collapsedRows.size()
					+ (_shownList->cfind(_selected)
						- _shownList->cbegin()
						- skip));
			cur = std::clamp(
				cur + direction,
				0,
				static_cast<int>(_collapsedRows.size()
					+ _shownList->size()
					- skip
					- 1));
			if (cur < _collapsedRows.size()) {
				_collapsedSelected = cur;
				_selected = nullptr;
			} else {
				_collapsedSelected = -1;
				_selected = *(_shownList->cbegin() + skip + cur - _collapsedRows.size());
			}
		}
		scrollToDefaultSelected();
	} else if (_state == WidgetState::Filtered) {
		if (_hashtagResults.empty() && _filterResults.empty() && _peerSearchResults.empty() && _searchResults.empty()) {
			return;
		}
		if ((_hashtagSelected < 0 || _hashtagSelected >= _hashtagResults.size()) &&
			(_filteredSelected < 0 || _filteredSelected >= _filterResults.size()) &&
			(_peerSearchSelected < 0 || _peerSearchSelected >= _peerSearchResults.size()) &&
			(_searchedSelected < 0 || _searchedSelected >= _searchResults.size())) {
			if (_hashtagResults.empty() && _filterResults.empty() && _peerSearchResults.empty()) {
				_searchedSelected = 0;
			} else if (_hashtagResults.empty() && _filterResults.empty()) {
				_peerSearchSelected = 0;
			} else if (_hashtagResults.empty()) {
				_filteredSelected = 0;
			} else {
				_hashtagSelected = 0;
			}
		} else {
			int32 cur = base::in_range(_hashtagSelected, 0, _hashtagResults.size())
				? _hashtagSelected
				: (base::in_range(_filteredSelected, 0, _filterResults.size())
					? (_hashtagResults.size() + _filteredSelected)
					: (base::in_range(_peerSearchSelected, 0, _peerSearchResults.size())
						? (_peerSearchSelected + _filterResults.size() + _hashtagResults.size())
						: (_searchedSelected + _peerSearchResults.size() + _filterResults.size() + _hashtagResults.size())));
			cur = std::clamp(
				cur + direction,
				0,
				static_cast<int>(_hashtagResults.size()
					+ _filterResults.size()
					+ _peerSearchResults.size()
					+ _searchResults.size()) - 1);
			if (cur < _hashtagResults.size()) {
				_hashtagSelected = cur;
				_filteredSelected = _peerSearchSelected = _searchedSelected = -1;
			} else if (cur < _hashtagResults.size() + _filterResults.size()) {
				_filteredSelected = cur - _hashtagResults.size();
				_hashtagSelected = _peerSearchSelected = _searchedSelected = -1;
			} else if (cur < _hashtagResults.size() + _filterResults.size() + _peerSearchResults.size()) {
				_peerSearchSelected = cur - _hashtagResults.size() - _filterResults.size();
				_hashtagSelected = _filteredSelected = _searchedSelected = -1;
			} else {
				_hashtagSelected = _filteredSelected = _peerSearchSelected = -1;
				_searchedSelected = cur - _hashtagResults.size() - _filterResults.size() - _peerSearchResults.size();
			}
		}
		if (base::in_range(_hashtagSelected, 0, _hashtagResults.size())) {
			const auto from = _hashtagSelected * st::mentionHeight;
			scrollToItem(from, st::mentionHeight);
		} else if (base::in_range(_filteredSelected, 0, _filterResults.size())) {
			const auto &result = _filterResults[_filteredSelected];
			const auto from = filteredOffset() + result.top;
			scrollToItem(from, result.row->height());
		} else if (base::in_range(_peerSearchSelected, 0, _peerSearchResults.size())) {
			const auto from = peerSearchOffset()
				+ _peerSearchSelected * st::dialogsRowHeight
				+ (_peerSearchSelected ? 0 : -st::searchedBarHeight);
			const auto height = st::dialogsRowHeight
				+ (_peerSearchSelected ? 0 : st::searchedBarHeight);
			scrollToItem(from, height);
		} else {
			const auto from = searchedOffset()
				+ _searchedSelected * _st->height
				+ (_searchedSelected ? 0 : -st::searchedBarHeight);
			const auto height = _st->height
				+ (_searchedSelected ? 0 : st::searchedBarHeight);
			scrollToItem(from, height);
		}
	}
	update();
}

void InnerWidget::scrollToEntry(const RowDescriptor &entry) {
	if (_state == WidgetState::Default) {
		if (auto row = _shownList->getRow(entry.key)) {
			scrollToItem(dialogsOffset() + row->top(), row->height());
		}
	} else if (_state == WidgetState::Filtered) {
		for (int32 i = 0, c = _searchResults.size(); i < c; ++i) {
			if (isSearchResultActive(_searchResults[i].get(), entry)) {
				const auto from = searchedOffset() + i * _st->height;
				scrollToItem(from, _st->height);
				return;
			}
		}
		for (auto i = 0, c = int(_filterResults.size()); i != c; ++i) {
			auto &result = _filterResults[i];
			if (result.key() == entry.key) {
				const auto from = filteredOffset() + result.top;
				scrollToItem(from, result.row->height());
				return;
			}
		}
	}
}

void InnerWidget::selectSkipPage(int32 pixels, int32 direction) {
	clearMouseSelection();
	int toSkip = pixels / _st->height;
	if (_state != WidgetState::Default) {
		selectSkip(direction * toSkip);
		return;
	}
	const auto skip = _skipTopDialog ? 1 : 0;
	if (!_selected) {
		if (direction > 0 && _shownList->size() > skip) {
			_selected = (_shownList->cbegin() + skip)->get();
			_collapsedSelected = -1;
		} else {
			return;
		}
	}
	if (direction > 0) {
		for (auto i = _shownList->cfind(_selected), end = _shownList->cend()
			; i != end && (toSkip--)
			; ++i) {
			_selected = *i;
		}
	} else {
		for (auto i = _shownList->cfind(_selected), b = _shownList->cbegin()
			; i != b && (*i)->index() > skip && (toSkip--)
			;) {
			_selected = *(--i);
		}
		if (toSkip && !_collapsedRows.empty()) {
			_collapsedSelected = std::max(int(_collapsedRows.size()) - toSkip, 0);
			_selected = nullptr;
		}
	}
	scrollToDefaultSelected();
	update();
}

void InnerWidget::scrollToItem(int top, int height) {
	_mustScrollTo.fire({ top, top + height });
}

void InnerWidget::scrollToDefaultSelected() {
	Expects(_state == WidgetState::Default);

	if (_collapsedSelected >= 0) {
		const auto from = _collapsedSelected * st::dialogsImportantBarHeight;
		scrollToItem(from, st::dialogsImportantBarHeight);
	} else if (_selected) {
		const auto from = dialogsOffset() + _selected->top();
		scrollToItem(from, _selected->height());
	}
}

void InnerWidget::preloadRowsData() {
	if (!parentWidget()) {
		return;
	}

	auto yFrom = _visibleTop;
	auto yTo = _visibleTop + (_visibleBottom - _visibleTop) * (PreloadHeightsCount + 1);
	if (_state == WidgetState::Default) {
		auto otherStart = _shownList->size() * _st->height;
		if (yFrom < otherStart) {
			for (auto i = _shownList->findByY(yFrom), end = _shownList->cend()
				; i != end
				; ++i) {
				if (((*i)->index() * _st->height) >= yTo) {
					break;
				}
				(*i)->entry()->chatListPreloadData();
			}
			yFrom = 0;
		} else {
			yFrom -= otherStart;
		}
		yTo -= otherStart;
	} else if (_state == WidgetState::Filtered) {
		int32 from = (yFrom - filteredOffset()) / _st->height;
		if (from < 0) from = 0;
		if (from < _filterResults.size()) {
			int32 to = (yTo / _st->height) + 1;
			if (to > _filterResults.size()) to = _filterResults.size();

			for (; from < to; ++from) {
				_filterResults[from].key().entry()->chatListPreloadData();
			}
		}

		from = (yFrom > filteredOffset() + st::searchedBarHeight ? ((yFrom - filteredOffset() - st::searchedBarHeight) / st::dialogsRowHeight) : 0) - _filterResults.size();
		if (from < 0) from = 0;
		if (from < _peerSearchResults.size()) {
			int32 to = (yTo > filteredOffset() + st::searchedBarHeight ? ((yTo - filteredOffset() - st::searchedBarHeight) / st::dialogsRowHeight) : 0) - _filterResults.size() + 1;
			if (to > _peerSearchResults.size()) to = _peerSearchResults.size();

			for (; from < to; ++from) {
				_peerSearchResults[from]->peer->loadUserpic();
			}
		}
		from = (yFrom > filteredOffset() + ((_peerSearchResults.empty() ? 0 : st::searchedBarHeight) + st::searchedBarHeight) ? ((yFrom - filteredOffset() - (_peerSearchResults.empty() ? 0 : st::searchedBarHeight) - st::searchedBarHeight) / st::dialogsRowHeight) : 0) - _filterResults.size() - _peerSearchResults.size();
		if (from < 0) from = 0;
		if (from < _searchResults.size()) {
			int32 to = (yTo > filteredOffset() + (_peerSearchResults.empty() ? 0 : st::searchedBarHeight) + st::searchedBarHeight ? ((yTo - filteredOffset() - (_peerSearchResults.empty() ? 0 : st::searchedBarHeight) - st::searchedBarHeight) / st::dialogsRowHeight) : 0) - _filterResults.size() - _peerSearchResults.size() + 1;
			if (to > _searchResults.size()) to = _searchResults.size();

			for (; from < to; ++from) {
				_searchResults[from]->item()->history()->peer->loadUserpic();
			}
		}
	}
}

bool InnerWidget::chooseCollapsedRow() {
	if (_state != WidgetState::Default) {
		return false;
	} else if ((_collapsedSelected < 0)
		|| (_collapsedSelected >= _collapsedRows.size())) {
		return false;
	}
	const auto &row = _collapsedRows[_collapsedSelected];
	Assert(row->folder != nullptr);
	_controller->openFolder(row->folder);
	return true;
}

void InnerWidget::switchToFilter(FilterId filterId) {
	const auto &list = session().data().chatsFilters().list();
	const auto filterIt = filterId
		? ranges::find(list, filterId, &Data::ChatFilter::id)
		: end(list);
	const auto found = (filterIt != end(list));
	if (!found) {
		filterId = 0;
	}
	if (_filterId == filterId) {
		jumpToTop();
		return;
	}
	saveChatsFilterScrollState(_filterId);
	if (_openedFolder) {
		_filterId = filterId;
		refreshShownList();
	} else {
		clearSelection();
		stopReorderPinned();
		_filterId = filterId;
		refreshShownList();
		refreshWithCollapsedRows(true);
	}
	refreshEmptyLabel();
	{
		const auto skip = found
			// Don't save a scroll state for very flexible chat filters.
			&& (filterIt->flags() & (Data::ChatFilter::Flag::NoRead));
		if (!skip) {
			restoreChatsFilterScrollState(filterId);
		}
	}
}

void InnerWidget::jumpToTop() {
	_mustScrollTo.fire({ 0, -1 });
}

void InnerWidget::saveChatsFilterScrollState(FilterId filterId) {
	_chatsFilterScrollStates[filterId] = -y();
}

void InnerWidget::restoreChatsFilterScrollState(FilterId filterId) {
	const auto it = _chatsFilterScrollStates.find(filterId);
	if (it != end(_chatsFilterScrollStates)) {
		_mustScrollTo.fire({ std::max(it->second, 0), -1 });
	}
}

bool InnerWidget::chooseHashtag() {
	if (_state != WidgetState::Filtered) {
		return false;
	} else if ((_hashtagSelected < 0)
		|| (_hashtagSelected >= _hashtagResults.size())) {
		return false;
	}
	const auto &hashtag = _hashtagResults[_hashtagSelected];
	if (_hashtagDeleteSelected) {
		auto recent = cRecentSearchHashtags();
		for (auto i = recent.begin(); i != recent.cend();) {
			if (i->first == hashtag->tag) {
				i = recent.erase(i);
			} else {
				++i;
			}
		}
		cSetRecentSearchHashtags(recent);
		session().local().writeRecentHashtagsAndBots();
		_refreshHashtagsRequests.fire({});
		selectByMouse(QCursor::pos());
	} else {
		session().local().saveRecentSearchHashtags('#' + hashtag->tag);
		_completeHashtagRequests.fire_copy(hashtag->tag);
	}
	return true;
}

ChosenRow InnerWidget::computeChosenRow() const {
	if (_state == WidgetState::Default) {
		if (_selected) {
			return {
				.key = _selected->key(),
				.message = Data::UnreadMessagePosition,
			};
		}
	} else if (_state == WidgetState::Filtered) {
		if (base::in_range(_filteredSelected, 0, _filterResults.size())) {
			return {
				.key = _filterResults[_filteredSelected].key(),
				.message = Data::UnreadMessagePosition,
				.filteredRow = true,
			};
		} else if (base::in_range(_peerSearchSelected, 0, _peerSearchResults.size())) {
			const auto peer = _peerSearchResults[_peerSearchSelected]->peer;
			return {
				.key = session().data().history(peer),
				.message = Data::UnreadMessagePosition
			};
		} else if (base::in_range(_searchedSelected, 0, _searchResults.size())) {
			const auto result = _searchResults[_searchedSelected].get();
			const auto topic = result->topic();
			const auto item = result->item();
			return {
				.key = (topic ? (Entry*)topic : (Entry*)item->history()),
				.message = item->position()
			};
		}
	}
	return ChosenRow();
}

bool InnerWidget::chooseRow(
		Qt::KeyboardModifiers modifiers,
		MsgId pressedTopicRootId) {
	if (chooseCollapsedRow()) {
		return true;
	} else if (chooseHashtag()) {
		return true;
	}
	const auto modifyChosenRow = [&](
			ChosenRow row,
			Qt::KeyboardModifiers modifiers) {
		row.newWindow = (modifiers & Qt::ControlModifier);
		row.userpicClick = (_lastRowLocalMouseX >= 0)
			&& (_lastRowLocalMouseX < _st->nameLeft)
			&& (width() > _narrowWidth);
		return row;
	};
	auto chosen = modifyChosenRow(computeChosenRow(), modifiers);
	if (chosen.key) {
		if (IsServerMsgId(chosen.message.fullId.msg)) {
			session().local().saveRecentSearchHashtags(_filter);
		}
		if (!chosen.message.fullId) {
			if (const auto history = chosen.key.history()) {
				if (const auto forum = history->peer->forum()) {
					if (pressedTopicRootId) {
						chosen.message.fullId = {
							history->peer->id,
							pressedTopicRootId,
						};
					}
				}
			}
		}
		_chosenRow.fire_copy(chosen);
		return true;
	}
	return false;
}

RowDescriptor InnerWidget::chatListEntryBefore(
		const RowDescriptor &which) const {
	if (!which.key) {
		return RowDescriptor();
	}
	if (_state == WidgetState::Default) {
		if (const auto row = _shownList->getRow(which.key)) {
			const auto i = _shownList->cfind(row);
			if (i != _shownList->cbegin()) {
				return RowDescriptor(
					(*(i - 1))->key(),
					FullMsgId(PeerId(), ShowAtUnreadMsgId));
			}
		}
		return RowDescriptor();
	}

	const auto whichThread = which.key.thread();
	if (!whichThread) {
		return RowDescriptor();
	}
	if (!_searchResults.empty()) {
		for (auto b = _searchResults.cbegin(), i = b + 1, e = _searchResults.cend(); i != e; ++i) {
			if (isSearchResultActive(i->get(), which)) {
				const auto j = i - 1;
				return RowDescriptor(
					(*j)->item()->history(),
					(*j)->item()->fullId());
			}
		}
		if (isSearchResultActive(_searchResults[0].get(), which)) {
			if (_peerSearchResults.empty()) {
				if (_filterResults.empty()) {
					return RowDescriptor();
				}
				return RowDescriptor(
					_filterResults.back().key(),
					FullMsgId(PeerId(), ShowAtUnreadMsgId));
			}
			return RowDescriptor(
				session().data().history(_peerSearchResults.back()->peer),
				FullMsgId(PeerId(), ShowAtUnreadMsgId));
		}
	}
	if (const auto history = whichThread->asHistory()) {
		if (!_peerSearchResults.empty()
			&& _peerSearchResults[0]->peer == history->peer) {
			if (_filterResults.empty()) {
				return RowDescriptor();
			}
			return RowDescriptor(
				_filterResults.back().key(),
				FullMsgId(PeerId(), ShowAtUnreadMsgId));
		}
		if (!_peerSearchResults.empty()) {
			for (auto b = _peerSearchResults.cbegin(), i = b + 1, e = _peerSearchResults.cend(); i != e; ++i) {
				if ((*i)->peer == history->peer) {
					return RowDescriptor(
						session().data().history((*(i - 1))->peer),
						FullMsgId(PeerId(), ShowAtUnreadMsgId));
				}
			}
		}
	}
	if (_filterResults.empty() || _filterResults[0].key() == which.key) {
		return RowDescriptor();
	}

	for (auto b = _filterResults.cbegin(), i = b + 1, e = _filterResults.cend(); i != e; ++i) {
		if (i->key() == which.key) {
			return RowDescriptor(
				(i - 1)->key(),
				FullMsgId(PeerId(), ShowAtUnreadMsgId));
		}
	}
	return RowDescriptor();
}

RowDescriptor InnerWidget::chatListEntryAfter(
		const RowDescriptor &which) const {
	if (!which.key) {
		return RowDescriptor();
	}
	if (_state == WidgetState::Default) {
		if (const auto row = _shownList->getRow(which.key)) {
			const auto i = _shownList->cfind(row) + 1;
			if (i != _shownList->cend()) {
				return RowDescriptor(
					(*i)->key(),
					FullMsgId(PeerId(), ShowAtUnreadMsgId));
			}
		}
		return RowDescriptor();
	}

	const auto whichThread = which.key.thread();
	if (!whichThread) {
		return RowDescriptor();
	}
	for (auto i = _searchResults.cbegin(), e = _searchResults.cend(); i != e; ++i) {
		if (isSearchResultActive(i->get(), which)) {
			if (++i != e) {
				return RowDescriptor(
					(*i)->item()->history(),
					(*i)->item()->fullId());
			}
			return RowDescriptor();
		}
	}
	if (const auto history = whichThread->asHistory()) {
		for (auto i = _peerSearchResults.cbegin(), e = _peerSearchResults.cend(); i != e; ++i) {
			if ((*i)->peer == history->peer) {
				++i;
				if (i != e) {
					return RowDescriptor(
						session().data().history((*i)->peer),
						FullMsgId(PeerId(), ShowAtUnreadMsgId));
				} else if (!_searchResults.empty()) {
					return RowDescriptor(
						_searchResults.front()->item()->history(),
						_searchResults.front()->item()->fullId());
				}
				return RowDescriptor();
			}
		}
	}
	for (auto i = _filterResults.cbegin(), e = _filterResults.cend(); i != e; ++i) {
		if ((*i).key() == which.key) {
			++i;
			if (i != e) {
				return RowDescriptor(
					(*i).key(),
					FullMsgId(PeerId(), ShowAtUnreadMsgId));
			} else if (!_peerSearchResults.empty()) {
				return RowDescriptor(
					session().data().history(_peerSearchResults.front()->peer),
					FullMsgId(PeerId(), ShowAtUnreadMsgId));
			} else if (!_searchResults.empty()) {
				return RowDescriptor(
					_searchResults.front()->item()->history(),
					_searchResults.front()->item()->fullId());
			}
			return RowDescriptor();
		}
	}
	return RowDescriptor();
}

RowDescriptor InnerWidget::chatListEntryFirst() const {
	if (_state == WidgetState::Default) {
		const auto i = _shownList->cbegin();
		if (i != _shownList->cend()) {
			return RowDescriptor(
				(*i)->key(),
				FullMsgId(PeerId(), ShowAtUnreadMsgId));
		}
		return RowDescriptor();
	} else if (!_filterResults.empty()) {
		return RowDescriptor(
			_filterResults.front().key(),
			FullMsgId(PeerId(), ShowAtUnreadMsgId));
	} else if (!_peerSearchResults.empty()) {
		return RowDescriptor(
			session().data().history(_peerSearchResults.front()->peer),
			FullMsgId(PeerId(), ShowAtUnreadMsgId));
	} else if (!_searchResults.empty()) {
		return RowDescriptor(
			_searchResults.front()->item()->history(),
			_searchResults.front()->item()->fullId());
	}
	return RowDescriptor();
}

RowDescriptor InnerWidget::chatListEntryLast() const {
	if (_state == WidgetState::Default) {
		const auto i = _shownList->cend();
		if (i != _shownList->cbegin()) {
			return RowDescriptor(
				(*(i - 1))->key(),
				FullMsgId(PeerId(), ShowAtUnreadMsgId));
		}
		return RowDescriptor();
	} else if (!_searchResults.empty()) {
		return RowDescriptor(
			_searchResults.back()->item()->history(),
			_searchResults.back()->item()->fullId());
	} else if (!_peerSearchResults.empty()) {
		return RowDescriptor(
			session().data().history(_peerSearchResults.back()->peer),
			FullMsgId(PeerId(), ShowAtUnreadMsgId));
	} else if (!_filterResults.empty()) {
		return RowDescriptor(
			_filterResults.back().key(),
			FullMsgId(PeerId(), ShowAtUnreadMsgId));
	}
	return RowDescriptor();
}

void InnerWidget::setupOnlineStatusCheck() {
	session().changes().peerUpdates(
		Data::PeerUpdate::Flag::OnlineStatus
		| Data::PeerUpdate::Flag::GroupCall
		| Data::PeerUpdate::Flag::MessagesTTL
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		const auto &peer = update.peer;
		if (const auto user = peer->asUser()) {
			if (user->isSelf()) {
				return;
			}
			if (const auto history = session().data().historyLoaded(user)) {
				updateRowCornerStatusShown(history);
			}
		} else if (const auto group = peer->asMegagroup()) {
			if (const auto history = session().data().historyLoaded(group)) {
				updateRowCornerStatusShown(history);
			}
		} else if (peer->messagesTTL()) {
			if (const auto history = session().data().historyLoaded(peer)) {
				updateRowCornerStatusShown(history);
			}
		}
	}, lifetime());
}

void InnerWidget::repaintDialogRowCornerStatus(not_null<History*> history) {
	const auto user = history->peer->isUser();
	const auto size = user
		? st::dialogsOnlineBadgeSize
		: st::dialogsCallBadgeSize;
	const auto stroke = st::dialogsOnlineBadgeStroke;
	const auto skip = user
		? st::dialogsOnlineBadgeSkip
		: st::dialogsCallBadgeSkip;
	const auto updateRect = QRect(
		_st->photoSize - skip.x() - size,
		_st->photoSize - skip.y() - size,
		size,
		size
	).marginsAdded(
		{ stroke, stroke, stroke, stroke }
	).translated(
		st::defaultDialogRow.padding.left(),
		st::defaultDialogRow.padding.top()
	);
	const auto ttlUpdateRect = !history->peer->messagesTTL()
		? QRect()
		: Dialogs::CornerBadgeTTLRect(
			_st->photoSize
		).translated(
			st::defaultDialogRow.padding.left(),
			st::defaultDialogRow.padding.top()
		);
	updateDialogRow(
		RowDescriptor(
			history,
			FullMsgId()),
		updateRect.united(ttlUpdateRect),
		UpdateRowSection::Default | UpdateRowSection::Filtered);
}

void InnerWidget::updateRowCornerStatusShown(not_null<History*> history) {
	const auto repaint = [=] {
		repaintDialogRowCornerStatus(history);
	};
	repaint();

	const auto findRow = [&](not_null<History*> history)
		-> std::pair<Row*, int> {
		if (state() == WidgetState::Default) {
			const auto row = _shownList->getRow({ history });
			return { row, row ? defaultRowTop(row) : 0 };
		}
		const auto i = ranges::find(
			_filterResults,
			Key(history),
			&FilterResult::key);
		const auto index = (i - begin(_filterResults));
		const auto row = (i == end(_filterResults)) ? nullptr : i->row.get();
		return { row, filteredOffset() + index * _st->height };
	};
	if (const auto &[row, top] = findRow(history); row != nullptr) {
		const auto visible = (top < _visibleBottom)
			&& (top + _st->height > _visibleTop);
		row->updateCornerBadgeShown(
			history->peer,
			visible ? Fn<void()>(crl::guard(this, repaint)) : nullptr);
	}
}

RowDescriptor InnerWidget::resolveChatNext(RowDescriptor from) const {
	const auto row = from.key ? from : _controller->activeChatEntryCurrent();
	return row.key
		? computeJump(
			chatListEntryAfter(row),
			JumpSkip::NextOrEnd)
		: row;
}

RowDescriptor InnerWidget::resolveChatPrevious(RowDescriptor from) const {
	const auto row = from.key ? from : _controller->activeChatEntryCurrent();
	return row.key
		? computeJump(
			chatListEntryBefore(row),
			JumpSkip::PreviousOrBegin)
		: row;
}

void InnerWidget::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return isActiveWindow()
			&& !_controller->isLayerShown()
			&& !_controller->window().locked()
			&& !_childListShown.current().shown;
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;

		const auto row = _controller->activeChatEntryCurrent();
		// Those should be computed before the call to request->handle.
		const auto previous = row.key
			? computeJump(
				chatListEntryBefore(row),
				JumpSkip::PreviousOrBegin)
			: row;
		const auto next = row.key
			? computeJump(
				chatListEntryAfter(row),
				JumpSkip::NextOrEnd)
			: row;
		const auto first = [&] {
			const auto to = chatListEntryFirst();
			const auto jump = computeJump(to, JumpSkip::NextOrOriginal);
			return (to == row || jump == row || to == previous) ? to : jump;
		}();
		const auto last = [&] {
			const auto to = chatListEntryLast();
			const auto jump = computeJump(to, JumpSkip::PreviousOrOriginal);
			return (to == row || jump == row || to == next) ? to : jump;
		}();
		if (row.key) {
			request->check(Command::ChatPrevious) && request->handle([=] {
				return jumpToDialogRow(previous);
			});
			request->check(Command::ChatNext) && request->handle([=] {
				return jumpToDialogRow(next);
			});
		}
		request->check(Command::ChatFirst) && request->handle([=] {
			return jumpToDialogRow(first);
		});
		request->check(Command::ChatLast) && request->handle([=] {
			return jumpToDialogRow(last);
		});
		request->check(Command::ChatSelf) && request->handle([=] {
			_controller->showThread(
				session().data().history(session().user()),
				ShowAtUnreadMsgId,
				Window::SectionShow::Way::ClearStack);
			return true;
		});
		request->check(Command::ShowArchive) && request->handle([=] {
			const auto folder = session().data().folderLoaded(
				Data::Folder::kId);
			if (folder && !folder->chatsList()->empty()) {
				const auto controller = _controller;
				controller->openFolder(folder);

				// Calling openFolder() could've destroyed this widget.
				controller->window().hideSettingsAndLayer();
				return true;
			}
			return false;
		});

		if (session().data().chatsFilters().has()) {
			const auto filters = &session().data().chatsFilters();
			const auto filtersCount = int(filters->list().size());
			auto &&folders = ranges::views::zip(
				Shortcuts::kShowFolder,
				ranges::views::ints(0, ranges::unreachable));
			for (const auto [command, index] : folders) {
				const auto select = (command == Command::ShowFolderLast)
					? (filtersCount - 1)
					: std::clamp(index, 0, filtersCount - 1);
				request->check(command) && request->handle([=] {
					if (select <= filtersCount) {
						_controller->setActiveChatsFilter(
							filters->lookupId(select));
					}
					return true;
				});
			}
		}

		static const auto kPinned = {
			Command::ChatPinned1,
			Command::ChatPinned2,
			Command::ChatPinned3,
			Command::ChatPinned4,
			Command::ChatPinned5,
			Command::ChatPinned6,
			Command::ChatPinned7,
			Command::ChatPinned8,
		};
		auto &&pinned = ranges::views::zip(
			kPinned,
			ranges::views::ints(0, ranges::unreachable));
		for (const auto [command, index] : pinned) {
			request->check(command) && request->handle([=, index = index] {
				const auto list = (_filterId
					? session().data().chatsFilters().chatsList(_filterId)
					: session().data().chatsList()
				)->indexed();
				const auto count = Dialogs::PinnedDialogsCount(
					_filterId,
					list);
				if (index >= count) {
					return false;
				}
				const auto skip = Dialogs::FixedOnTopDialogsCount(list);
				const auto row = *(list->cbegin() + skip + index);
				return jumpToDialogRow({ row->key(), FullMsgId() });
			});
		}

		const auto nearFolder = [=](bool isNext) {
			const auto id = _controller->activeChatsFilterCurrent();
			const auto list = &session().data().chatsFilters().list();
			const auto index = int(ranges::find(
				*list,
				id,
				&Data::ChatFilter::id
			) - begin(*list));
			if (index == list->size() && id != 0) {
				return false;
			}
			const auto changed = index + (isNext ? 1 : -1);
			if (changed >= int(list->size()) || changed < 0) {
				return false;
			}
			_controller->setActiveChatsFilter((changed >= 0)
				? (*list)[changed].id()
				: 0);
			return true;
		};

		request->check(Command::FolderNext) && request->handle([=] {
			return nearFolder(true);
		});

		request->check(Command::FolderPrevious) && request->handle([=] {
			return nearFolder(false);
		});

		request->check(Command::ReadChat) && request->handle([=] {
			const auto thread = _selected ? _selected->thread() : nullptr;
			if (!thread) {
				return false;
			}
			if (Window::IsUnreadThread(thread)) {
				Window::MarkAsReadThread(thread);
			}
			return true;
		});

		request->check(Command::ShowContacts) && request->handle([=] {
			_controller->show(PrepareContactsBox(_controller));
			return true;
		});

		if (session().supportMode() && row.key.history()) {
			request->check(
				Command::SupportScrollToCurrent
			) && request->handle([=] {
				scrollToEntry(row);
				return true;
			});
		}
	}, lifetime());
}

RowDescriptor InnerWidget::computeJump(
		const RowDescriptor &to,
		JumpSkip skip) const {
	auto result = to;
	if (result.key) {
		const auto down = (skip == JumpSkip::NextOrEnd)
			|| (skip == JumpSkip::NextOrOriginal);
		const auto needSkip = [&] {
			return (result.key.folder() != nullptr)
				|| (session().supportMode()
					&& !result.key.entry()->chatListBadgesState().unread);
		};
		while (needSkip()) {
			const auto next = down
				? chatListEntryAfter(result)
				: chatListEntryBefore(result);
			if (next.key) {
				result = next;
			} else {
				if (skip == JumpSkip::PreviousOrOriginal
					|| skip == JumpSkip::NextOrOriginal) {
					result = to;
				}
				break;
			}
		}
	}
	return result;
}

bool InnerWidget::jumpToDialogRow(RowDescriptor to) {
	if (to == chatListEntryLast()) {
		_listBottomReached.fire({});
	}
	if (uniqueSearchResults()) {
		to.fullId = FullMsgId();
	}
	return _controller->jumpToChatListEntry(to);
}

} // namespace Dialogs
