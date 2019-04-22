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
#include "apiwrap.h"
#include "mainwidget.h"
#include "styles/style_dialogs.h" // st::dialogsArchiveUserpic

namespace Data {
namespace {

constexpr auto kLoadedChatsMinCount = 20;

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
, _name(lang(lng_archived_chats)) {
	indexNameParts();
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
	if (_chatsList.indexed()->size() == 1) {
		updateChatListSortPosition();
	}
	applyChatListMessage(history->chatListMessage());
}

void Folder::unregisterOne(not_null<History*> history) {
	if (_chatsList.empty()) {
		updateChatListExistence();
	}
	if (_chatListMessage && _chatListMessage->history() == history) {
		computeChatListMessage();
	}
}

void Folder::oneListMessageChanged(HistoryItem *from, HistoryItem *to) {
	if (!applyChatListMessage(to) && _chatListMessage == from) {
		computeChatListMessage();
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
		p.drawRoundedRect(x, y, size, size, size / 3., size / 3.);
	}
	st::dialogsArchiveUserpic.paintInCenter(p, { x, y, size, size });
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

void Folder::requestChatListMessage() {
	if (!chatListMessageKnown()) {
		session().api().requestDialogEntry(this);
	}
}

TimeId Folder::adjustedChatListTimeId() const {
	return _chatListMessage ? _chatListMessage->date() : chatListTimeId();
}

void Folder::applyDialog(const MTPDdialogFolder &data) {
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
	updateCloudUnread(data);
	if (_chatsList.indexed()->size() < kLoadedChatsMinCount) {
		session().api().requestDialogs(this);
	}
}

void Folder::updateCloudUnread(const MTPDdialogFolder &data) {
	const auto notifier = unreadStateChangeNotifier(!_chatsList.loaded());

	_cloudUnread.messagesCountMuted = data.vunread_muted_messages_count.v;
	_cloudUnread.messagesCount = _cloudUnread.messagesCountMuted
		+ data.vunread_unmuted_messages_count.v;
	_cloudUnread.chatsCountMuted = data.vunread_muted_peers_count.v;
	_cloudUnread.chatsCount = _cloudUnread.chatsCountMuted
		+ data.vunread_unmuted_peers_count.v;
}

Dialogs::UnreadState Folder::chatListUnreadState() const {
	const auto state = _chatsList.loaded()
		? _chatsList.unreadState()
		: _cloudUnread;
	auto result = Dialogs::UnreadState();
	result.messagesCount = state.messagesCount;
	result.messagesCountMuted = result.messagesCount.value_or(0);
	result.chatsCount = result.chatsCountMuted = state.chatsCount;
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
		const Dialogs::UnreadState &wasState,
		const Dialogs::UnreadState &nowState) {
	const auto updateCloudUnread = _cloudUnread.messagesCount.has_value()
		&& wasState.messagesCount.has_value();
	const auto notify = _chatsList.loaded() || updateCloudUnread;
	const auto notifier = unreadStateChangeNotifier(notify);

	_chatsList.unreadStateChanged(wasState, nowState);
	if (!_cloudUnread.messagesCount.has_value()
		|| !wasState.messagesCount.has_value()) {
		return;
	}
	Assert(nowState.messagesCount.has_value());

	*_cloudUnread.messagesCount += *nowState.messagesCount
		- *wasState.messagesCount;
	_cloudUnread.messagesCountMuted += nowState.messagesCountMuted
		- wasState.messagesCountMuted;
	_cloudUnread.chatsCount += nowState.chatsCount - wasState.chatsCount;
	_cloudUnread.chatsCountMuted += nowState.chatsCountMuted
		- wasState.chatsCountMuted;
}

void Folder::unreadEntryChanged(
		const Dialogs::UnreadState &state,
		bool added) {
	const auto updateCloudUnread = _cloudUnread.messagesCount.has_value()
		&& state.messagesCount.has_value();
	const auto notify = _chatsList.loaded() || updateCloudUnread;
	const auto notifier = unreadStateChangeNotifier(notify);

	_chatsList.unreadEntryChanged(state, added);
	if (!_cloudUnread.messagesCount.has_value()
		|| !state.messagesCount.has_value()) {
		return;
	}
	const auto delta = (added ? 1 : -1);
	*_cloudUnread.messagesCount += delta * *state.messagesCount;
	_cloudUnread.messagesCountMuted += delta * state.messagesCountMuted;
	_cloudUnread.chatsCount += delta * state.chatsCount;
	_cloudUnread.chatsCountMuted += delta * state.chatsCountMuted;
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
	return session().settings().countUnreadMessages()
		? chatListUnreadState().messagesCount.value_or(0)
		: chatListUnreadState().chatsCount;
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
