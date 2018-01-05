/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_entry.h"

class ChannelData;

namespace Data {

struct FeedPosition {
	FeedPosition() = default;
	FeedPosition(const MTPFeedPosition &position);
	FeedPosition(not_null<HistoryItem*> item);

	explicit operator bool() const {
		return (msgId != 0);
	}

	bool operator<(const FeedPosition &other) const;
	inline bool operator>(const FeedPosition &other) const {
		return other < *this;
	}
	inline bool operator<=(const FeedPosition &other) const {
		return !(other < *this);
	}
	inline bool operator>=(const FeedPosition &other) const {
		return !(*this < other);
	}
	inline bool operator==(const FeedPosition &other) const {
		return (date == other.date)
			&& (peerId == other.peerId)
			&& (msgId == other.msgId);
	}
	inline bool operator!=(const FeedPosition &other) const {
		return !(*this == other);
	}

	TimeId date = 0;
	PeerId peerId = 0;
	MsgId msgId = 0;

};

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
	void setUnreadPosition(const FeedPosition &position);

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
	void loadUserpic() override;
	void paintUserpic(
		Painter &p,
		int x,
		int y,
		int size) const override;

private:
	void recountLastMessage();
	bool justSetLastMessage(not_null<HistoryItem*> item);

	FeedId _id = 0;
	std::vector<not_null<History*>> _channels;

	HistoryItem *_lastMessage = nullptr;

	FeedPosition _unreadPosition;
	int _unreadCount = 0;
	int _unreadMutedCount = 0;
	bool _complete = false;

};

} // namespace Data
