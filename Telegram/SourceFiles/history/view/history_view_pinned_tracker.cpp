/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_pinned_tracker.h"

#include "data/data_changes.h"
#include "data/data_peer.h"
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

PinnedTracker::PinnedTracker(not_null<History*> history) : _history(history) {
	_history->session().changes().peerFlagsValue(
		_history->peer,
		Data::PeerUpdate::Flag::PinnedMessages
	) | rpl::map([=] {
		return _history->peer->hasPinnedMessages();
	}) | rpl::distinct_until_changed(
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
	SharedMediaViewer(
		&_history->peer->session(),
		Storage::SharedMediaKey(
			_history->peer->id,
			Storage::SharedMediaType::Pinned,
			_viewerAroundId),
		kLoadedLimit,
		kLoadedLimit
	) | rpl::start_with_next([=](const SparseIdsSlice &result) {
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
		}
	}, _dataLifetime);
}

void PinnedTracker::refreshCurrentFromSlice() {
	const auto i = ranges::lower_bound(_slice.ids, _aroundId);
	const auto empty = _slice.ids.empty();
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
