/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_feed_messages.h"

#include "apiwrap.h"
#include "auth_session.h"
#include "data/data_session.h"
#include "storage/storage_feed_messages.h"

namespace Data {

rpl::producer<MessagesSlice> FeedMessagesViewer(
		Storage::FeedMessagesKey key,
		int limitBefore,
		int limitAfter) {
	Expects(IsServerMsgId(key.position.fullId.msg)
		|| (key.position.fullId.msg == 0));

	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto builder = lifetime.make_state<MessagesSliceBuilder>(
			key.position,
			limitBefore,
			limitAfter);
		const auto feed = Auth().data().feed(key.feedId);
		using AroundData = MessagesSliceBuilder::AroundData;
		const auto requestMediaAround = [=](const AroundData &data) {
			if (data.aroundId || !key.position) {
				//Auth().api().requestFeedMessages( // #feed
				//	feed,
				//	data.aroundId,
				//	data.direction);
			}
		};
		builder->insufficientAround(
		) | rpl::start_with_next(requestMediaAround, lifetime);

		const auto pushNextSnapshot = [=] {
			consumer.put_next(builder->snapshot());
		};

		using SliceUpdate = Storage::FeedMessagesSliceUpdate;
		Auth().storage().feedMessagesSliceUpdated(
		) | rpl::filter([=](const SliceUpdate &update) {
			return (update.feedId == key.feedId);
		}) | rpl::filter([=](const SliceUpdate &update) {
			return builder->applyUpdate(update.data);
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		using OneRemoved = Storage::FeedMessagesRemoveOne;
		Auth().storage().feedMessagesOneRemoved(
		) | rpl::filter([=](const OneRemoved &update) {
			return (update.feedId == key.feedId);
		}) | rpl::filter([=](const OneRemoved &update) {
			return builder->removeOne(update.messageId);
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		using AllRemoved = Storage::FeedMessagesRemoveAll;
		Auth().storage().feedMessagesAllRemoved(
		) | rpl::filter([=](const AllRemoved &update) {
			return (update.feedId == key.feedId);
		}) | rpl::filter([=](const AllRemoved &update) {
			return builder->removeFromChannel(update.channelId);
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		using Invalidate = Storage::FeedMessagesInvalidate;
		Auth().storage().feedMessagesInvalidated(
		) | rpl::filter([=](const Invalidate &update) {
			return (update.feedId == key.feedId);
		}) | rpl::filter([=] {
			return builder->invalidated();
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		using InvalidateBottom = Storage::FeedMessagesInvalidateBottom;
		Auth().storage().feedMessagesBottomInvalidated(
		) | rpl::filter([=](const InvalidateBottom &update) {
			return (update.feedId == key.feedId);
		}) | rpl::filter([=] {
			return builder->bottomInvalidated();
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		using Result = Storage::FeedMessagesResult;
		Auth().storage().query(Storage::FeedMessagesQuery(
			key,
			limitBefore,
			limitAfter
		)) | rpl::filter([=](const Result &result) {
			return builder->applyInitial(result);
		}) | rpl::start_with_next_done(
			pushNextSnapshot,
			[=] { builder->checkInsufficient(); },
			lifetime);

		return lifetime;
	};
}

} // namespace Data
