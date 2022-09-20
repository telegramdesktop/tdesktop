/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_forum_topic.h"

#include "data/data_channel.h"
#include "data/data_forum.h"
#include "data/data_session.h"
#include "dialogs/dialogs_main_list.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "history/history.h"
#include "history/history_item.h"

namespace Data {

ForumTopic::ForumTopic(not_null<History*> forum, MsgId rootId)
: Entry(&forum->owner(), Type::ForumTopic)
, _forum(forum)
, _list(forum->peer->asChannel()->forum()->topicsList())
, _rootId(rootId) {
}

not_null<History*> ForumTopic::forum() const {
	return _forum;
}

MsgId ForumTopic::rootId() const {
	return _rootId;
}

void ForumTopic::applyTopic(const MTPForumTopic &topic) {
	Expects(_rootId == topic.data().vid().v);

	const auto &data = topic.data();
	const auto title = qs(data.vtitle());
	if (_title != title) {
		_title = title;
		++_titleVersion;
		indexTitleParts();
		updateChatListEntry();
	}

	const auto pinned = _list->pinned();
	if (data.is_pinned()) {
		pinned->addPinned(Dialogs::Key(this));
	} else {
		pinned->setPinned(Dialogs::Key(this), false);
	}

	applyTopicFields(
		data.vunread_count().v,
		data.vread_inbox_max_id().v,
		data.vread_outbox_max_id().v);
	applyTopicTopMessage(data.vtop_message().v);
	//setUnreadMark(data.is_unread_mark());
}

void ForumTopic::indexTitleParts() {
	_titleWords.clear();
	_titleFirstLetters.clear();
	auto toIndexList = QStringList();
	auto appendToIndex = [&](const QString &value) {
		if (!value.isEmpty()) {
			toIndexList.push_back(TextUtilities::RemoveAccents(value));
		}
	};

	appendToIndex(_title);
	const auto appendTranslit = !toIndexList.isEmpty()
		&& cRussianLetters().match(toIndexList.front()).hasMatch();
	if (appendTranslit) {
		appendToIndex(translitRusEng(toIndexList.front()));
	}
	auto toIndex = toIndexList.join(' ');
	toIndex += ' ' + rusKeyboardLayoutSwitch(toIndex);

	const auto namesList = TextUtilities::PrepareSearchWords(toIndex);
	for (const auto &name : namesList) {
		_titleWords.insert(name);
		_titleFirstLetters.insert(name[0]);
	}
}

int ForumTopic::chatListNameVersion() const {
	return _titleVersion;
}

void ForumTopic::applyTopicFields(
		int unreadCount,
		MsgId maxInboxRead,
		MsgId maxOutboxRead) {
	if (maxInboxRead + 1 >= _inboxReadBefore.value_or(1)) {
		setUnreadCount(unreadCount);
		setInboxReadTill(maxInboxRead);
	}
	setOutboxReadTill(maxOutboxRead);
}

void ForumTopic::applyTopicTopMessage(MsgId topMessageId) {
	if (topMessageId) {
		const auto itemId = FullMsgId(_forum->peer->id, topMessageId);
		if (const auto item = owner().message(itemId)) {
			setLastServerMessage(item);
		} else {
			setLastServerMessage(nullptr);
		}
	} else {
		setLastServerMessage(nullptr);
	}
}

void ForumTopic::setLastServerMessage(HistoryItem *item) {
	_lastServerMessage = item;
	if (_lastMessage
		&& *_lastMessage
		&& !(*_lastMessage)->isRegular()
		&& (!item || (*_lastMessage)->date() > item->date())) {
		return;
	}
	setLastMessage(item);
}

void ForumTopic::setLastMessage(HistoryItem *item) {
	if (_lastMessage && *_lastMessage == item) {
		return;
	}
	_lastMessage = item;
	if (!item || item->isRegular()) {
		_lastServerMessage = item;
	}
	setChatListMessage(item);
}

void ForumTopic::setChatListMessage(HistoryItem *item) {
	if (_chatListMessage && *_chatListMessage == item) {
		return;
	}
	const auto was = _chatListMessage.value_or(nullptr);
	if (item) {
		if (item->isSponsored()) {
			return;
		}
		if (_chatListMessage
			&& *_chatListMessage
			&& !(*_chatListMessage)->isRegular()
			&& (*_chatListMessage)->date() > item->date()) {
			return;
		}
		_chatListMessage = item;
		setChatListTimeId(item->date());

#if 0 // #TODO forum
		// If we have a single message from a group, request the full album.
		if (hasOrphanMediaGroupPart()
			&& !item->toPreview({
				.hideSender = true,
				.hideCaption = true }).images.empty()) {
			owner().histories().requestGroupAround(item);
		}
#endif
	} else if (!_chatListMessage || *_chatListMessage) {
		_chatListMessage = nullptr;
		updateChatListEntry();
	}
}

void ForumTopic::setInboxReadTill(MsgId upTo) {
	if (_inboxReadBefore) {
		accumulate_max(*_inboxReadBefore, upTo + 1);
	} else {
		_inboxReadBefore = upTo + 1;
	}
}

void ForumTopic::setOutboxReadTill(MsgId upTo) {
	if (_outboxReadBefore) {
		accumulate_max(*_outboxReadBefore, upTo + 1);
	} else {
		_outboxReadBefore = upTo + 1;
	}
}

void ForumTopic::loadUserpic() {
}

void ForumTopic::paintUserpic(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const {
	// #TODO forum
}

void ForumTopic::requestChatListMessage() {
	if (!chatListMessageKnown()) {
		// #TODO forum
	}
}

TimeId ForumTopic::adjustedChatListTimeId() const {
	const auto result = chatListTimeId();
#if 0 // #TODO forum
	if (const auto draft = cloudDraft()) {
		if (!Data::draftIsNull(draft) && !session().supportMode()) {
			return std::max(result, draft->date);
		}
	}
#endif
	return result;
}

int ForumTopic::fixedOnTopIndex() const {
	return kArchiveFixOnTopIndex;
}

bool ForumTopic::shouldBeInChatList() const {
	return isPinnedDialog(FilterId())
		|| !lastMessageKnown()
		|| (lastMessage() != nullptr);
}

HistoryItem *ForumTopic::lastMessage() const {
	return _lastMessage.value_or(nullptr);
}

bool ForumTopic::lastMessageKnown() const {
	return _lastMessage.has_value();
}

HistoryItem *ForumTopic::lastServerMessage() const {
	return _lastServerMessage.value_or(nullptr);
}

bool ForumTopic::lastServerMessageKnown() const {
	return _lastServerMessage.has_value();
}

int ForumTopic::unreadCount() const {
	return _unreadCount ? *_unreadCount : 0;
}

int ForumTopic::unreadCountForBadge() const {
	const auto result = unreadCount();
	return (!result && unreadMark()) ? 1 : result;
}

bool ForumTopic::unreadCountKnown() const {
	return _unreadCount.has_value();
}

void ForumTopic::setUnreadCount(int newUnreadCount) {
	if (_unreadCount == newUnreadCount) {
		return;
	}
	const auto wasForBadge = (unreadCountForBadge() > 0);
	const auto notifier = unreadStateChangeNotifier(true);
	_unreadCount = newUnreadCount;
}

void ForumTopic::setUnreadMark(bool unread) {
	if (_unreadMark == unread) {
		return;
	}
	const auto noUnreadMessages = !unreadCount();
	const auto refresher = gsl::finally([&] {
		if (inChatList() && noUnreadMessages) {
			updateChatListEntry();
		}
	});
	const auto notifier = unreadStateChangeNotifier(noUnreadMessages);
	_unreadMark = unread;
}

bool ForumTopic::unreadMark() const {
	return _unreadMark;
}

int ForumTopic::chatListUnreadCount() const {
	const auto state = chatListUnreadState();
	return state.marks
		+ (Core::App().settings().countUnreadMessages()
			? state.messages
			: state.chats);
}

Dialogs::UnreadState ForumTopic::chatListUnreadState() const {
	auto result = Dialogs::UnreadState();
	const auto count = _unreadCount.value_or(0);
	const auto mark = !count && _unreadMark;
	const auto muted = _forum->mute();
	result.messages = count;
	result.messagesMuted = muted ? count : 0;
	result.chats = count ? 1 : 0;
	result.chatsMuted = (count && muted) ? 1 : 0;
	result.marks = mark ? 1 : 0;
	result.marksMuted = (mark && muted) ? 1 : 0;
	result.known = _unreadCount.has_value();
	return result;
}

bool ForumTopic::chatListUnreadMark() const {
	return false;
}

bool ForumTopic::chatListMutedBadge() const {
	return true;
}

HistoryItem *ForumTopic::chatListMessage() const {
	return _lastMessage.value_or(nullptr);
}

bool ForumTopic::chatListMessageKnown() const {
	return _lastMessage.has_value();
}

const QString &ForumTopic::chatListName() const {
	return _title;
}

const base::flat_set<QString> &ForumTopic::chatListNameWords() const {
	return _titleWords;
}

const base::flat_set<QChar> &ForumTopic::chatListFirstLetters() const {
	return _titleFirstLetters;
}

const QString &ForumTopic::chatListNameSortKey() const {
	static const auto empty = QString();
	return empty;
}

} // namespace Data
