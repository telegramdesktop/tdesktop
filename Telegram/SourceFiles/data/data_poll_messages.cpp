/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_poll_messages.h"

#include "data/data_messages.h"
#include "data/data_shared_media.h"
#include "data/data_sparse_ids.h"
#include "data/data_chat.h"
#include "history/history.h"
#include "main/main_session.h"
#include "storage/storage_shared_media.h"

namespace Data {

rpl::producer<MessagesSlice> PollMessagesViewer(
		not_null<Main::Session*> session,
		not_null<History*> history,
		MsgId topicRootId,
		PeerId monoforumPeerId,
		MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto peerId = history->peer->id;
	const auto migrateFrom = history->peer->migrateFrom();
	const auto migratedPeerId = migrateFrom ? migrateFrom->id : PeerId(0);
	const auto messageId = ((aroundId.fullId.msg == ShowAtTheEndMsgId)
			|| (aroundId == MaxMessagePosition))
		? (ServerMaxMsgId - 1)
		: (aroundId.fullId.peer == peerId)
		? aroundId.fullId.msg
		: (aroundId.fullId.msg
			? (aroundId.fullId.msg - ServerMaxMsgId)
			: (ServerMaxMsgId - 1));
	const auto key = SharedMediaMergedKey(
		SparseIdsMergedSlice::Key(
			peerId,
			topicRootId,
			monoforumPeerId,
			migratedPeerId,
			messageId),
		Storage::SharedMediaType::Poll);
	return SharedMediaMergedViewer(
		session,
		key,
		limitBefore,
		limitAfter
	) | rpl::map([=](SparseIdsMergedSlice &&slice) {
		auto result = MessagesSlice();
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
