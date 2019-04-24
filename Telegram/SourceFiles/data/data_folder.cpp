/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_folder.h"

#include "data/data_session.h"
#include "data/data_channel.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "storage/storage_facade.h"
#include "core/application.h"
//#include "storage/storage_feed_messages.h" // #feed
#include "auth_session.h"
#include "observer_peer.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "styles/style_dialogs.h"

namespace Data {
namespace {

constexpr auto kLoadedChatsMinCount = 20;
constexpr auto kShowChatNamesCount = 2;

rpl::producer<int> PinnedDialogsInFolderMaxValue() {
	return rpl::single(
		rpl::empty_value()
	) | rpl::then(
		Core::App().configUpdates()
	) | rpl::map([=] {
		return Global::PinnedDialogsInFolderMax();
	});
}

} // namespace

// #feed
//MessagePosition FeedPositionFromMTP(const MTPFeedPosition &position) {
//	Expects(position.type() == mtpc_feedPosition);
//
//	const auto &data = position.c_feedPosition();
//	return MessagePosition(data.vdate.v, FullMsgId(
//		peerToChannel(peerFromMTP(data.vpeer)),
//		data.vid.v));
//}

Folder::Folder(not_null<Data::Session*> owner, FolderId id)
: Entry(owner, this)
, _id(id)
, _chatsList(PinnedDialogsInFolderMaxValue())
, _name(lang(lng_archived_name)) {
	indexNameParts();

	Notify::PeerUpdateViewer(
		Notify::PeerUpdate::Flag::NameChanged
	) | rpl::start_with_next([=](const Notify::PeerUpdate &update) {
		for (const auto history : _unreadHistoriesLast) {
			if (history->peer == update.peer) {
				++_chatListViewVersion;
				updateChatListEntry();
				return;
			}
		}
	}, _lifetime);
}

FolderId Folder::id() const {
	return _id;
}

void Folder::indexNameParts() {
	_nameWords.clear();
	_nameFirstLetters.clear();
	auto toIndexList = QStringList();
	auto appendToIndex = [&](const QString &value) {
		if (!value.isEmpty()) {
			toIndexList.push_back(TextUtilities::RemoveAccents(value));
		}
	};

	appendToIndex(_name);
	const auto appendTranslit = !toIndexList.isEmpty()
		&& cRussianLetters().match(toIndexList.front()).hasMatch();
	if (appendTranslit) {
		appendToIndex(translitRusEng(toIndexList.front()));
	}
	auto toIndex = toIndexList.join(' ');
	toIndex += ' ' + rusKeyboardLayoutSwitch(toIndex);

	const auto namesList = TextUtilities::PrepareSearchWords(toIndex);
	for (const auto &name : namesList) {
		_nameWords.insert(name);
		_nameFirstLetters.insert(name[0]);
	}
}

void Folder::registerOne(not_null<History*> history) {
	++_chatListViewVersion;
	if (_chatsList.indexed()->size() == 1) {
		updateChatListSortPosition();
		if (!_cloudUnread.known) {
			session().api().requestDialogEntry(this);
		}
	} else {
		updateChatListEntry();
	}
	applyChatListMessage(history->chatListMessage());
}

void Folder::unregisterOne(not_null<History*> history) {
	if (_chatsList.empty()) {
		updateChatListExistence();
	} else {
		++_chatListViewVersion;
		updateChatListEntry();
	}
	if (_chatListMessage && _chatListMessage->history() == history) {
		computeChatListMessage();
	}
}

void Folder::oneListMessageChanged(HistoryItem *from, HistoryItem *to) {
	if (!applyChatListMessage(to) && _chatListMessage == from) {
		computeChatListMessage();
	}
	if (from || to) {
		const auto history = from ? from->history() : to->history();
		if (!history->chatListUnreadState().empty()) {
			reorderUnreadHistories();
		}
	}
}

bool Folder::applyChatListMessage(HistoryItem *item) {
	if (!item) {
		return false;
	} else if (_chatListMessage
		&& _chatListMessage->date() >= item->date()) {
		return false;
	}
	_chatListMessage = item;
	updateChatListEntry();
	return true;
}

void Folder::computeChatListMessage() {
	auto &&items = ranges::view::all(
		*_chatsList.indexed()
	) | ranges::view::filter([](not_null<Dialogs::Row*> row) {
		return row->entry()->chatListMessage() != nullptr;
	});
	const auto chatListDate = [](not_null<Dialogs::Row*> row) {
		return row->entry()->chatListMessage()->date();
	};
	const auto top = ranges::max_element(
		items,
		ranges::less(),
		chatListDate);
	if (top == items.end()) {
		_chatListMessage = nullptr;
	} else {
		_chatListMessage = (*top)->entry()->chatListMessage();
	}
	updateChatListEntry();
}

void Folder::addUnreadHistory(not_null<History*> history) {
	const auto i = ranges::find(_unreadHistories, history);
	if (i == end(_unreadHistories)) {
		_unreadHistories.push_back(history);
		reorderUnreadHistories();
	}
}

void Folder::removeUnreadHistory(not_null<History*> history) {
	const auto i = ranges::find(_unreadHistories, history);
	if (i != end(_unreadHistories)) {
		_unreadHistories.erase(i);
		reorderUnreadHistories();
	}
}

void Folder::reorderUnreadHistories() {
	// We want first kShowChatNamesCount histories, by last message date.
	const auto predicate = [](not_null<History*> a, not_null<History*> b) {
		const auto aItem = a->chatListMessage();
		const auto bItem = b->chatListMessage();
		const auto aDate = aItem ? aItem->date() : TimeId(0);
		const auto bDate = bItem ? bItem->date() : TimeId(0);
		return aDate > bDate;
	};
	if (size(_unreadHistories) <= kShowChatNamesCount) {
		ranges::sort(_unreadHistories, predicate);
		if (!ranges::equal(_unreadHistories, _unreadHistoriesLast)) {
			_unreadHistoriesLast = _unreadHistories;
		}
	} else {
		const auto till = begin(_unreadHistories) + kShowChatNamesCount - 1;
		ranges::nth_element(_unreadHistories, till, predicate);
		if constexpr (kShowChatNamesCount > 2) {
			ranges::sort(begin(_unreadHistories), till, predicate);
		}
		auto &&head = ranges::view::all(
			_unreadHistories
		) | ranges::view::take_exactly(
			kShowChatNamesCount
		);
		if (!ranges::equal(head, _unreadHistoriesLast)) {
			_unreadHistoriesLast = head | ranges::to_vector;
		}
	}
	++_chatListViewVersion;
	updateChatListEntry();
}

not_null<Dialogs::MainList*> Folder::chatsList() {
	return &_chatsList;
}

void Folder::loadUserpic() {
	//constexpr auto kPaintUserpicsCount = 4; // #feed
	//auto load = kPaintUserpicsCount;
	//for (const auto history : _histories) {
	//	history->peer->loadUserpic();
	//	if (!--load) {
	//		break;
	//	}
	//}
}

void Folder::paintUserpic(
		Painter &p,
		int x,
		int y,
		int size) const {
	p.setPen(Qt::NoPen);
	p.setBrush(st::historyPeerArchiveUserpicBg);
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(x, y, size, size);
	}
	if (size == st::dialogsPhotoSize) {
		st::dialogsArchiveUserpic.paintInCenter(p, { x, y, size, size });
	} else {
		p.save();
		const auto ratio = size / float64(st::dialogsPhotoSize);
		p.translate(x + size / 2., y + size / 2.);
		p.scale(ratio, ratio);
		const auto skip = st::dialogsPhotoSize;
		st::dialogsArchiveUserpic.paintInCenter(
			p,
			{ -skip, -skip, 2 * skip, 2 * skip });
		p.restore();
	}
	//const auto small = (size - st::lineWidth) / 2; // #feed
	//const auto delta = size - small;
	//auto index = 0;
	//for (const auto history : _histories) {
	//	history->peer->paintUserpic(p, x, y, small);
	//	switch (++index) {
	//	case 1:
	//	case 3: x += delta; break;
	//	case 2: x -= delta; y += delta; break;
	//	case 4: return;
	//	}
	//}
}

