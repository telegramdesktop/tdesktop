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
	explicit FeedPosition(const MTPFeedPosition &position);
	explicit FeedPosition(not_null<HistoryItem*> item);
	FeedPosition(TimeId date, FullMsgId msgId) : date(date), msgId(msgId) {
	}

	explicit operator bool() const {
		return (msgId.msg != 0);
	}

	inline bool operator<(const FeedPosition &other) const {
		if (date < other.date) {
			return true;
		} else if (other.date < date) {
			return false;
		}
		return (msgId < other.msgId);
	}
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
			&& (msgId == other.msgId);
	}
	inline bool operator!=(const FeedPosition &other) const {
		return !(*this == other);
	}

	TimeId date = 0;
	FullMsgId msgId;

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
