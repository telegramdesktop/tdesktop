/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_inner_widget.h"

#include "dialogs/dialogs_three_state_icon.h"
#include "dialogs/ui/chat_search_empty.h"
#include "dialogs/ui/chat_search_in.h"
#include "dialogs/ui/dialogs_layout.h"
#include "dialogs/ui/dialogs_video_userpic.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_widget.h"
#include "dialogs/dialogs_search_from_controllers.h"
#include "dialogs/dialogs_search_tags.h"
#include "history/view/history_view_context_menu.h"
#include "history/history.h"
#include "history/history_item.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/shortcuts.h"
#include "core/ui_integration.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_options.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/painter.h"
#include "ui/rect.h"
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
#include "data/data_changes.h"
#include "data/data_message_reactions.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
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
#include "ui/chat/chats_filter_tag.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/loading_element.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/unread_badge.h"
#include "boxes/filters/edit_filter_box.h"
#include "boxes/peers/edit_forum_topic_box.h"
#include "api/api_chat_filters.h"
#include "base/qt/qt_common_adapters.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat.h" // popupMenuExpandedSeparator
#include "styles/style_chat_helpers.h"
#include "styles/style_color_indices.h"
#include "styles/style_window.h"
#include "styles/style_media_player.h"
#include "styles/style_menu_icons.h"

#include <QtWidgets/QApplication>