bool Folder::chatsListLoaded() const {
	return _chatsList.loaded();
}

void Folder::setChatsListLoaded(bool loaded) {
	if (_chatsList.loaded() == loaded) {
		return;
	}
	const auto notifier = unreadStateChangeNotifier(true);
	_chatsList.setLoaded(loaded);
}

void Folder::setCloudChatsListSize(int size) {
	_cloudChatsListSize = size;
	updateChatListEntry();
}

int Folder::chatsListSize() const {
	return std::max(
		_chatsList.indexed()->size(),
		_chatsList.loaded() ? 0 : _cloudChatsListSize);
}

int Folder::unreadHistoriesCount() const {
	return _unreadHistories.size();
}

const std::vector<not_null<History*>> &Folder::lastUnreadHistories() const {
	return _unreadHistoriesLast;
}

uint32 Folder::chatListViewVersion() const {
	return _chatListViewVersion;
}

void Folder::requestChatListMessage() {
	if (!chatListMessageKnown()) {
		session().api().requestDialogEntry(this);
	}
}

TimeId Folder::adjustedChatListTimeId() const {
	return _chatListMessage ? _chatListMessage->date() : chatListTimeId();
}

void Folder::applyDialog(const MTPDdialogFolder &data) {
	updateCloudUnread(data);
	if (const auto peerId = peerFromMTP(data.vpeer)) {
		const auto history = owner().history(peerId);
		const auto fullId = FullMsgId(
			peerToChannel(peerId),
			data.vtop_message.v);
		history->setFolder(this, App::histItemById(fullId));
	} else {
		_chatsList.clear();
		updateChatListExistence();
	}
	if (_chatsList.indexed()->size() < kLoadedChatsMinCount) {
		session().api().requestDialogs(this);
	}
}

