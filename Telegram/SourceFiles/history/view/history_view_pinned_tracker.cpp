/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_pinned_tracker.h"

#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_shared_media.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "history/history.h"
#include "history/history_item.h"

namespace HistoryView {
namespace{

constexpr auto kLoadedLimit = 5;
constexpr auto kChangeViewerLimit = 2;

} // namespace

PinnedTracker::PinnedTracker(not_null<History*> history)
: _history(history->migrateToOrMe())
, _migratedPeer(_history->peer->migrateFrom()) {
	using namespace rpl::mappers;
	const auto has = [&](PeerData *peer) -> rpl::producer<bool> {
		auto &changes = _history->session().changes();
		const auto flag = Data::PeerUpdate::Flag::PinnedMessages;
		if (!peer) {
			return rpl::single(false);
		}
		return changes.peerFlagsValue(peer, flag) | rpl::map([=] {
			return peer->hasPinnedMessages();
		});
	};
	rpl::combine(
		has(_history->peer),
		has(_migratedPeer),
		_1 || _2
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool has) {
		if (has) {
			refreshViewer();
		} else {
			clear();
		}
	}, _lifetime);
}

PinnedTracker::~PinnedTracker() = default;

rpl::producer<PinnedId> PinnedTracker::shownMessageId() const {
	return _current.value();
}

void PinnedTracker::reset() {
	_current.reset(currentMessageId());
}

PinnedId PinnedTracker::currentMessageId() const {
	return _current.current();
}

void PinnedTracker::refreshViewer() {
	if (_viewerAroundId == _aroundId) {
		return;
	}
	_dataLifetime.destroy();
	_viewerAroundId = _aroundId;
	SharedMediaMergedViewer(
		&_history->peer->session(),
		SharedMediaMergedKey(
			SparseIdsMergedSlice::Key(
				_history->peer->id,
				_migratedPeer ? _migratedPeer->id : 0,
				_viewerAroundId),
			Storage::SharedMediaType::Pinned),
		kLoadedLimit,
		kLoadedLimit
	) | rpl::start_with_next([=](const SparseIdsMergedSlice &result) {
		_slice.fullCount = result.fullCount();
		_slice.skippedBefore = result.skippedBefore();
		_slice.skippedAfter = result.skippedAfter();
		_slice.ids.clear();
		const auto count = result.size();
		_slice.ids.reserve(count);
		for (auto i = 0; i != count; ++i) {
			_slice.ids.push_back(result[i]);
		}
		refreshCurrentFromSlice();
		if (_slice.fullCount == 0) {
			_history->peer->setHasPinnedMessages(false);
			if (_migratedPeer) {
				_migratedPeer->setHasPinnedMessages(false);
			}
		}
	}, _dataLifetime);
}

void PinnedTracker::refreshCurrentFromSlice() {
	const auto proj1 = [](FullMsgId id) {
		return id.channel ? id.msg : (id.msg - ServerMaxMsgId);
	};
	const auto proj2 = [](FullMsgId id) {
		return id.msg;
	};
	const auto i = _migratedPeer
		? ranges::lower_bound(_slice.ids, _aroundId, ranges::less(), proj1)
		: ranges::lower_bound(_slice.ids, _aroundId, ranges::less(), proj2);
	const auto before = int(i - begin(_slice.ids));
	const auto after = int(end(_slice.ids) - i);
	const auto haveValidData = (before > 0 || _slice.skippedBefore == 0)
		&& (after > 0 || _slice.skippedAfter == 0);
	const auto nearEnd = !haveValidData
		|| (before <= kChangeViewerLimit && _slice.skippedBefore != 0)
		|| (after <= kChangeViewerLimit && _slice.skippedAfter != 0);
	if (haveValidData) {
		const auto count = std::max(
			_slice.fullCount.value_or(1),
			int(_slice.ids.size()));
		const auto index = _slice.skippedBefore.has_value()
			? (*_slice.skippedBefore + before)
			: _slice.skippedAfter.has_value()
			? (count - *_slice.skippedAfter - after)
			: 1;
		if (i != begin(_slice.ids)) {
			_current = PinnedId{ *(i - 1), index - 1, count };
		} else if (!_slice.ids.empty()) {
			_current = PinnedId{ _slice.ids.front(), 0, count };
		} else {
			_current = PinnedId();
		}
	}
	if (nearEnd) {
		refreshViewer();
	}
}

void PinnedTracker::clear() {
	_dataLifetime.destroy();
	_viewerAroundId = 0;
	_current = PinnedId();
}

void PinnedTracker::trackAround(MsgId messageId) {
	if (_aroundId == messageId) {
		return;
	}
	_aroundId = messageId;
	if (!_aroundId) {
		clear();
	} else {
		refreshCurrentFromSlice();
	}
}

} // namespace HistoryView
