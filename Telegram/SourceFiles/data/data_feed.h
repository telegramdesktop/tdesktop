/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_entry.h"
#include "data/data_messages.h"

class ChannelData;

namespace Data {

MessagePosition FeedPositionFromMTP(const MTPFeedPosition &position);

class Feed : public Dialogs::Entry {
public:
	Feed(FeedId id);

	FeedId id() const {
		return _id;
	}
	void registerOne(not_null<ChannelData*> channel);
	void unregisterOne(not_null<ChannelData*> channel);

	void updateLastMessage(not_null<HistoryItem*> item);
	void messageRemoved(not_null<HistoryItem*> item);
	void historyCleared(not_null<History*> history);

	void setUnreadCounts(int unreadCount, int unreadMutedCount);
	void setUnreadPosition(const MessagePosition &position) {
		_unreadPosition = position;
	}
	MessagePosition unreadPosition() const {
		return _unreadPosition.current();
	}
	rpl::producer<MessagePosition> unreadPositionChanges() const {
		return _unreadPosition.changes();
	}

	bool toImportant() const override {
		return false; // TODO feeds workmode
	}
	int chatListUnreadCount() const override {
		return _unreadCount;
	}
	bool chatListMutedBadge() const override {
		return _unreadCount <= _unreadMutedCount;
	}
	HistoryItem *chatsListItem() const override {
		return _lastMessage;
	}
	const QString &chatsListName() const override {
		return _name;
	}
	const base::flat_set<QString> &chatsListNameWords() const override {
		return _nameWords;
	}
	const base::flat_set<QChar> &chatsListFirstLetters() const override {
		return _nameFirstLetters;
	}

	void loadUserpic() override;
	void paintUserpic(
		Painter &p,
		int x,
		int y,
		int size) const override;

private:
	void indexNameParts();
	void recountLastMessage();
	bool justSetLastMessage(not_null<HistoryItem*> item);

	FeedId _id = 0;
	std::vector<not_null<History*>> _channels;

	QString _name;
	base::flat_set<QString> _nameWords;
	base::flat_set<QChar> _nameFirstLetters;
	HistoryItem *_lastMessage = nullptr;

	rpl::variable<MessagePosition> _unreadPosition;
	int _unreadCount = 0;
	int _unreadMutedCount = 0;
	bool _complete = false;

};

} // namespace Data
