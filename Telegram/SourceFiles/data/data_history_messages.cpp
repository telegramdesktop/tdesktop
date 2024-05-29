/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_history_messages.h"

#include "apiwrap.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_sparse_ids.h"
#include "history/history.h"
#include "main/main_session.h"

namespace Data {

void HistoryMessages::addNew(MsgId messageId) {
	_chat.addNew(messageId);
}

void HistoryMessages::addExisting(MsgId messageId, MsgRange noSkipRange) {
	_chat.addExisting(messageId, noSkipRange);
}

void HistoryMessages::addSlice(
		std::vector<MsgId> &&messageIds,
		MsgRange noSkipRange,
		std::optional<int> count) {
	_chat.addSlice(std::move(messageIds), noSkipRange, count);
}

void HistoryMessages::removeOne(MsgId messageId) {
	_chat.removeOne(messageId);
	_oneRemoved.fire_copy(messageId);
}

void HistoryMessages::removeAll() {
	_chat.removeAll();
	_allRemoved.fire({});
}

void HistoryMessages::invalidateBottom() {
	_chat.invalidateBottom();
	_bottomInvalidated.fire({});
}

Storage::SparseIdsListResult HistoryMessages::snapshot(
		const Storage::SparseIdsListQuery &query) const {
	return _chat.snapshot(query);
}

auto HistoryMessages::sliceUpdated() const
-> rpl::producer<Storage::SparseIdsSliceUpdate> {
	return _chat.sliceUpdated();
}

rpl::producer<MsgId> HistoryMessages::oneRemoved() const {
	return _oneRemoved.events();
}

rpl::producer<> HistoryMessages::allRemoved() const {
	return _allRemoved.events();
}

rpl::producer<> HistoryMessages::bottomInvalidated() const {
	return _bottomInvalidated.events();
}

rpl::producer<SparseIdsSlice> HistoryViewer(
		not_null<History*> history,
		MsgId aroundId,
		int limitBefore,
		int limitAfter) {
	Expects(IsServerMsgId(aroundId) || (aroundId == 0));
	Expects((aroundId != 0) || (limitBefore == 0 && limitAfter == 0));

	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		const auto messages = &history->messages();

		auto builder = lifetime.make_state<SparseIdsSliceBuilder>(
			aroundId,
			limitBefore,
			limitAfter);
		using RequestAroundInfo = SparseIdsSliceBuilder::AroundData;
		builder->insufficientAround(
		) | rpl::start_with_next([=](const RequestAroundInfo &info) {
			if (!info.aroundId) {
				// Ignore messages-count-only requests, because we perform
				// them with non-zero limit of messages and end up adding
				// a broken slice with several last messages from the chat
				// with a non-skip range starting at zero.
				return;
			}
			history->session().api().requestHistory(
				history,
				info.aroundId,
				info.direction);
		}, lifetime);

		auto pushNextSnapshot = [=] {
			consumer.put_next(builder->snapshot());
		};

		using SliceUpdate = Storage::SparseIdsSliceUpdate;
		messages->sliceUpdated(
		) | rpl::filter([=](const SliceUpdate &update) {
			return builder->applyUpdate(update);
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		messages->oneRemoved(
		) | rpl::filter([=](MsgId messageId) {
			return builder->removeOne(messageId);
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		messages->allRemoved(
		) | rpl::filter([=] {
			return builder->removeAll();
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		messages->bottomInvalidated(
		) | rpl::filter([=] {
			return builder->invalidateBottom();
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		const auto snapshot = messages->snapshot({
			aroundId,
			limitBefore,
			limitAfter,
		});
		if (snapshot.count || !snapshot.messageIds.empty()) {
			if (builder->applyInitial(snapshot)) {
				pushNextSnapshot();
			}
		}
		builder->checkInsufficient();

		return lifetime;
	};
}

rpl::producer<SparseIdsMergedSlice> HistoryMergedViewer(
		not_null<History*> history,
		/*Universal*/MsgId universalAroundId,
		int limitBefore,
		int limitAfter) {
	const auto migrateFrom = history->peer->migrateFrom();
	auto createSimpleViewer = [=](
			PeerId peerId,
			MsgId topicRootId,
			SparseIdsSlice::Key simpleKey,
			int limitBefore,
			int limitAfter) {
		const auto chosen = (history->peer->id == peerId)
			? history
			: history->owner().history(peerId);
		return HistoryViewer(chosen, simpleKey, limitBefore, limitAfter);
	};
	const auto peerId = history->peer->id;
	const auto topicRootId = MsgId();
	const auto migratedPeerId = migrateFrom ? migrateFrom->id : PeerId(0);
	using Key = SparseIdsMergedSlice::Key;
	return SparseIdsMergedSlice::CreateViewer(
		Key(peerId, topicRootId, migratedPeerId, universalAroundId),
		limitBefore,
		limitAfter,
		std::move(createSimpleViewer));
}

rpl::producer<MessagesSlice> HistoryMessagesViewer(
		not_null<History*> history,
		MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto computeUnreadAroundId = [&] {
		if (const auto migrated = history->migrateFrom()) {
			if (const auto around = migrated->loadAroundId()) {
				return MsgId(around - ServerMaxMsgId);
			}
		}
		if (const auto around = history->loadAroundId()) {
			return around;
		}
		return MsgId(ServerMaxMsgId - 1);
	};
	const auto messageId = (aroundId.fullId.msg == ShowAtUnreadMsgId)
		? computeUnreadAroundId()
		: (aroundId.fullId.msg == ShowAtTheEndMsgId)
		? (ServerMaxMsgId - 1)
		: (aroundId.fullId.peer == history->peer->id)
		? aroundId.fullId.msg
		: (aroundId.fullId.msg - ServerMaxMsgId);
	return HistoryMergedViewer(
		history,
		messageId,
		limitBefore,
		limitAfter
	) | rpl::map([=](SparseIdsMergedSlice &&slice) {
		auto result = Data::MessagesSlice();
		result.fullCount = slice.fullCount();
		result.skippedAfter = slice.skippedAfter();
		result.skippedBefore = slice.skippedBefore();
		const auto count = slice.size();
		result.ids.reserve(count);
		if (const auto msgId = slice.nearest(messageId)) {
			result.nearestToAround = *msgId;
		}
		for (auto i = 0; i != count; ++i) {
			result.ids.push_back(slice[i]);
		}
		return result;
	});
}

} // namespace Data