namespace Dialogs {
namespace {

constexpr auto kHashtagResultsLimit = 5;
constexpr auto kStartReorderThreshold = 30;
constexpr auto kQueryPreviewLimit = 32;
constexpr auto kPreviewPostsLimit = 3;

[[nodiscard]] InnerWidget::ChatsFilterTagsKey SerializeFilterTagsKey(
		FilterId filterId,
		uint8 more,
		bool active) {
	return (filterId & 0xFFFFFFFF)
		| (static_cast<int64_t>(more) << 32)
		| (static_cast<int64_t>(active) << 40);
}

[[nodiscard]] int FixedOnTopDialogsCount(not_null<Dialogs::IndexedList*> list) {
	auto result = 0;
	for (const auto &row : *list) {
		if (!row->entry()->fixedOnTopIndex()) {
			break;
		}
		++result;
	}
	return result;
}

[[nodiscard]] int PinnedDialogsCount(
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

[[nodiscard]] UserData *MaybeBotWithApp(Row *row) {
	if (row) {
		if (const auto history = row->key().history()) {
			if (const auto user = history->peer->asUser()) {
				if (user->botInfo && user->botInfo->hasMainApp) {
					if (!history->unreadCount() && !history->unreadMark()) {
						return user;
					}
				}
			}
		}
	}
	return nullptr;
}

[[nodiscard]] object_ptr<SearchEmpty> MakeSearchEmpty(
		QWidget *parent,
		SearchState state,
		Fn<void()> resetChatTypeFilter) {
	const auto query = state.query.trimmed();
	const auto hashtag = !query.isEmpty() && (query[0] == '#');
	const auto trimmed = hashtag ? query.mid(1).trimmed() : query;
	const auto fromPeer = (state.tab == ChatSearchTab::MyMessages
		|| state.tab == ChatSearchTab::PublicPosts
		|| !state.inChat.peer()
		|| !(state.inChat.peer()->isChat()
			|| state.inChat.peer()->isMegagroup()))
		? nullptr
		: state.fromPeer;
	const auto waiting = trimmed.isEmpty()
		&& state.tags.empty()
		&& !fromPeer;
	const auto suggestAllChats = !waiting
		&& state.tab == ChatSearchTab::MyMessages
		&& state.filter != ChatTypeFilter::All;
	const auto icon = waiting
		? SearchEmptyIcon::Search
		: SearchEmptyIcon::NoResults;
	auto text = TextWithEntities();
	if (waiting) {
		if (hashtag) {
			text.append(tr::lng_search_tab_by_hashtag(tr::now));
		} else {
			text.append(tr::lng_dlg_search_for_messages(tr::now));
		}
	} else {
		text.append(tr::lng_search_tab_no_results(
			tr::now,
			Ui::Text::Bold));
		if (!trimmed.isEmpty()) {
			const auto preview = (trimmed.size() > kQueryPreviewLimit + 3)
				? (trimmed.mid(0, kQueryPreviewLimit) + Ui::kQEllipsis)
				: trimmed;
			text.append("\n").append(
				tr::lng_search_tab_no_results_text(
					tr::now,
					lt_query,
					trimmed.mid(0, kQueryPreviewLimit)));
			if (suggestAllChats) {
				text.append("\n\n").append(
					Ui::Text::Link(tr::lng_search_tab_try_in_all(tr::now)));
			} else if (hashtag) {
				text.append("\n").append(
					tr::lng_search_tab_no_results_retry(tr::now));
			}
		}
	}
	auto result = object_ptr<SearchEmpty>(
		parent,
		icon,
		rpl::single(std::move(text)));
	if (suggestAllChats) {
		result->handlerActivated(
		) | rpl::start_with_next(resetChatTypeFilter, result->lifetime());
	}
	result->show();
	result->resizeToWidth(parent->width());
	return result;
}

[[nodiscard]] QString ChatTypeFilterLabel(ChatTypeFilter filter) {
	switch (filter) {
	case ChatTypeFilter::All:
		return tr::lng_search_filter_all(tr::now);
	case ChatTypeFilter::Private:
		return tr::lng_search_filter_private(tr::now);
	case ChatTypeFilter::Groups:
		return tr::lng_search_filter_group(tr::now);
	case ChatTypeFilter::Channels:
		return tr::lng_search_filter_channel(tr::now);
	}
	Unexpected("Chat type filter in search results.");
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

struct InnerWidget::TagCache {
	Ui::ChatsFilterTagContext context;
	QImage frame;
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
, _childListShown(std::move(childListShown)) {
	setAttribute(Qt::WA_OpaquePaintEvent, true);

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_topicJumpCache = nullptr;
		_chatsFilterTags.clear();
		_rightButtons.clear();
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
		refreshEmpty();
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
		return !_savedSublists
			&& !_openedForum
			&& (folder == _openedFolder);
	}) | rpl::start_with_next([=] {
		refresh();
	}, lifetime());

	rpl::merge(
		session().settings().archiveCollapsedChanges() | rpl::map_to(false),
		session().data().chatsFilters().changed() | rpl::map_to(true)
	) | rpl::start_with_next([=](bool refreshHeight) {
		if (refreshHeight) {
			_chatsFilterTags.clear();
		}
		if (refreshHeight && _filterId) {
			// Height of the main list will be refreshed in other way.
			_shownList->updateHeights(_narrowRatio);
		}
		refreshWithCollapsedRows();
	}, lifetime());

	session().data().chatsFilters().tagsEnabledValue(
	) | rpl::distinct_until_changed() | rpl::start_with_next([=](bool tags) {
		_handleChatListEntryTagRefreshesLifetime.destroy();
		if (_shownList->updateHeights(_narrowRatio)) {
			refresh();
		}
		if (!tags) {
			return;
		}
		using Event = Data::Session::ChatListEntryRefresh;
		session().data().chatListEntryRefreshes(
		) | rpl::filter([=](const Event &event) {
			if (_waitingAllChatListEntryRefreshesForTags) {
				return false;
			}
			if (event.existenceChanged) {
				if (event.key.entry()->inChatList(_filterId)) {
					_waitingAllChatListEntryRefreshesForTags = true;
					return true;
				}
			}
			return false;
		}) | rpl::start_with_next([=](const Event &event) {
			Ui::PostponeCall(crl::guard(this, [=] {
				_waitingAllChatListEntryRefreshesForTags = false;
				if (_shownList->updateHeights(_narrowRatio)) {
					refresh();
				}
			}));
		}, _handleChatListEntryTagRefreshesLifetime);

		session().data().chatsFilters().tagColorChanged(
		) | rpl::start_with_next([=](Data::TagColorChanged data) {
			const auto filterId = data.filterId;
			const auto key = SerializeFilterTagsKey(filterId, 0, false);
			const auto activeKey = SerializeFilterTagsKey(filterId, 0, true);
			{
				auto &tags = _chatsFilterTags;
				if (const auto it = tags.find(key); it != tags.end()) {
					tags.erase(it);
				}
				if (const auto it = tags.find(activeKey); it != tags.end()) {
					tags.erase(it);
				}
			}
			if (data.colorExistenceChanged) {
				auto &filters = session().data().chatsFilters();
				for (const auto &filter : filters.list()) {
					if (filter.id() != filterId) {
						continue;
					}
					const auto c = filter.colorIndex();
					const auto list = filters.chatsList(filterId);
					for (const auto &row : list->indexed()->all()) {
						row->entry()->setColorIndexForFilterId(filterId, c);
					}
				}
				if (_shownList->updateHeights(_narrowRatio)) {
					refresh();
				}
			} else {
				update();
			}
		}, _handleChatListEntryTagRefreshesLifetime);
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

	_controller->window().widget()->globalForceClicks(
	) | rpl::start_with_next([=](QPoint globalPosition) {
		processGlobalForceClick(globalPosition);
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
			result.row->recountHeight(_narrowRatio, _filterId);
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

int InnerWidget::hashtagsOffset() const {
	return searchInChatOffset() + searchInChatSkip();
}

int InnerWidget::filteredOffset() const {
	return hashtagsOffset() + (_hashtagResults.size() * st::mentionHeight);
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

int InnerWidget::searchInChatOffset() const {
	return (_searchTags ? _searchTags->height() : 0);
}

int InnerWidget::searchInChatSkip() const {
	return _searchIn ? _searchIn->height() : 0;
}

int InnerWidget::previewOffset() const {
	auto result = peerSearchOffset();
	if (!_peerSearchResults.empty()) {
		result += (_peerSearchResults.size() * st::dialogsRowHeight)
			+ st::searchedBarHeight;
	}
	return result;
}

int InnerWidget::searchedOffset() const {
	auto result = previewOffset();
	if (!_previewResults.empty()) {
		result += (_previewResults.size() * st::dialogsRowHeight)
			+ st::searchedBarHeight;
	}
	return result;
}

void InnerWidget::changeOpenedFolder(Data::Folder *folder) {
	Expects(!folder || !_savedSublists);

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
	Expects(!forum || !_savedSublists);

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

void InnerWidget::showSavedSublists() {
	Expects(!_geometryInited);
	Expects(!_savedSublists);

	_savedSublists = true;

	stopReorderPinned();
	clearSelection();

	_filterId = 0;
	_openedForum = nullptr;
	_st = &st::defaultDialogRow;
	refreshShownList();

	_openedForumLifetime.destroy();

	//session().data().savedMessages().chatsListChanges(
	//) | rpl::start_with_next([=] {
	//	refresh();
	//}, lifetime());

	refreshWithCollapsedRows(true);
	if (_loadMoreCallback) {
		_loadMoreCallback();
	}
}

void InnerWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setInactive(
		_controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any));
	if (!_savedSublists && _controller->contentOverlapped(this, e)) {
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
		const auto &key = row->key();
		const auto active = mayBeActive && isRowActive(row, activeEntry);
		const auto forum = key.history() && key.history()->isForum();
		if (forum && !_topicJumpCache) {
			_topicJumpCache = std::make_unique<Ui::TopicJumpCache>();
		}
		const auto expanding = forum
			&& (key.history()->peer->id == childListShown.peerId);
		context.rightButton = maybeCacheRightButton(row);

		context.st = (forum ? &st::forumDialogRow : _st.get());

		auto chatsFilterTags = std::vector<QImage*>();
		if (context.narrow) {
			context.chatsFilterTags = nullptr;
		} else if (row->entry()->hasChatsFilterTags(context.filter)) {
			const auto a = active;
			context.st = forum
				? &st::taggedForumDialogRow
				: &st::taggedDialogRow;
			auto availableWidth = context.width
				- context.st->padding.right()
				- st::dialogsUnreadPadding
				- context.st->nameLeft;
			auto more = uint8(0);
			const auto &list = session().data().chatsFilters().list();
			for (const auto &filter : list) {
				if (!row->entry()->inChatList(filter.id())
					|| (filter.id() == context.filter)) {
					continue;
				}
				if (active
					&& (filter.flags() & Data::ChatFilter::Flag::NoRead)
					&& !filter.contains(key.history(), true)) {
					// Hack for History::fakeUnreadWhileOpened().
					continue;
				}
				if (const auto tag = cacheChatsFilterTag(filter, 0, a)) {
					if (more) {
						more++;
						continue;
					}
					const auto tagWidth = tag->width()
						/ style::DevicePixelRatio();
					if (availableWidth < tagWidth) {
						more++;
					} else {
						chatsFilterTags.push_back(tag);
						availableWidth -= tagWidth
							+ st::dialogRowFilterTagSkip;
					}
				}
			}
			if (more) {
				if (const auto tag = cacheChatsFilterTag({}, more, a)) {
					const auto tagWidth = tag->width()
						/ style::DevicePixelRatio();
					if (availableWidth < tagWidth) {
						more++;
						if (!chatsFilterTags.empty()) {
							const auto tag = cacheChatsFilterTag({}, more, a);
							if (tag) {
								chatsFilterTags.back() = tag;
							}
						}
					} else {
						chatsFilterTags.push_back(tag);
					}
				}
			}
			context.chatsFilterTags = &chatsFilterTags;
		} else {
			context.chatsFilterTags = nullptr;
		}

		context.topicsExpanded = (expanding && !active)
			? childListShown.shown
			: 0.;
		context.active = active;
		context.selected = _menuRow.key
			? (row->key() == _menuRow.key)
			: _chatPreviewRow.key
			? (row->key() == _chatPreviewRow.key)
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
		if (_searchTags) {
			paintSearchTags(p, {
				.st = &st::forumTopicRow,
				.currentBg = currentBg(),
				.now = ms,
				.width = fullWidth,
				.paused = videoPaused,
			});
			p.translate(0, _searchTags->height());
		}
		if (_searchIn) {
			p.translate(0, searchInChatSkip());
			if (_previewResults.empty() && _searchResults.empty()) {
				p.fillRect(0, 0, fullWidth, st::lineWidth, st::shadowFg);
			}
		}
		if (!_hashtagResults.empty()) {
			const auto skip = hashtagsOffset();
			auto from = floorclamp(r.y() - skip, st::mentionHeight, 0, _hashtagResults.size());
			auto to = ceilclamp(r.y() + r.height() - skip, st::mentionHeight, 0, _hashtagResults.size());
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
				if (to < _hashtagResults.size()) {
					p.translate(0, (_hashtagResults.size() - to) * st::mentionHeight);
				}
			}
		}
		if (!_filterResults.empty()) {
			auto skip = filteredOffset();
			auto from = filteredIndex(r.y() - skip);
			auto to = std::min(
				filteredIndex(r.y() + r.height() - skip) + 1,
				int(_filterResults.size()));
			const auto height = filteredHeight(from);
			p.translate(0, height);
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
				if (to < _peerSearchResults.size()) {
					p.translate(0, (_peerSearchResults.size() - to) * st::dialogsRowHeight);
				}
			}
		}

		const auto showUnreadInSearchResults = uniqueSearchResults();
		if (_previewResults.empty() && _searchResults.empty()) {
			if (_loadingAnimation) {
				const auto text = tr::lng_contacts_loading(tr::now);
				p.fillRect(0, 0, fullWidth, st::searchedBarHeight, st::searchedBarBg);
				p.setFont(st::searchedBarFont);
				p.setPen(st::searchedBarFg);
				p.drawTextLeft(st::searchedBarPosition.x(), st::searchedBarPosition.y(), width(), text);
				p.translate(0, st::searchedBarHeight);
			}
			return;
		}
		if (!_previewResults.empty()) {
			const auto text = tr::lng_search_tab_public_posts(tr::now);
			p.fillRect(0, 0, fullWidth, st::searchedBarHeight, st::searchedBarBg);
			p.setFont(st::searchedBarFont);
			p.setPen(st::searchedBarFg);
			p.drawTextLeft(st::searchedBarPosition.x(), st::searchedBarPosition.y(), width(), text);
			const auto moreFont = (_selectedMorePosts || _pressedMorePosts)
				? st::searchedBarFont->underline()
				: st::searchedBarFont;
			{
				const auto text = tr::lng_channels_your_more(tr::now);
				if (!_morePostsWidth) {
					_morePostsWidth = moreFont->width(text);
				}
				p.setFont(moreFont);
				p.drawTextLeft(
					width() - st::searchedBarPosition.x() - _morePostsWidth,
					st::searchedBarPosition.y(),
					width(),
					text);
				p.translate(0, st::searchedBarHeight);
			}
			auto skip = previewOffset();
			auto from = floorclamp(r.y() - skip, _st->height, 0, _previewResults.size());
			auto to = ceilclamp(r.y() + r.height() - skip, _st->height, 0, _previewResults.size());
			p.translate(0, from * _st->height);
			if (from < _previewResults.size()) {
				for (; from < to; ++from) {
					const auto &result = _previewResults[from];
					const auto active = isSearchResultActive(result.get(), activeEntry);
					const auto selected = _menuRow.key
						? isSearchResultActive(result.get(), _menuRow)
						: _chatPreviewRow.key
						? isSearchResultActive(result.get(), _chatPreviewRow)
						: (from == (isPressed()
							? _previewPressed
							: _previewSelected));
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
			if (to < _previewResults.size()) {
				p.translate(0, (_previewResults.size() - to) * _st->height);
			}
		}

		if (!_searchResults.empty()) {
			const auto text = showUnreadInSearchResults
				? u"Search results"_q
				: tr::lng_search_found_results(
					tr::now,
					lt_count,
					_searchedMigratedCount + _searchedCount);
			p.fillRect(0, 0, fullWidth, st::searchedBarHeight, st::searchedBarBg);
			p.setFont(st::searchedBarFont);
			p.setPen(st::searchedBarFg);
			p.drawTextLeft(st::searchedBarPosition.x(), st::searchedBarPosition.y(), width(), text);
			const auto filterOver = _selectedChatTypeFilter
				|| _pressedChatTypeFilter;
			const auto filterFont = filterOver
				? st::searchedBarFont->underline()
				: st::searchedBarFont;
			if (hasChatTypeFilter()) {
				const auto text = ChatTypeFilterLabel(_searchState.filter);
				if (!_chatTypeFilterWidth) {
					_chatTypeFilterWidth = filterFont->width(text);
				}
				p.setFont(filterFont);
				p.drawTextLeft(
					(width()
						- st::searchedBarPosition.x()
						- _chatTypeFilterWidth),
					st::searchedBarPosition.y(),
					width(),
					text);
			}
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
						: _chatPreviewRow.key
						? isSearchResultActive(result.get(), _chatPreviewRow)
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

[[nodiscard]] RightButton *InnerWidget::maybeCacheRightButton(Row *row) {
	if (const auto user = MaybeBotWithApp(row)) {
		const auto it = _rightButtons.find(user->id);
		if (it == _rightButtons.end()) {
			auto rightButton = RightButton();
			const auto text = tr::lng_profile_open_app_short(tr::now);
			rightButton.text.setText(st::dialogRowOpenBotTextStyle, text);
			const auto size = QSize(
				rightButton.text.maxWidth()
					+ rightButton.text.minHeight(),
				st::dialogRowOpenBotHeight);
			const auto generateBg = [&](const style::color &c) {
				auto bg = QImage(
					style::DevicePixelRatio() * size,
					QImage::Format_ARGB32_Premultiplied);
				bg.setDevicePixelRatio(style::DevicePixelRatio());
				bg.fill(Qt::transparent);
				{
					auto p = QPainter(&bg);
					auto hq = PainterHighQualityEnabler(p);
					p.setPen(Qt::NoPen);
					p.setBrush(c);
					const auto r = size.height() / 2;
					p.drawRoundedRect(Rect(size), r, r);
				}
				return bg;
			};
			rightButton.bg = generateBg(st::activeButtonBg);
			rightButton.selectedBg = generateBg(st::activeButtonBgOver);
			rightButton.activeBg = generateBg(st::activeButtonFg);
			return &(_rightButtons.emplace(
				user->id,
				std::move(rightButton)).first->second);
		} else {
			return &(it->second);
		}
	}
	return nullptr;
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

bool InnerWidget::isRowActive(
		not_null<Row*> row,
		const RowDescriptor &entry) const {
	const auto key = row->key();
	return (entry.key == key)
		|| (entry.key.sublist() && key.peer() && key.peer()->isSelf());
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

	if (const auto info = peer->botVerifyDetails()) {
		if (!result->badge.ready(info)) {
			result->badge.set(
				info,
				peer->owner().customEmojiManager().factory(),
				[=] { updateSearchResult(peer); });
		}
		const auto &st = Ui::VerifiedStyle(context);
		const auto position = rectForName.topLeft();
		const auto skip = result->badge.drawVerified(p, position, st);
		rectForName.setLeft(position.x() + skip + st::dialogsChatTypeSkip);
	} else if (const auto chatTypeIcon = Ui::ChatTypeIcon(peer, context)) {
		chatTypeIcon->paint(p, rectForName.topLeft(), context.width);
		rectForName.setLeft(rectForName.left()
			+ chatTypeIcon->width()
			+ st::dialogsChatTypeSkip);
	}
	const auto badgeWidth = result->badge.drawGetWidth(p, {
		.peer = peer,
		.rectForName = rectForName,
		.nameWidth = result->name.maxWidth(),
		.outerWidth = context.width,
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
		.prioritizeVerification = true,
		.paused = context.paused,
	});
	rectForName.setWidth(rectForName.width() - badgeWidth);

	QRect tr(context.st->textLeft, context.st->textTop, namewidth, st::dialogsTextFont->height);
	p.setFont(st::dialogsTextFont);
	QString username = peer->username();
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

void InnerWidget::paintSearchTags(
		Painter &p,
		const Ui::PaintContext &context) const {
	Expects(_searchTags != nullptr);

	const auto height = _searchTags->height();
	p.fillRect(0, 0, width(), height, currentBg());
	const auto top = st::dialogsSearchTagBottom / 2;
	const auto position = QPoint(_searchTagsLeft, top);
	_searchTags->paint(p, position, context.now, context.paused);
}

void InnerWidget::showPeerMenu() {
	if (!_selected) {
		return;
	}
	const auto &padding = st::defaultDialogRow.padding;
	const auto pos = QPoint(
		width() - padding.right(),
		_selected->top() + _selected->height() + padding.bottom());
	auto event = QContextMenuEvent(
		QContextMenuEvent::Keyboard,
		pos,
		mapToGlobal(pos));
	InnerWidget::contextMenuEvent(&event);
}

void InnerWidget::mouseMoveEvent(QMouseEvent *e) {
	if (_chatPreviewTouchGlobal || _touchDragStartGlobal) {
		return;
	}
	const auto globalPosition = e->globalPos();
	if (!_lastMousePosition) {
		_lastMousePosition = globalPosition;
		return;
	} else if (!_mouseSelection
		&& *_lastMousePosition == globalPosition) {
		return;
	}
	selectByMouse(globalPosition);
	if (_chatPreviewScheduled && !isUserpicPress()) {
		cancelChatPreview();
	}
}

void InnerWidget::cancelChatPreview() {
	_chatPreviewTouchGlobal = {};
	_chatPreviewScheduled = false;
	if (_chatPreviewRow.key) {
		updateDialogRow(base::take(_chatPreviewRow));
	}
	_controller->cancelScheduledPreview();
}

void InnerWidget::clearIrrelevantState() {
	if (_state == WidgetState::Default) {
		_hashtagSelected = -1;
		setHashtagPressed(-1);
		_hashtagDeleteSelected = _hashtagDeletePressed = false;
		_filteredSelected = -1;
		setFilteredPressed(-1, false, false);
		_peerSearchSelected = -1;
		setPeerSearchPressed(-1);
		_previewSelected = -1;
		setPreviewPressed(-1);
		_searchedSelected = -1;
		setSearchedPressed(-1);
	} else if (_state == WidgetState::Filtered) {
		_collapsedSelected = -1;
		setCollapsedPressed(-1);
		_selected = nullptr;
		clearPressed();
	}
}

bool InnerWidget::lookupIsInBotAppButton(
		Row *row,
		QPoint localPosition) {
	if (const auto user = MaybeBotWithApp(row)) {
		const auto it = _rightButtons.find(user->id);
		if (it != _rightButtons.end()) {
			const auto s = it->second.bg.size() / style::DevicePixelRatio();
			const auto r = QRect(
				width() - s.width() - st::dialogRowOpenBotRight,
				st::dialogRowOpenBotTop,
				s.width(),
				s.height());
			if (r.contains(localPosition)) {
				return true;
			}
		}
	}
	return false;
}

void InnerWidget::selectByMouse(QPoint globalPosition) {
	const auto local = mapFromGlobal(globalPosition);
	if (updateReorderPinned(local)) {
		return;
	}
	_mouseSelection = true;
	_lastMousePosition = globalPosition;
	_lastRowLocalMouseX = local.x();

	const auto tagBase = QPoint(
		_searchTagsLeft,
		st::dialogsSearchTagBottom / 2);
	const auto tagPoint = local - tagBase;
	const auto inTags = _searchTags
		&& QRect(
			tagBase,
			QSize(width() - 2 * _searchTagsLeft, _searchTags->height())
		).contains(local);
	const auto tagLink = inTags
		? _searchTags->lookupHandler(tagPoint)
		: nullptr;
	ClickHandler::setActive(tagLink);
	if (inTags) {
		setCursor(tagLink ? style::cur_pointer : style::cur_default);
	}

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
		const auto mappedY = selected ? mouseY - offset - selected->top() : 0;
		const auto selectedTopicJump = selected
			&& selected->lookupIsInTopicJump(local.x(), mappedY);
		const auto selectedBotApp = selected
			&& lookupIsInBotAppButton(selected, QPoint(local.x(), mappedY));
		if (_collapsedSelected != collapsedSelected
			|| _selected != selected
			|| _selectedTopicJump != selectedTopicJump
			|| _selectedBotApp != selectedBotApp) {
			updateSelectedRow();
			_selected = selected;
			_selectedTopicJump = selectedTopicJump;
			_selectedBotApp = selectedBotApp;
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
			auto skip = hashtagsOffset();
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
			const auto mappedY = (filteredSelected >= 0)
				? mouseY - skip - _filterResults[filteredSelected].top
				: 0;
			const auto selectedTopicJump = (filteredSelected >= 0)
				&& _filterResults[filteredSelected].row->lookupIsInTopicJump(
					local.x(),
					mappedY);
			const auto selectedBotApp = (filteredSelected >= 0)
				&& lookupIsInBotAppButton(
					_filterResults[filteredSelected].row,
					QPoint(local.x(), mappedY));
			if (_filteredSelected != filteredSelected
				|| _selectedTopicJump != selectedTopicJump
				|| _selectedBotApp != selectedBotApp) {
				updateSelectedRow();
				_filteredSelected = filteredSelected;
				_selectedTopicJump = selectedTopicJump;
				_selectedBotApp = selectedBotApp;
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
		if (!_previewResults.empty()) {
			auto skip = previewOffset();
			auto previewSelected = (mouseY >= skip) ? ((mouseY - skip) / _st->height) : -1;
			if (previewSelected < 0 || previewSelected >= _previewResults.size()) {
				previewSelected = -1;
			}
			if (_previewSelected != previewSelected) {
				updateSelectedRow();
				_previewSelected = previewSelected;
				updateSelectedRow();
			}
			auto selectedMorePosts = false;
			const auto from = skip - st::searchedBarHeight;
			if (mouseY <= skip && mouseY >= from) {
				const auto left = width()
					- _morePostsWidth
					- 2 * st::searchedBarPosition.x();
				if (_morePostsWidth > 0 && local.x() >= left) {
					selectedMorePosts = true;
				}
			}
			if (_selectedMorePosts != selectedMorePosts) {
				update(0, from, width(), st::searchedBarHeight);
				_selectedMorePosts = selectedMorePosts;
			}
		}
		if (!_searchResults.empty()) {
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
			auto selectedChatTypeFilter = false;
			const auto from = skip - st::searchedBarHeight;
			if (hasChatTypeFilter() && mouseY <= skip && mouseY >= from) {
				const auto left = width()
					- _chatTypeFilterWidth
					- 2 * st::searchedBarPosition.x();
				if (_chatTypeFilterWidth > 0 && local.x() >= left) {
					selectedChatTypeFilter = true;
				}
			}
			if (_selectedChatTypeFilter != selectedChatTypeFilter) {
				update(0, from, width(), st::searchedBarHeight);
				_selectedChatTypeFilter = selectedChatTypeFilter;
			}
		}
		if (!inTags && wasSelected != isSelected()) {
			setCursor(wasSelected ? style::cur_default : style::cur_pointer);
		}
	}
}

RowDescriptor InnerWidget::computeChatPreviewRow() const {
	auto result = computeChosenRow();
	if (const auto peer = result.key.peer()) {
		const auto topicId = _pressedTopicJump
			? _pressedTopicJumpRootId
			: 0;
		if (const auto topic = peer->forumTopicFor(topicId)) {
			return { topic, FullMsgId() };
		}
	}
	return { result.key, result.message.fullId };
}

void InnerWidget::processGlobalForceClick(QPoint globalPosition) {
	const auto parent = parentWidget();
	if (_pressButton == Qt::LeftButton
		&& parent->rect().contains(parent->mapFromGlobal(globalPosition))) {
		showChatPreview();
	}
}

void InnerWidget::mousePressEvent(QMouseEvent *e) {
	selectByMouse(e->globalPos());

	_pressButton = e->button();
	setPressed(_selected, _selectedTopicJump, _selectedBotApp);
	setCollapsedPressed(_collapsedSelected);
	setHashtagPressed(_hashtagSelected);
	_hashtagDeletePressed = _hashtagDeleteSelected;
	setFilteredPressed(_filteredSelected, _selectedTopicJump, _selectedBotApp);
	setPeerSearchPressed(_peerSearchSelected);
	setPreviewPressed(_previewSelected);
	setSearchedPressed(_searchedSelected);
	_pressedMorePosts = _selectedMorePosts;
	_pressedChatTypeFilter = _selectedChatTypeFilter;

	const auto alt = (e->modifiers() & Qt::AltModifier);
	if (alt && showChatPreview()) {
		return;
	} else if (!alt && isUserpicPress()) {
		scheduleChatPreview(e->globalPos());
	}

	if (base::in_range(_collapsedSelected, 0, _collapsedRows.size())) {
		auto row = &_collapsedRows[_collapsedSelected]->row;
		row->addRipple(e->pos(), QSize(width(), st::dialogsImportantBarHeight), [this, index = _collapsedSelected] {
			update(0, (index * st::dialogsImportantBarHeight), width(), st::dialogsImportantBarHeight);
		});
	} else if (_pressed) {
		auto row = _pressed;
		const auto weak = Ui::MakeWeak(this);
		const auto updateCallback = [weak, row] {
			const auto strong = weak.data();
			if (!strong || !strong->_pinnedShiftAnimation.animating()) {
				row->entry()->updateChatListEntry();
			}
		};
		const auto origin = e->pos()
			- QPoint(0, dialogsOffset() + _pressed->top());
		if (addBotAppRipple(origin, updateCallback)) {
		} else if (_pressedTopicJump) {
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
		const auto origin = e->pos() - QPoint(0, hashtagsOffset() + _hashtagPressed * st::mentionHeight);
		row->addRipple(origin, QSize(width(), st::mentionHeight), [this, index = _hashtagPressed] {
			update(0, hashtagsOffset() + index * st::mentionHeight, width(), st::mentionHeight);
		});
	} else if (base::in_range(_filteredPressed, 0, _filterResults.size())) {
		const auto &result = _filterResults[_filteredPressed];
		const auto row = result.row;
		const auto filterId = _filterId;
		const auto origin = e->pos()
			- QPoint(0, filteredOffset() + result.top);
		const auto updateCallback = [=] { repaintDialogRow(filterId, row); };
		if (addBotAppRipple(origin, updateCallback)) {
		} else if (_pressedTopicJump) {
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
	ClickHandler::pressed();
	if (anim::Disabled()
		&& !_chatPreviewScheduled
		&& (!_pressed || !_pressed->entry()->isPinnedDialog(_filterId))) {
		mousePressReleased(e->globalPos(), e->button(), e->modifiers());
	}
}

bool InnerWidget::addBotAppRipple(QPoint origin, Fn<void()> updateCallback) {
	if (!(_pressedBotApp && _pressedBotAppData)) {
		return false;
	}
	const auto size = _pressedBotAppData->bg.size()
		/ style::DevicePixelRatio();
	if (!_pressedBotAppData->ripple) {
		_pressedBotAppData->ripple = std::make_unique<Ui::RippleAnimation>(
			st::defaultRippleAnimation,
			Ui::RippleAnimation::RoundRectMask(size, size.height() / 2),
			updateCallback);
	}
	const auto shift = QPoint(
		width() - size.width() - st::dialogRowOpenBotRight,
		st::dialogRowOpenBotTop);
	_pressedBotAppData->ripple->add(origin - shift);
	return true;
}

const std::vector<Key> &InnerWidget::pinnedChatsOrder() const {
	const auto owner = &session().data();
	return _savedSublists
		? owner->pinnedChatsOrder(&owner->savedMessages())
		: _openedForum
		? owner->pinnedChatsOrder(_openedForum)
		: _filterId
		? owner->pinnedChatsOrder(_filterId)
		: owner->pinnedChatsOrder(_openedFolder);
}

void InnerWidget::checkReorderPinnedStart(QPoint localPosition) {
	if (!_pressed
		|| _dragging
		|| (_state != WidgetState::Default)
		|| _pressedBotApp) {
		return;
	} else if (qAbs(localPosition.y() - _dragStart.y())
		< style::ConvertScale(kStartReorderThreshold)) {
		return;
	}
	_dragging = _pressed;
	startReorderPinned(localPosition);
}

void InnerWidget::startReorderPinned(QPoint localPosition) {
	Expects(_dragging != nullptr);

	cancelChatPreview();
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
	if (_savedSublists) {
		session().api().savePinnedOrder(&session().data().savedMessages());
	} else if (_openedForum) {
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
		_touchDragStartGlobal = {};
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

bool InnerWidget::finishReorderOnRelease() {
	if (!_dragging) {
		return false;
	}
	updateReorderIndexGetCount();
	if (_draggingIndex >= 0) {
		_pinnedRows[_draggingIndex].yadd.start(0.);
		_pinnedRows[_draggingIndex].animStartTime = crl::now();
		if (!_pinnedShiftAnimation.animating()) {
			_pinnedShiftAnimation.start();
		}
	}
	finishReorderPinned();
	return true;
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
	auto shiftHeight = 0;
	auto now = crl::now();
	if (_dragStart.y() > localPosition.y() && _draggingIndex > 0) {
		shift = -floorclamp(_dragStart.y() - localPosition.y() + (draggingHeight / 2), draggingHeight, 0, _draggingIndex);

		for (auto from = _draggingIndex, to = _draggingIndex + shift; from > to; --from) {
			_shownList->movePinned(_dragging, -1);
			std::swap(_pinnedRows[from], _pinnedRows[from - 1]);
			_pinnedRows[from].yadd = anim::value(_pinnedRows[from].yadd.current() - draggingHeight, 0);
			_pinnedRows[from].animStartTime = now;
			shiftHeight -= (*(_shownList->cbegin() + from))->height();
		}
	} else if (_dragStart.y() < localPosition.y() && _draggingIndex + 1 < pinnedCount) {
		shift = floorclamp(localPosition.y() - _dragStart.y() + (draggingHeight / 2), draggingHeight, 0, pinnedCount - _draggingIndex - 1);

		for (auto from = _draggingIndex, to = _draggingIndex + shift; from < to; ++from) {
			_shownList->movePinned(_dragging, 1);
			std::swap(_pinnedRows[from], _pinnedRows[from + 1]);
			_pinnedRows[from].yadd = anim::value(_pinnedRows[from].yadd.current() + draggingHeight, 0);
			_pinnedRows[from].animStartTime = now;
			shiftHeight += (*(_shownList->cbegin() + from))->height();
		}
	}
	if (shift) {
		_draggingIndex += shift;
		_aboveIndex = _draggingIndex;
		_dragStart.setY(_dragStart.y() + shiftHeight);
		if (!_pinnedShiftAnimation.animating()) {
			_pinnedShiftAnimation.start();
		}
	}
	_aboveTopShift = qCeil(_pinnedRows[_aboveIndex].yadd.current());
	_pinnedRows[_draggingIndex].yadd = anim::value(
		yaddWas - shiftHeight,
		localPosition.y() - _dragStart.y());
	if (!_pinnedRows[_draggingIndex].animStartTime) {
		_pinnedRows[_draggingIndex].yadd.finish();
	}
	pinnedShiftAnimationCallback(now);

	const auto delta = [&] {
		if (localPosition.y() < _visibleTop) {
			return localPosition.y() - _visibleTop;
		} else if ((_savedSublists || _openedFolder || _openedForum || _filterId)
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
		const auto maxHeight = st::taggedForumDialogRow.height;
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
	if (_chatPreviewScheduled) {
		_controller->cancelScheduledPreview();
	}
	_pressButton = Qt::NoButton;

	const auto wasDragging = finishReorderOnRelease();

	auto collapsedPressed = _collapsedPressed;
	setCollapsedPressed(-1);
	const auto pressedTopicRootId = _pressedTopicJumpRootId;
	const auto pressedTopicJump = _pressedTopicJump;
	const auto pressedBotApp = _pressedBotApp;
	auto pressed = _pressed;
	clearPressed();
	auto hashtagPressed = _hashtagPressed;
	setHashtagPressed(-1);
	auto hashtagDeletePressed = _hashtagDeletePressed;
	_hashtagDeletePressed = false;
	auto filteredPressed = _filteredPressed;
	setFilteredPressed(-1, false, false);
	auto peerSearchPressed = _peerSearchPressed;
	setPeerSearchPressed(-1);
	auto previewPressed = _previewPressed;
	setPreviewPressed(-1);
	auto searchedPressed = _searchedPressed;
	setSearchedPressed(-1);
	const auto pressedMorePosts = _pressedMorePosts;
	_pressedMorePosts = false;
	const auto pressedChatTypeFilter = _pressedChatTypeFilter;
	_pressedChatTypeFilter = false;
	if (wasDragging) {
		selectByMouse(globalPosition);
	}
	if (_pressedBotAppData && _pressedBotAppData->ripple) {
		_pressedBotAppData->ripple->lastStop();
	}
	updateSelectedRow();
	if (!wasDragging && button == Qt::LeftButton) {
		if ((collapsedPressed >= 0 && collapsedPressed == _collapsedSelected)
			|| (pressed
				&& pressed == _selected
				&& pressedTopicJump == _selectedTopicJump
				&& pressedBotApp == _selectedBotApp)
			|| (hashtagPressed >= 0
				&& hashtagPressed == _hashtagSelected
				&& hashtagDeletePressed == _hashtagDeleteSelected)
			|| (filteredPressed >= 0 && filteredPressed == _filteredSelected)
			|| (peerSearchPressed >= 0
				&& peerSearchPressed == _peerSearchSelected)
			|| (previewPressed >= 0
				&& previewPressed == _previewSelected)
			|| (searchedPressed >= 0
				&& searchedPressed == _searchedSelected)
			|| (pressedMorePosts
				&& pressedMorePosts == _selectedMorePosts)
			|| (pressedChatTypeFilter
				&& pressedChatTypeFilter == _selectedChatTypeFilter)) {
			if (pressedBotApp && (pressed || filteredPressed >= 0)) {
				const auto &row = pressed
					? pressed
					: _filterResults[filteredPressed].row.get();
				if (const auto user = MaybeBotWithApp(row)) {
					_openBotMainAppRequests.fire(peerToUser(user->id));
				}
			} else {
				chooseRow(modifiers, pressedTopicRootId);
			}
		}
	}
	if (auto activated = ClickHandler::unpressed()) {
		ActivateClickHandler(window(), activated, ClickContext{
			button,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = _controller,
			}) });
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

void InnerWidget::setPressed(
		Row *pressed,
		bool pressedTopicJump,
		bool pressedBotApp) {
	if ((_pressed != pressed)
		|| (pressed && _pressedTopicJump != pressedTopicJump)
		|| (pressed && _pressedBotApp != pressedBotApp)) {
		if (_pressed) {
			_pressed->stopLastRipple();
		}
		if (_pressedBotAppData && _pressedBotAppData->ripple) {
			_pressedBotAppData->ripple->lastStop();
		}
		_pressed = pressed;
		if (pressed || !pressedTopicJump || !pressedBotApp) {
			_pressedTopicJump = pressedTopicJump;
			_pressedBotApp = pressedBotApp;
			if (pressedBotApp) {
				if (const auto user = MaybeBotWithApp(pressed)) {
					const auto it = _rightButtons.find(user->id);
					if (it != _rightButtons.end()) {
						_pressedBotAppData = &(it->second);
					}
				}
			}
			const auto history = pressedTopicJump
				? pressed->history()
				: nullptr;
			const auto item = history ? history->chatListMessage() : nullptr;
			_pressedTopicJumpRootId = item ? item->topicRootId() : MsgId();
		}
	}
}

void InnerWidget::clearPressed() {
	setPressed(nullptr, false, false);
}

void InnerWidget::setHashtagPressed(int pressed) {
	if (base::in_range(_hashtagPressed, 0, _hashtagResults.size())) {
		_hashtagResults[_hashtagPressed]->row.stopLastRipple();
	}
	_hashtagPressed = pressed;
}

void InnerWidget::setFilteredPressed(
		int pressed,
		bool pressedTopicJump,
		bool pressedBotApp) {
	if (_filteredPressed != pressed
		|| (pressed >= 0 && _pressedTopicJump != pressedTopicJump)
		|| (pressed >= 0 && _pressedBotApp != pressedBotApp)) {
		if (base::in_range(_filteredPressed, 0, _filterResults.size())) {
			_filterResults[_filteredPressed].row->stopLastRipple();
		}
		if (_pressedBotAppData && _pressedBotAppData->ripple) {
			_pressedBotAppData->ripple->lastStop();
		}
		_filteredPressed = pressed;
		if (pressed >= 0 || !pressedTopicJump || !pressedBotApp) {
			_pressedTopicJump = pressedTopicJump;
			_pressedBotApp = pressedBotApp;
			if (pressed >= 0 && pressedBotApp) {
				const auto &row = _filterResults[pressed].row;
				if (const auto history = row->history()) {
					const auto it = _rightButtons.find(history->peer->id);
					if (it != _rightButtons.end()) {
						_pressedBotAppData = &(it->second);
					}
				}
			}
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

void InnerWidget::setPreviewPressed(int pressed) {
	if (base::in_range(_previewPressed, 0, _previewResults.size())) {
		_previewResults[_previewPressed]->stopLastRipple();
	}
	_previewPressed = pressed;
}

void InnerWidget::setSearchedPressed(int pressed) {
	if (base::in_range(_searchedPressed, 0, _searchResults.size())) {
		_searchResults[_searchedPressed]->stopLastRipple();
	}
	_searchedPressed = pressed;
}

void InnerWidget::resizeEvent(QResizeEvent *e) {
	if (_searchTags) {
		_searchTags->resizeToWidth(width() - 2 * _searchTagsLeft);
	}
	resizeEmpty();
	moveSearchIn();
}

void InnerWidget::moveSearchIn() {
	if (!_searchIn) {
		return;
	}
	const auto searchInWidth = std::max(
		width(),
		st::columnMinimalWidthLeft - _narrowWidth);
	_searchIn->resizeToWidth(searchInWidth);
	_searchIn->moveToLeft(0, searchInChatOffset());
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
		setPressed(newRow, _pressedTopicJump, _pressedBotApp);
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
		} else if (event.key.sublist()) {
			return _savedSublists;
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
				: key.sublist()
				? _savedSublists
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
	if (_savedSublists) {
		if (!entry->asSublist()) {
			return nullptr;
		}
	} else if (_openedForum) {
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
			update(0, hashtagsOffset() + _hashtagSelected * st::mentionHeight, width(), st::mentionHeight);
		} else if (_filteredSelected >= 0) {
			if (_filteredSelected < _filterResults.size()) {
				const auto &result = _filterResults[_filteredSelected];
				update(0, filteredOffset() + result.top, width(), result.row->height());
			}
		} else if (_peerSearchSelected >= 0) {
			update(0, peerSearchOffset() + _peerSearchSelected * st::dialogsRowHeight, width(), st::dialogsRowHeight);
		} else if (_previewSelected >= 0) {
			update(0, previewOffset() + _previewSelected * _st->height, width(), _st->height);
		} else if (_searchedSelected >= 0) {
			update(0, searchedOffset() + _searchedSelected * _st->height, width(), _st->height);
		}
	}
}

void InnerWidget::refreshShownList() {
	const auto list = _savedSublists
		? session().data().savedMessages().chatsList()->indexed()
		: _openedForum
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
		_selectedMorePosts = false;
		_selectedChatTypeFilter = false;
		_selected = nullptr;
		_filteredSelected
			= _searchedSelected
			= _previewSelected
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
		|| _searchState.inChat) {
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

bool InnerWidget::showChatPreview() {
	const auto row = computeChatPreviewRow();
	const auto callback = crl::guard(this, [=](bool shown) {
		chatPreviewShown(shown, row);
	});
	return _controller->showChatPreview(row, callback);
}

void InnerWidget::chatPreviewShown(bool shown, RowDescriptor row) {
	_chatPreviewScheduled = false;
	if (shown) {
		_chatPreviewRow = row;
		if (base::take(_chatPreviewTouchGlobal)) {
			_touchCancelRequests.fire({});
		}
		ClickHandler::unpressed();
		mousePressReleased(QCursor::pos(), Qt::NoButton, Qt::NoModifier);
	} else {
		cancelChatPreview();
		const auto globalPosition = QCursor::pos();
		if (rect().contains(mapFromGlobal(globalPosition))) {
			setMouseTracking(true);
			selectByMouse(globalPosition);
		}
	}
}

bool InnerWidget::scheduleChatPreview(QPoint positionOverride) {
	const auto row = computeChatPreviewRow();
	const auto callback = crl::guard(this, [=](bool shown) {
		chatPreviewShown(shown, row);
	});
	_chatPreviewScheduled = _controller->scheduleChatPreview(
		row,
		callback,
		nullptr,
		positionOverride);
	return _chatPreviewScheduled;
}

void InnerWidget::contextMenuEvent(QContextMenuEvent *e) {
	_menu = nullptr;

	const auto fromMouse = e->reason() == QContextMenuEvent::Mouse;

	if (fromMouse) {
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
			} else if (base::in_range(_previewSelected, 0, _previewResults.size())) {
				return {
					_previewResults[_previewSelected]->item()->history(),
					_previewResults[_previewSelected]->item()->fullId()
				};
			} else if (base::in_range(_searchedSelected, 0, _searchResults.size())) {
				return {
					_searchResults[_searchedSelected]->item()->history(),
					_searchResults[_searchedSelected]->item()->fullId()
				};
			}
		}
		return RowDescriptor();
	}();
	if (!row.key) {
		return;
	}

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
		if (!fromMouse) {
			return;
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

bool InnerWidget::processTouchEvent(not_null<QTouchEvent*> e) {
	const auto point = e->touchPoints().empty()
		? std::optional<QPoint>()
		: e->touchPoints().front().screenPos().toPoint();
	switch (e->type()) {
	case QEvent::TouchBegin: {
		if (!point) {
			return false;
		}
		selectByMouse(*point);
		if (isUserpicPressOnWide() && scheduleChatPreview(*point)) {
			_chatPreviewTouchGlobal = point;
		} else if (!_dragging) {
			_touchDragStartGlobal = point;
			_touchDragPinnedTimer.callOnce(QApplication::startDragTime());
		}
	} break;

	case QEvent::TouchUpdate: {
		if (!point) {
			return false;
		}
		if (_chatPreviewTouchGlobal) {
			const auto delta = (*_chatPreviewTouchGlobal - *point);
			if (delta.manhattanLength() > _st->photoSize) {
				cancelChatPreview();
			}
		}
		if (_touchDragStartGlobal && _dragging) {
			updateReorderPinned(mapFromGlobal(*point));
			return _dragging != nullptr;
		} else if (_touchDragStartGlobal) {
			const auto delta = (*_touchDragStartGlobal - *point);
			if (delta.manhattanLength() > QApplication::startDragDistance()) {
				if (_touchDragPinnedTimer.isActive()) {
					_touchDragPinnedTimer.cancel();
					_touchDragStartGlobal = {};
					_touchDragNowGlobal = {};
				} else {
					dragPinnedFromTouch();
				}
			} else {
				_touchDragNowGlobal = point;
			}
		}
	} break;

	case QEvent::TouchEnd:
	case QEvent::TouchCancel: {
		if (_chatPreviewTouchGlobal) {
			cancelChatPreview();
		}
		if (_touchDragStartGlobal) {
			_touchDragStartGlobal = {};
			return finishReorderOnRelease();
		}
	} break;
	}
	return false;
}

void InnerWidget::dragPinnedFromTouch() {
	Expects(_touchDragStartGlobal.has_value());

	const auto global = *_touchDragStartGlobal;
	_touchDragPinnedTimer.cancel();
	selectByMouse(global);
	if (!_selected || _dragging || _state != WidgetState::Default) {
		return;
	}
	_dragStart = mapFromGlobal(global);
	_dragging = _selected;
	const auto now = mapFromGlobal(_touchDragNowGlobal.value_or(global));
	startReorderPinned(now);
	updateReorderPinned(now);
}

bool InnerWidget::hasChatTypeFilter() const {
	return !_searchResults.empty()
		&& (_searchState.tab == ChatSearchTab::MyMessages);
}

void InnerWidget::searchRequested(bool loading) {
	_searchWaiting = false;
	_searchLoading = loading;
	if (loading) {
		clearSearchResults(true);
		clearPreviewResults();
	}
	refresh(true);
}

void InnerWidget::applySearchState(SearchState state) {
	if (_searchState == state) {
		return;
	}
	auto withSameQuery = state;
	withSameQuery.query = _searchState.query;
	const auto otherChanged = (_searchState != withSameQuery);

	const auto ignoreInChat = (state.tab == ChatSearchTab::MyMessages)
		|| (state.tab == ChatSearchTab::PublicPosts);
	const auto sublist = ignoreInChat ? nullptr : state.inChat.sublist();
	const auto peer = ignoreInChat ? nullptr : state.inChat.peer();
	if (const auto migrateFrom = peer ? peer->migrateFrom() : nullptr) {
		_searchInMigrated = peer->owner().history(migrateFrom);
	} else {
		_searchInMigrated = nullptr;
	}
	if (peer && peer->isSelf()) {
		const auto reactions = &peer->owner().reactions();
		_searchTags = std::make_unique<SearchTags>(
			&peer->owner(),
			reactions->myTagsValue(sublist),
			state.tags);

		_searchTags->repaintRequests() | rpl::start_with_next([=] {
			const auto height = _searchTags->height();
			update(0, 0, width(), height);
		}, _searchTags->lifetime());

		_searchTags->menuRequests(
		) | rpl::start_with_next([=](Data::ReactionId id) {
			HistoryView::ShowTagInListMenu(
				&_menu,
				_lastMousePosition.value_or(QCursor::pos()),
				this,
				id,
				_controller);
		}, _searchTags->lifetime());

		_searchTags->heightValue() | rpl::skip(
			1
		) | rpl::start_with_next([=] {
			refresh();
			moveSearchIn();
		}, _searchTags->lifetime());
	} else {
		_searchTags = nullptr;
		state.tags.clear();
	}
	_searchFromShown = ignoreInChat
		? nullptr
		: sublist
		? sublist->peer().get()
		: state.fromPeer;
	if (state.inChat) {
		onHashtagFilterUpdate(QStringView());
	}
	if (state.filter != _searchState.filter) {
		_chatTypeFilterWidth = 0;
		update();
	}
	_searchState = std::move(state);
	_searchHashOrCashtag = IsHashOrCashtagSearchQuery(_searchState.query);
	_searchWithPostsPreview = computeSearchWithPostsPreview();

	updateSearchIn();
	moveSearchIn();

	auto newFilter = _searchState.query;
	const auto mentionsSearch = (newFilter == u"@"_q);
	const auto words = mentionsSearch
		? QStringList(newFilter)
		: TextUtilities::PrepareSearchWords(newFilter);
	newFilter = words.isEmpty() ? QString() : words.join(' ');
	if (newFilter != _filter || otherChanged) {
		_filter = newFilter;
		if (_filter.isEmpty()
			&& !_searchState.fromPeer
			&& _searchState.tags.empty()) {
			clearFilter();
		} else {
			setState(WidgetState::Filtered);
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
					i->row->recountHeight(_narrowRatio, _filterId);
					top += i->row->height();
				}
			};
			if (_searchState.filterChatsList() && !words.isEmpty()) {
				if (_savedSublists) {
					const auto owner = &session().data();
					append(owner->savedMessages().chatsList()->indexed());
				} else if (_openedForum) {
					append(_openedForum->topicsList()->indexed());
				} else {
					const auto owner = &session().data();
					append(owner->chatsList()->indexed());
					const auto id = Data::Folder::kId;
					if (const auto add = owner->folderLoaded(id)) {
						append(add->chatsList()->indexed());
					}
					append(owner->contactsNoChatsList());
				}
			}
		}
		clearMouseSelection(true);
	}
	if (_state != WidgetState::Default) {
		_searchWaiting = true;
		_searchRequests.fire(otherChanged
			? SearchRequestDelay::Instant
			: SearchRequestDelay::Delayed);
		if (_searchWaiting) {
			refresh(true);
		}
	}
}

void InnerWidget::onHashtagFilterUpdate(QStringView newFilter) {
	if (newFilter.isEmpty()
		|| newFilter.at(0) != '#'
		|| _searchState.inChat) {
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
	row->recountHeight(_narrowRatio, _filterId);
	const auto &[i, ok] = _filterResultsGlobal.emplace(key, std::move(row));
	const auto height = filteredHeight();
	_filterResults.emplace_back(i->second.get());
	_filterResults.back().top = height;
	trackResultsHistory(key.owningHistory());
}

InnerWidget::~InnerWidget() {
	session().data().stories().decrementPreloadingMainSources();
	clearSearchResults();
}

void InnerWidget::clearSearchResults(bool clearPeerSearchResults) {
	if (clearPeerSearchResults) {
		_peerSearchResults.clear();
	}
	_searchResults.clear();
	_searchedCount = _searchedMigratedCount = 0;
}

void InnerWidget::clearPreviewResults() {
	_previewResults.clear();
	_previewCount = 0;
}

void InnerWidget::trackResultsHistory(not_null<History*> history) {
	if (!_trackedHistories.emplace(history).second) {
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
	}, _trackedLifetime);

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
			if (_chatPreviewRow.key.topic() == topic) {
				_chatPreviewRow = {};
			}
		}, _trackedLifetime);
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
		} else if (base::in_range(_previewSelected, 0, _previewResults.size())) {
			if (const auto item = _previewResults[_previewSelected]->item()) {
				if (const auto topic = item->topic()) {
					return topic;
				}
				return item->history();
			}
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

rpl::producer<ChatSearchTab> InnerWidget::changeSearchTabRequests() const {
	return _changeSearchTabRequests.events();
}

auto InnerWidget::changeSearchFilterRequests() const
-> rpl::producer<ChatTypeFilter>{
	return _changeSearchFilterRequests.events();
}

rpl::producer<> InnerWidget::cancelSearchRequests() const {
	return _cancelSearchRequests.events();
}

rpl::producer<> InnerWidget::cancelSearchFromRequests() const {
	return _cancelSearchFromRequests.events();
}

rpl::producer<> InnerWidget::changeSearchFromRequests() const {
	return _changeSearchFromRequests.events();
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::mustScrollTo() const {
	return _mustScrollTo.events();
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::dialogMoved() const {
	return _dialogMoved.events();
}

rpl::producer<SearchRequestDelay> InnerWidget::searchRequests() const {
	return _searchRequests.events();
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
	_searchWaiting = false;
	_searchLoading = false;

	const auto uniquePeers = uniqueSearchResults();
	const auto withPreview = _searchWithPostsPreview;
	const auto toPreview = withPreview && type.posts;
	if (type.start && !type.migrated && (!withPreview || !type.posts)) {
		clearSearchResults(false);
	}
	if (!withPreview || toPreview) {
		clearPreviewResults();
	}

	const auto key = (!_openedForum || _searchState.inChat.topic())
		? _searchState.inChat
		: Key(_openedForum->history());
	if (inject
		&& (!_searchState.inChat
			|| inject->history() == _searchState.inChat.history())) {
		Assert(_searchResults.empty());
		Assert(!toPreview);
		const auto index = int(_searchResults.size());
		_searchResults.push_back(
			std::make_unique<FakeRow>(
				key,
				inject,
				[=] { repaintSearchResult(index); }));
		trackResultsHistory(inject->history());
		++fullCount;
	}
	auto &results = toPreview ? _previewResults : _searchResults;
	for (const auto &item : messages) {
		const auto history = item->history();
		if (toPreview || !uniquePeers || !hasHistoryInResults(history)) {
			const auto index = int(results.size());
			const auto repaint = toPreview
				? Fn<void()>([=] { repaintSearchResult(index); })
				: [=] { repaintPreviewResult(index); };
			results.push_back(
				std::make_unique<FakeRow>(key, item, repaint));
			trackResultsHistory(history);
			if (!toPreview && uniquePeers && !history->unreadCountKnown()) {
				history->owner().histories().requestDialogEntry(history);
			} else if (toPreview && results.size() >= kPreviewPostsLimit) {
				break;
			}
		}
	}
	if (type.migrated) {
		_searchedMigratedCount = fullCount;
	} else if (!withPreview || !toPreview) {
		_searchedCount = fullCount;
	} else {
		_previewCount = fullCount;
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
	refreshEmpty();
	if (_searchTags) {
		_searchTagsLeft = st::dialogsFilterSkip
			+ st::dialogsFilterPadding.x();
		_searchTags->resizeToWidth(width() - 2 * _searchTagsLeft);
	}
	auto h = 0;
	if (_state == WidgetState::Default) {
		if (_shownList->empty()) {
			h = st::dialogsEmptyHeight;
		} else {
			h = dialogsOffset() + _shownList->height();
		}
	} else if (_state == WidgetState::Filtered) {
		if (_searchEmpty && !_searchEmpty->isHidden()) {
			h = searchedOffset() + st::recentPeersEmptyHeightMin;
			_searchEmpty->setMinimalHeight(st::recentPeersEmptyHeightMin);
			_searchEmpty->move(0, h - st::recentPeersEmptyHeightMin);
		} else if (_loadingAnimation) {
			h = searchedOffset() + _loadingAnimation->height();
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
	update();
}

void InnerWidget::refreshEmpty() {
	if (_state == WidgetState::Filtered) {
		const auto empty = _filterResults.empty()
			&& _searchResults.empty()
			&& _peerSearchResults.empty()
			&& _hashtagResults.empty();
		if (_searchLoading || _searchWaiting || !empty) {
			if (_searchEmpty) {
				_searchEmpty->hide();
			}
		} else if (_searchEmptyState != _searchState) {
			_searchEmptyState = _searchState;
			_searchEmpty = MakeSearchEmpty(this, _searchState, [=] {
				_changeSearchFilterRequests.fire(ChatTypeFilter::All);
			});
			if (_controller->session().data().chatsListLoaded()) {
				_searchEmpty->animate();
			}
		} else if (_searchEmpty) {
			_searchEmpty->show();
		}

		if ((!_searchLoading && !_searchWaiting) || !empty) {
			_loadingAnimation.destroy();
		} else if (!_loadingAnimation) {
			_loadingAnimation = Ui::CreateLoadingDialogRowWidget(
				this,
				*_st,
				2);
			_loadingAnimation->resizeToWidth(width());
			_loadingAnimation->move(0, searchedOffset());
			_loadingAnimation->show();
		}
	} else {
		_searchEmpty.destroy();
		_loadingAnimation.destroy();
		_searchEmptyState = {};
	}

	const auto data = &session().data();
	const auto state = !_shownList->empty()
		? EmptyState::None
		: _savedSublists
		? (data->savedMessages().chatsList()->loaded()
			? EmptyState::EmptySavedSublists
			: EmptyState::Loading)
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
		: (state == EmptyState::EmptySavedSublists)
		? tr::lng_no_saved_sublists()
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
	resizeEmpty();
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

void InnerWidget::resizeEmpty() {
	if (_empty) {
		const auto skip = st::dialogsEmptySkip;
		_empty->resizeToWidth(width() - 2 * skip);
		_empty->move(skip, (st::dialogsEmptyHeight - _empty->height()) / 2);
	}
	if (_searchEmpty) {
		_searchEmpty->resizeToWidth(width());
		_searchEmpty->move(0, searchedOffset());
	}
	if (_loadingAnimation) {
		_loadingAnimation->resizeToWidth(width());
		_loadingAnimation->move(0, searchedOffset());
	}
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
				= _previewSelected
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

auto InnerWidget::searchTagsChanges() const
-> rpl::producer<std::vector<Data::ReactionId>> {
	return _searchTags
		? _searchTags->selectedChanges()
		: rpl::never<std::vector<Data::ReactionId>>();
}

void InnerWidget::updateSearchIn() {
	if (!_searchState.inChat
		&& _searchHashOrCashtag == HashOrCashtag::None) {
		_searchIn = nullptr;
		return;
	} else if (!_searchIn) {
		_searchIn = std::make_unique<ChatSearchIn>(this);
		_searchIn->show();
		_searchIn->changeFromRequests() | rpl::start_to_stream(
			_changeSearchFromRequests,
			_searchIn->lifetime());
		_searchIn->cancelFromRequests() | rpl::start_to_stream(
			_cancelSearchFromRequests,
			_searchIn->lifetime());
		_searchIn->cancelInRequests() | rpl::start_to_stream(
			_cancelSearchRequests,
			_searchIn->lifetime());
		_searchIn->tabChanges() | rpl::start_to_stream(
			_changeSearchTabRequests,
			_searchIn->lifetime());
	}

	const auto sublist = _searchState.inChat.sublist();
	const auto topic = _searchState.inChat.topic();
	const auto peer = _searchState.inChat.owningHistory()
		? _searchState.inChat.owningHistory()->peer.get()
		: _openedForum
		? _openedForum->channel().get()
		: nullptr;
	const auto topicIcon = !topic
		? nullptr
		: topic->iconId()
		? Ui::MakeEmojiThumbnail(
			&topic->owner(),
			Data::SerializeCustomEmojiId(topic->iconId()))
		: Ui::MakeEmojiThumbnail(
			&topic->owner(),
			Data::TopicIconEmojiEntity({
				.title = (topic->isGeneral()
					? Data::ForumGeneralIconTitle()
					: topic->title()),
				.colorId = (topic->isGeneral()
					? Data::ForumGeneralIconColor(st::windowSubTextFg->c)
					: topic->colorId()),
			}));
	const auto peerIcon = peer
		? Ui::MakeUserpicThumbnail(peer)
		: sublist
		? Ui::MakeUserpicThumbnail(sublist->peer())
		: nullptr;
	const auto myIcon = Ui::MakeIconThumbnail(st::menuIconChats);
	const auto publicIcon = (_searchHashOrCashtag != HashOrCashtag::None)
		? Ui::MakeIconThumbnail(st::menuIconChannel)
		: nullptr;
	const auto peerTabType = (peer && peer->isBroadcast())
		? ChatSearchPeerTabType::Channel
		: (peer && (peer->isChat() || peer->isMegagroup()))
		? ChatSearchPeerTabType::Group
		: ChatSearchPeerTabType::Chat;
	const auto fromImage = _searchFromShown
		? Ui::MakeUserpicThumbnail(_searchFromShown)
		: nullptr;
	const auto fromName = _searchFromShown
		? _searchFromShown->shortName()
		: QString();
	_searchIn->apply({
		{ ChatSearchTab::ThisTopic, topicIcon },
		{ ChatSearchTab::ThisPeer, peerIcon },
		{ ChatSearchTab::MyMessages, myIcon },
		{ ChatSearchTab::PublicPosts, publicIcon },
	}, _searchState.tab, peerTabType, fromImage, fromName);
}

void InnerWidget::repaintSearchResult(int index) {
	rtlupdate(
		0,
		searchedOffset() + index * _st->height,
		width(),
		_st->height);
}

void InnerWidget::repaintPreviewResult(int index) {
	rtlupdate(
		0,
		previewOffset() + index * _st->height,
		width(),
		_st->height);
}

bool InnerWidget::computeSearchWithPostsPreview() const {
	return 	(_searchHashOrCashtag != HashOrCashtag::None)
		&& (_searchState.tab == ChatSearchTab::MyMessages);
}

void InnerWidget::clearFilter() {
	if (_state == WidgetState::Filtered || _searchState.inChat) {
		if (_searchState.inChat) {
			setState(WidgetState::Filtered);
		} else {
			setState(WidgetState::Default);
		}
		_hashtagResults.clear();
		_filterResults.clear();
		_filterResultsGlobal.clear();
		_peerSearchResults.clear();
		_searchResults.clear();
		_previewResults.clear();
		_trackedHistories.clear();
		_trackedLifetime.destroy();
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
		if (_hashtagResults.empty()
			&& _filterResults.empty()
			&& _peerSearchResults.empty()
			&& _previewResults.empty()
			&& _searchResults.empty()) {
			return;
		}
		if ((_hashtagSelected < 0 || _hashtagSelected >= _hashtagResults.size())
			&& (_filteredSelected < 0 || _filteredSelected >= _filterResults.size())
			&& (_peerSearchSelected < 0 || _peerSearchSelected >= _peerSearchResults.size())
			&& (_previewSelected < 0 || _previewSelected >= _previewResults.size())
			&& (_searchedSelected < 0 || _searchedSelected >= _searchResults.size())) {
			if (_hashtagResults.empty() && _filterResults.empty() && _peerSearchResults.empty() && _previewResults.empty()) {
				_searchedSelected = 0;
			} else if (_hashtagResults.empty() && _filterResults.empty() && _peerSearchResults.empty()) {
				_previewSelected = 0;
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
				: base::in_range(_filteredSelected, 0, _filterResults.size())
				? (_hashtagResults.size() + _filteredSelected)
				: base::in_range(_peerSearchSelected, 0, _peerSearchResults.size())
				? (_peerSearchSelected + _filterResults.size() + _hashtagResults.size())
				: base::in_range(_previewSelected, 0, _previewResults.size())
				? (_previewSelected + _peerSearchResults.size() + _filterResults.size() + _hashtagResults.size())
				: (_searchedSelected + _previewResults.size() + _peerSearchResults.size() + _filterResults.size() + _hashtagResults.size());
			cur = std::clamp(
				cur + direction,
				0,
				static_cast<int>(_hashtagResults.size()
					+ _filterResults.size()
					+ _peerSearchResults.size()
					+ _previewResults.size()
					+ _searchResults.size()) - 1);
			if (cur < _hashtagResults.size()) {
				_hashtagSelected = cur;
				_filteredSelected = _peerSearchSelected = _previewSelected = _searchedSelected = -1;
			} else if (cur < _hashtagResults.size() + _filterResults.size()) {
				_filteredSelected = cur - _hashtagResults.size();
				_hashtagSelected = _peerSearchSelected = _previewSelected = _searchedSelected = -1;
			} else if (cur < _hashtagResults.size() + _filterResults.size() + _peerSearchResults.size()) {
				_peerSearchSelected = cur - _hashtagResults.size() - _filterResults.size();
				_hashtagSelected = _filteredSelected = _previewSelected = _searchedSelected = -1;
			} else if (cur < _hashtagResults.size() + _filterResults.size() + _peerSearchResults.size() + _previewResults.size()) {
				_previewSelected = cur - _hashtagResults.size() - _filterResults.size() - _peerSearchResults.size();
				_hashtagSelected = _filteredSelected = _peerSearchSelected = _searchedSelected = -1;
			} else {
				_searchedSelected = cur - _hashtagResults.size() - _filterResults.size() - _peerSearchResults.size() - _previewResults.size();
				_hashtagSelected = _filteredSelected = _peerSearchSelected = _previewSelected = -1;
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
		} else if (base::in_range(_previewSelected, 0, _previewResults.size())) {
			const auto from = previewOffset()
				+ _previewSelected * _st->height
				+ (_previewSelected ? 0 : -st::searchedBarHeight);
			const auto height = _st->height
				+ (_previewSelected ? 0 : st::searchedBarHeight);
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
		for (auto i = 0, c = int(_previewResults.size()); i != c; ++i) {
			if (isSearchResultActive(_previewResults[i].get(), entry)) {
				const auto from = previewOffset() + i * _st->height;
				scrollToItem(from, _st->height);
				return;
			}
		}
		for (auto i = 0, c = int(_searchResults.size()); i != c; ++i) {
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
		auto from = (yFrom - filteredOffset()) / _st->height;
		if (from < 0) from = 0;
		if (from < _filterResults.size()) {
			const auto to = std::min(
				((yTo - filteredOffset()) / _st->height) + 1,
				int(_filterResults.size()));
			for (; from < to; ++from) {
				_filterResults[from].key().entry()->chatListPreloadData();
			}
		}

		from = (yFrom - peerSearchOffset()) / st::dialogsRowHeight;
		if (from < 0) from = 0;
		if (from < _peerSearchResults.size()) {
			const auto to = std::min(
				((yTo - peerSearchOffset()) / st::dialogsRowHeight) + 1,
				int(_peerSearchResults.size()));
			for (; from < to; ++from) {
				_peerSearchResults[from]->peer->loadUserpic();
			}
		}

		from = (yFrom - previewOffset()) / _st->height;
		if (from < 0) from = 0;
		if (from < _previewResults.size()) {
			const auto to = std::min(
				((yTo - previewOffset()) / _st->height) + 1,
				int(_previewResults.size()));
			for (; from < to; ++from) {
				_previewResults[from]->item()->history()->peer->loadUserpic();
			}
		}

		from = (yFrom - searchedOffset()) / _st->height;
		if (from < 0) from = 0;
		if (from < _searchResults.size()) {
			const auto to = std::min(
				((yTo - searchedOffset()) / _st->height) + 1,
				int(_searchResults.size()));
			for (; from < to; ++from) {
				_searchResults[from]->item()->history()->peer->loadUserpic();
			}
		}
	}
}

bool InnerWidget::chooseCollapsedRow(Qt::KeyboardModifiers modifiers) {
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
	if (_controller->windowId().type != Window::SeparateType::Primary) {
		return;
	}
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
	refreshEmpty();
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

QImage *InnerWidget::cacheChatsFilterTag(
		const Data::ChatFilter &filter,
		uint8 more,
		bool active) {
	if (!filter.id() && !more) {
		return nullptr;
	}
	const auto key = SerializeFilterTagsKey(filter.id(), more, active);
	auto &entry = _chatsFilterTags[key];
	if (!entry.frame.isNull()) {
		if (!entry.context.loading) {
			return &entry.frame;
		}
		for (const auto &[k, emoji] : entry.context.emoji) {
			if (!emoji->ready()) {
				return &entry.frame; // Still waiting for emoji.
			}
		}
	}
	auto roundedText = TextWithEntities();
	auto colorIndex = -1;
	if (filter.id()) {
		roundedText = filter.title().text;
		roundedText.text = roundedText.text.toUpper();
		if (filter.colorIndex()) {
			colorIndex = *(filter.colorIndex());
		}
	} else if (more > 0) {
		roundedText.text = QChar('+') + QString::number(more);
		colorIndex = st::colorIndexBlue;
	}
	if (roundedText.empty() || colorIndex < 0) {
		return nullptr;
	}
	const auto color = Ui::EmptyUserpic::UserpicColor(colorIndex).color2;
	entry.context.color = color->c;
	entry.context.active = active;
	entry.context.textContext = Core::MarkedTextContext{
		.session = &session(),
		.customEmojiRepaint = [] {},
	};
	entry.frame = Ui::ChatsFilterTag(roundedText, entry.context);
	return &entry.frame;
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
		if ((_collapsedSelected >= 0)
			&& (_collapsedSelected < _collapsedRows.size())) {
			const auto &row = _collapsedRows[_collapsedSelected];
			Assert(row->folder != nullptr);
			return {
				.key = row->folder,
				.message = Data::UnreadMessagePosition,
			};
		} else if (_selected) {
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
		} else if (base::in_range(_previewSelected, 0, _previewResults.size())) {
			const auto result = _previewResults[_previewSelected].get();
			const auto topic = result->topic();
			const auto item = result->item();
			return {
				.key = (topic ? (Entry*)topic : (Entry*)item->history()),
				.message = item->position()
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

bool InnerWidget::isUserpicPress() const {
	return (_lastRowLocalMouseX >= 0)
		&& (_lastRowLocalMouseX < _st->nameLeft)
		&& (_collapsedSelected < 0
			|| _collapsedSelected >= _collapsedRows.size());
}

bool InnerWidget::isUserpicPressOnWide() const {
	return isUserpicPress() && (width() > _narrowWidth);
}

bool InnerWidget::chooseRow(
		Qt::KeyboardModifiers modifiers,
		MsgId pressedTopicRootId) {
	if (chooseHashtag()) {
		return true;
	} else if (_selectedMorePosts) {
		if (_searchHashOrCashtag != HashOrCashtag::None) {
			_changeSearchTabRequests.fire(ChatSearchTab::PublicPosts);
		}
		return true;
	} else if (_selectedChatTypeFilter) {
		_menu = base::make_unique_q<Ui::PopupMenu>(
			this,
			st::popupMenuWithIcons);
		for (const auto tab : {
			ChatTypeFilter::All,
			ChatTypeFilter::Private,
			ChatTypeFilter::Groups,
			ChatTypeFilter::Channels,
		}) {
			_menu->addAction(ChatTypeFilterLabel(tab), [=] {
				_changeSearchFilterRequests.fire_copy(tab);
			}, (tab == _searchState.filter)
				? &st::mediaPlayerMenuCheck
				: nullptr);
		}
		_menu->popup(QCursor::pos());
		return true;
	}
	const auto modifyChosenRow = [&](
			ChosenRow row,
			Qt::KeyboardModifiers modifiers) {
		row.newWindow = (modifiers & Qt::ControlModifier);
		row.userpicClick = isUserpicPressOnWide();
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
			&& !_childListShown.current().shown
			&& !_chatPreviewRow.key;
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
		} else if (_state == WidgetState::Default
			? !_shownList->empty()
			: !_filterResults.empty()) {
			request->check(Command::ChatNext) && request->handle([=] {
				return jumpToDialogRow(first);
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
			for (const auto &[command, index] : folders) {
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
		for (const auto &[command, index] : pinned) {
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

		(!_openedForum)
			&& request->check(Command::ArchiveChat)
			&& request->handle([=] {
				const auto thread = _selected ? _selected->thread() : nullptr;
				if (!thread) {
					return false;
				}
				const auto history = thread->owningHistory();
				const auto isArchived = history->folder()
					&& (history->folder()->id() == Data::Folder::kId);

				Window::ToggleHistoryArchived(
					_controller->uiShow(),
					history,
					!isArchived);
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

rpl::producer<UserId> InnerWidget::openBotMainAppRequests() const {
	return _openBotMainAppRequests.events();
}

} // namespace Dialogs