void Folder::updateCloudUnread(const MTPDdialogFolder &data) {
	const auto notifier = unreadStateChangeNotifier(!_chatsList.loaded());

	_cloudUnread.messagesMuted = data.vunread_muted_messages_count.v;
	_cloudUnread.messages = _cloudUnread.messagesMuted
		+ data.vunread_unmuted_messages_count.v;
	_cloudUnread.chatsMuted = data.vunread_muted_peers_count.v;
	_cloudUnread.chats = _cloudUnread.chatsMuted
		+ data.vunread_unmuted_peers_count.v;
	_cloudUnread.known = true;
}

Dialogs::UnreadState Folder::chatListUnreadState() const {
	const auto localUnread = _chatsList.unreadState();
	auto result = _chatsList.loaded() ? localUnread : _cloudUnread;
	result.messagesMuted = result.messages;
	result.chatsMuted = result.chats;

	// We don't know the real value of marked chats counts.
	result.marksMuted = result.marks = localUnread.marks;

	return result;
}

void Folder::applyPinnedUpdate(const MTPDupdateDialogPinned &data) {
	const auto folderId = data.has_folder_id() ? data.vfolder_id.v : 0;
	if (folderId != 0) {
		LOG(("API Error: Nested folders detected."));
	}
	owner().setChatPinned(this, data.is_pinned());
}

void Folder::unreadStateChanged(
		const Dialogs::Key &key,
		const Dialogs::UnreadState &wasState,
		const Dialogs::UnreadState &nowState) {
	if (const auto history = key.history()) {
		if (!wasState.empty() && nowState.empty()) {
			removeUnreadHistory(history);
		} else if (wasState.empty() && !nowState.empty()) {
			addUnreadHistory(history);
		}
	}

	const auto updateCloudUnread = _cloudUnread.known && wasState.known;
	const auto notify = _chatsList.loaded() || updateCloudUnread;
	const auto notifier = unreadStateChangeNotifier(notify);

	_chatsList.unreadStateChanged(wasState, nowState);
	if (updateCloudUnread) {
		Assert(nowState.known);
		_cloudUnread += nowState - wasState;

		// We don't know the real value of marked chats counts.
		_cloudUnread.marks = _cloudUnread.marksMuted = 0;
	}
}

void Folder::unreadEntryChanged(
		const Dialogs::Key &key,
		const Dialogs::UnreadState &state,
		bool added) {
	if (const auto history = key.history()) {
		if (!state.empty()) {
			if (added) {
				addUnreadHistory(history);
			} else {
				removeUnreadHistory(history);
			}
		}
	}

	const auto updateCloudUnread = _cloudUnread.known && state.known;
	const auto notify = _chatsList.loaded() || updateCloudUnread;
	const auto notifier = unreadStateChangeNotifier(notify);

	_chatsList.unreadEntryChanged(state, added);
	if (updateCloudUnread) {
		if (added) {
			_cloudUnread += state;
		} else {
			_cloudUnread -= state;
		}

		// We don't know the real value of marked chats counts.
		_cloudUnread.marks = _cloudUnread.marksMuted = 0;
	}
}

// #feed
//MessagePosition Folder::unreadPosition() const {
//	return _unreadPosition.current();
//}
//
//rpl::producer<MessagePosition> Folder::unreadPositionChanges() const {
//	return _unreadPosition.changes();
//}

bool Folder::toImportant() const {
	return false;
}

int Folder::fixedOnTopIndex() const {
	return kArchiveFixOnTopIndex;
}

bool Folder::shouldBeInChatList() const {
	return !_chatsList.empty();
}

int Folder::chatListUnreadCount() const {
	const auto state = chatListUnreadState();
	return state.marks
		+ (session().settings().countUnreadMessages()
			? state.messages
			: state.chats);
}

bool Folder::chatListUnreadMark() const {
	return false; // #feed unread mark
}

bool Folder::chatListMutedBadge() const {
	return true;
}

HistoryItem *Folder::chatListMessage() const {
	return _chatListMessage;
}

bool Folder::chatListMessageKnown() const {
	return true;
}

const QString &Folder::chatListName() const {
	return _name;
}

const base::flat_set<QString> &Folder::chatListNameWords() const {
	return _nameWords;
}

const base::flat_set<QChar> &Folder::chatListFirstLetters() const {
	return _nameFirstLetters;
}

} // namespace Data
