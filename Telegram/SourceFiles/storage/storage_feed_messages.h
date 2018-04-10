/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/event_stream.h>
#include "storage/storage_facade.h"
#include "data/data_feed_messages.h"

namespace Storage {

struct FeedMessagesAddNew {
	FeedMessagesAddNew(FeedId feedId, Data::MessagePosition messageId)
	: feedId(feedId)
	, messageId(messageId) {
	}

	FeedId feedId = 0;
	Data::MessagePosition messageId;

};

struct FeedMessagesAddSlice {
	FeedMessagesAddSlice(
		FeedId feedId,
		std::vector<Data::MessagePosition> &&messageIds,
		Data::MessagesRange noSkipRange)
	: feedId(feedId)
	, messageIds(std::move(messageIds))
	, noSkipRange(noSkipRange) {
	}

	FeedId feedId = 0;
	std::vector<Data::MessagePosition> messageIds;
	Data::MessagesRange noSkipRange;

};

struct FeedMessagesRemoveOne {
	FeedMessagesRemoveOne(
		FeedId feedId,
		Data::MessagePosition messageId)
	: feedId(feedId)
	, messageId(messageId) {
	}

	FeedId feedId = 0;
	Data::MessagePosition messageId;

};

struct FeedMessagesRemoveAll {
	FeedMessagesRemoveAll(FeedId feedId, ChannelId channelId)
	: feedId(feedId)
	, channelId(channelId) {
	}

	FeedId feedId = 0;
	ChannelId channelId = 0;

};

struct FeedMessagesInvalidate {
	explicit FeedMessagesInvalidate(FeedId feedId)
	: feedId(feedId) {
	}

	FeedId feedId = 0;

};

struct FeedMessagesInvalidateBottom {
	explicit FeedMessagesInvalidateBottom(FeedId feedId)
	: feedId(feedId) {
	}

	FeedId feedId = 0;

};


struct FeedMessagesKey {
	FeedMessagesKey(
		FeedId feedId,
		Data::MessagePosition position)
	: feedId(feedId)
	, position(position) {
	}

	bool operator==(const FeedMessagesKey &other) const {
		return (feedId == other.feedId)
			&& (position == other.position);
	}
	bool operator!=(const FeedMessagesKey &other) const {
		return !(*this == other);
	}

	FeedId feedId = 0;
	Data::MessagePosition position;

};

struct FeedMessagesQuery {
	FeedMessagesQuery(
		FeedMessagesKey key,
		int limitBefore,
		int limitAfter)
	: key(key)
	, limitBefore(limitBefore)
	, limitAfter(limitAfter) {
	}

	FeedMessagesKey key;
	int limitBefore = 0;
	int limitAfter = 0;

};

using FeedMessagesResult = Data::MessagesResult;

struct FeedMessagesSliceUpdate {
	FeedMessagesSliceUpdate(
		FeedId feedId,
		Data::MessagesSliceUpdate &&data)
	: feedId(feedId)
	, data(std::move(data)) {
	}

	FeedId feedId = 0;
	Data::MessagesSliceUpdate data;

};

class FeedMessages {
public:
	void add(FeedMessagesAddNew &&query);
	void add(FeedMessagesAddSlice &&query);
	void remove(FeedMessagesRemoveOne &&query);
	void remove(FeedMessagesRemoveAll &&query);
	void invalidate(FeedMessagesInvalidate &&query);
	void invalidate(FeedMessagesInvalidateBottom &&query);

	rpl::producer<FeedMessagesResult> query(
		FeedMessagesQuery &&query) const;
	rpl::producer<FeedMessagesSliceUpdate> sliceUpdated() const;
	rpl::producer<FeedMessagesRemoveOne> oneRemoved() const;
	rpl::producer<FeedMessagesRemoveAll> allRemoved() const;
	rpl::producer<FeedMessagesInvalidate> invalidated() const;
	rpl::producer<FeedMessagesInvalidateBottom> bottomInvalidated() const;

private:
	using List = Data::MessagesList;

	std::map<FeedId, List>::iterator enforceLists(FeedId feedId);

	std::map<FeedId, List> _lists;

	rpl::event_stream<FeedMessagesSliceUpdate> _sliceUpdated;
	rpl::event_stream<FeedMessagesRemoveOne> _oneRemoved;
	rpl::event_stream<FeedMessagesRemoveAll> _allRemoved;
	rpl::event_stream<FeedMessagesInvalidate> _invalidated;
	rpl::event_stream<FeedMessagesInvalidateBottom> _bottomInvalidated;

	rpl::lifetime _lifetime;

};

} // namespace Storage
