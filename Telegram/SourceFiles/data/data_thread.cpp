/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_thread.h"

#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_unread_things.h"
#include "main/main_session.h"

namespace Data {

Thread::~Thread() = default;

not_null<Thread*> Thread::migrateToOrMe() const {
	const auto history = asHistory();
	return history ? history->migrateToOrMe() : const_cast<Thread*>(this);
}

MsgId Thread::topicRootId() const {
	if (const auto topic = asTopic()) {
		return topic->rootId();
	}
	return MsgId();
}

PeerId Thread::monoforumPeerId() const {
	if (const auto sublist = asSublist()) {
		return sublist->sublistPeer()->id;
	}
	return PeerId();
}

PeerData *Thread::maybeSublistPeer() const {
	if (const auto sublist = asSublist()) {
		return sublist->sublistPeer();
	}
	return nullptr;
}

not_null<PeerData*> Thread::peer() const {
	return owningHistory()->peer;
}

PeerNotifySettings &Thread::notify() {
	const auto topic = asTopic();
	return topic ? topic->notify() : peer()->notify();
}

const PeerNotifySettings &Thread::notify() const {
	return const_cast<Thread*>(this)->notify();
}

void Thread::setUnreadThingsKnown() {
	_flags |= Flag::UnreadThingsKnown;
}

HistoryUnreadThings::Proxy Thread::unreadMentions() {
	return {
		this,
		_unreadThings,
		HistoryUnreadThings::Type::Mentions,
		!!(_flags & Flag::UnreadThingsKnown),
	};
}

HistoryUnreadThings::ConstProxy Thread::unreadMentions() const {
	return {
		_unreadThings ? &_unreadThings->mentions : nullptr,
		!!(_flags & Flag::UnreadThingsKnown),
	};
}

HistoryUnreadThings::Proxy Thread::unreadReactions() {
	return {
		this,
		_unreadThings,
		HistoryUnreadThings::Type::Reactions,
		!!(_flags & Flag::UnreadThingsKnown),
	};
}

HistoryUnreadThings::ConstProxy Thread::unreadReactions() const {
	return {
		_unreadThings ? &_unreadThings->reactions : nullptr,
		!!(_flags & Flag::UnreadThingsKnown),
	};
}

bool Thread::canToggleUnread(bool nowUnread) const {
	if ((asTopic() || asForum()) && !nowUnread) {
		return false;
	} else if (asSublist() && owningHistory()->peer->isSelf()) {
		return false;
	} else if (asHistory() && peer()->amMonoforumAdmin()) {
		return false;
	}
	return true;
}

const base::flat_set<MsgId> &Thread::unreadMentionsIds() const {
	if (!_unreadThings) {
		static const auto Result = base::flat_set<MsgId>();
		return Result;
	}
	return _unreadThings->mentions.ids();
}

const base::flat_set<MsgId> &Thread::unreadReactionsIds() const {
	if (!_unreadThings) {
		static const auto Result = base::flat_set<MsgId>();
		return Result;
	}
	return _unreadThings->reactions.ids();
}

void Thread::clearNotifications() {
	_notifications.clear();
}

void Thread::clearIncomingNotifications() {
	if (!peer()->isSelf()) {
		const auto proj = [](ItemNotification notification) {
			return notification.item->out();
		};
		_notifications.erase(
			ranges::remove(_notifications, false, proj),
			end(_notifications));
	}
}

void Thread::removeNotification(not_null<HistoryItem*> item) {
	_notifications.erase(
		ranges::remove(_notifications, item, &ItemNotification::item),
		end(_notifications));
}

std::optional<ItemNotification> Thread::currentNotification() const {
	return empty(_notifications)
		? std::nullopt
		: std::make_optional(_notifications.front());
}

bool Thread::hasNotification() const {
	return !empty(_notifications);
}

void Thread::skipNotification() {
	if (!empty(_notifications)) {
		_notifications.pop_front();
	}
}

void Thread::pushNotification(ItemNotification notification) {
	_notifications.push_back(notification);
}

void Thread::popNotification(ItemNotification notification) {
	if (!empty(_notifications) && (_notifications.back() == notification)) {
		_notifications.pop_back();
	}
}

void Thread::setMuted(bool muted) {
	if (muted) {
		_flags |= Flag::Muted;
	} else {
		_flags &= ~Flag::Muted;
	}
}

void Thread::setUnreadMarkFlag(bool unread) {
	if (unread) {
		_flags |= Flag::UnreadMark;
	} else {
		_flags &= ~Flag::UnreadMark;
	}
}

[[nodiscard]] bool Thread::hasPinnedMessages() const {
	return (_flags & Flag::HasPinnedMessages);
}

void Thread::setHasPinnedMessages(bool has) {
	if (hasPinnedMessages() == has) {
		return;
	} else if (has) {
		_flags |= Flag::HasPinnedMessages;
	} else {
		_flags &= ~Flag::HasPinnedMessages;
	}
	session().changes().entryUpdated(
		this,
		EntryUpdate::Flag::HasPinnedMessages);
}

void Thread::saveMeAsActiveSubsectionThread() {
	if (const auto channel = owningHistory()->peer->asChannel()) {
		if (channel->useSubsectionTabs()) {
			if (const auto forum = channel->forum()) {
				forum->saveActiveSubsectionThread(this);
			} else if (const auto monoforum = channel->monoforum()) {
				monoforum->saveActiveSubsectionThread(this);
			}
		}
	}
}

} // namespace Data
