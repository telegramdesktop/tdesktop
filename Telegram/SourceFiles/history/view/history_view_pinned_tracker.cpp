/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_pinned_tracker.h"

#include "data/data_changes.h"
#include "data/data_pinned_messages.h"
#include "data/data_peer.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "history/history.h"
#include "history/history_item.h"

namespace HistoryView {
namespace{

constexpr auto kLoadedLimit = 4;

} // namespace

PinnedTracker::PinnedTracker(not_null<History*> history) : _history(history) {
	_history->session().changes().peerFlagsValue(
		_history->peer,
		Data::PeerUpdate::Flag::PinnedMessage
	) | rpl::start_with_next([=] {
		refreshData();
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

void PinnedTracker::refreshData() {
	const auto now = _history->peer->currentPinnedMessages();
	if (!now) {
		_dataLifetime.destroy();
		_current = PinnedId();
	} else if (_data.get() != now) {
		_dataLifetime.destroy();
		_data = now;
		if (_aroundId) {
			setupViewer(now);
		}
	}
}

void PinnedTracker::trackAround(MsgId messageId) {
	if (_aroundId == messageId) {
		return;
	}
	_dataLifetime.destroy();
	_aroundId = messageId;
	if (!_aroundId) {
		_current = PinnedId();
	} else if (const auto now = _data.get()) {
		setupViewer(now);
	}
}

void PinnedTracker::setupViewer(not_null<Data::PinnedMessages*> data) {
	data->viewer(
		_aroundId,
		kLoadedLimit + 2
	) | rpl::start_with_next([=](const Data::PinnedAroundId &snapshot) {
		const auto i = ranges::lower_bound(snapshot.ids, _aroundId);
		const auto empty = snapshot.ids.empty();
		const auto before = (i - begin(snapshot.ids));
		const auto after = (end(snapshot.ids) - i);
		if (snapshot.ids.empty()) {
			_current = PinnedId();
			return;
		}
		const auto count = std::max(
			snapshot.fullCount.value_or(1),
			int(snapshot.ids.size()));
		const auto index = snapshot.skippedBefore.has_value()
			? (*snapshot.skippedBefore + before)
			: snapshot.skippedAfter.has_value()
			? (count - *snapshot.skippedAfter - after)
			: 1;
		if (i != begin(snapshot.ids)) {
			_current = PinnedId{ *(i - 1), index - 1, count };
		} else if (snapshot.skippedBefore == 0) {
			_current = PinnedId{ snapshot.ids.front(), 0, count };
		}
	}, _dataLifetime);
}

} // namespace HistoryView
