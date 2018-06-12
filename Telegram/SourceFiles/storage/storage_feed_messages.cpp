/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_feed_messages.h"

namespace Storage {

auto FeedMessages::enforceLists(FeedId feedId)
-> std::map<FeedId, List>::iterator {
	auto result = _lists.find(feedId);
	if (result != _lists.end()) {
		return result;
	}
	result = _lists.emplace(feedId, List {}).first;
	result->second.sliceUpdated(
	) | rpl::map([=](Data::MessagesSliceUpdate &&update) {
		return FeedMessagesSliceUpdate(
			feedId,
			std::move(update));
	}) | rpl::start_to_stream(_sliceUpdated, _lifetime);
	return result;
}

void FeedMessages::add(FeedMessagesAddNew &&query) {
	auto feedId = query.feedId;
	auto feedIt = enforceLists(feedId);
	feedIt->second.addNew(query.messageId);
}

void FeedMessages::add(FeedMessagesAddSlice &&query) {
	auto feedIt = enforceLists(query.feedId);
	feedIt->second.addSlice(
		std::move(query.messageIds),
		query.noSkipRange,
		base::none);
}

void FeedMessages::remove(FeedMessagesRemoveOne &&query) {
	auto feedIt = _lists.find(query.feedId);
	if (feedIt != _lists.end()) {
		feedIt->second.removeOne(query.messageId);
		_oneRemoved.fire(std::move(query));
	}
}

void FeedMessages::remove(FeedMessagesRemoveAll &&query) {
	auto feedIt = _lists.find(query.feedId);
	if (feedIt != _lists.end()) {
		feedIt->second.removeAll(query.channelId);
		_allRemoved.fire(std::move(query));
	}
}

void FeedMessages::invalidate(FeedMessagesInvalidate &&query) {
	auto feedIt = _lists.find(query.feedId);
	if (feedIt != _lists.end()) {
		feedIt->second.invalidateBottom();
		feedIt->second.invalidate();
		_invalidated.fire(std::move(query));
	}
}

void FeedMessages::invalidate(FeedMessagesInvalidateBottom &&query) {
	auto feedIt = _lists.find(query.feedId);
	if (feedIt != _lists.end()) {
		feedIt->second.invalidateBottom();
		_bottomInvalidated.fire(std::move(query));
	}
}

rpl::producer<FeedMessagesResult> FeedMessages::query(
		FeedMessagesQuery &&query) const {
	auto feedIt = _lists.find(query.key.feedId);
	if (feedIt != _lists.end()) {
		return feedIt->second.query(Data::MessagesQuery(
			query.key.position,
			query.limitBefore,
			query.limitAfter));
	}
	return [](auto consumer) {
		consumer.put_done();
		return rpl::lifetime();
	};
}

rpl::producer<FeedMessagesSliceUpdate> FeedMessages::sliceUpdated() const {
	return _sliceUpdated.events();
}

rpl::producer<FeedMessagesRemoveOne> FeedMessages::oneRemoved() const {
	return _oneRemoved.events();
}

rpl::producer<FeedMessagesRemoveAll> FeedMessages::allRemoved() const {
	return _allRemoved.events();
}

rpl::producer<FeedMessagesInvalidate> FeedMessages::invalidated() const {
	return _invalidated.events();
}

rpl::producer<FeedMessagesInvalidateBottom> FeedMessages::bottomInvalidated() const {
	return _bottomInvalidated.events();
}

} // namespace Storage
