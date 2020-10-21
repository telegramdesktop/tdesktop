/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_pinned_messages.h"

#include "data/data_peer.h"
#include "main/main_session.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "data/data_shared_media.h"
#include "data/data_sparse_ids.h"

namespace Data {
namespace {

constexpr auto PinnedType = Storage::SharedMediaType::Pinned;

using Storage::SharedMediaQuery;
using Storage::SharedMediaKey;
using Storage::SharedMediaResult;

} // namespace

PinnedMessages::PinnedMessages(not_null<PeerData*> peer)
: _peer(peer)
, _storage(_peer->session().storage()) {
}

bool PinnedMessages::empty() const {
	return _storage.empty(SharedMediaKey(_peer->id, PinnedType, 0));
}

MsgId PinnedMessages::topId() const {
	const auto slice = _storage.snapshot(
		SharedMediaQuery(
			SharedMediaKey(_peer->id, PinnedType, ServerMaxMsgId),
			1,
			1));
	return slice.messageIds.empty() ? 0 : slice.messageIds.back();
}

rpl::producer<PinnedAroundId> PinnedMessages::viewer(
		MsgId aroundId,
		int limit) const {
	return SharedMediaViewer(
		&_peer->session(),
		SharedMediaKey(_peer->id, PinnedType, aroundId),
		limit,
		limit
	) | rpl::map([](const SparseIdsSlice &result) {
		auto data = PinnedAroundId();
		data.fullCount = result.fullCount();
		data.skippedBefore = result.skippedBefore();
		data.skippedAfter = result.skippedAfter();
		const auto count = result.size();
		data.ids.reserve(count);
		for (auto i = 0; i != count; ++i) {
			data.ids.push_back(result[i]);
		}
		return data;
	});
}

void PinnedMessages::setTopId(MsgId messageId) {
	while (true) {
		auto top = topId();
		if (top > messageId) {
			_storage.remove(Storage::SharedMediaRemoveOne(
				_peer->id,
				PinnedType,
				top));
		} else if (top == messageId) {
			return;
		} else {
			break;
		}
	}
	_storage.add(Storage::SharedMediaAddNew(
		_peer->id,
		PinnedType,
		messageId));
}

} // namespace Data
