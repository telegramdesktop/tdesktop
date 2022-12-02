/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_pinned_list.h"

#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_entry.h"
#include "history/history.h"
#include "data/data_session.h"
#include "data/data_forum.h"

namespace Dialogs {

PinnedList::PinnedList(FilterId filterId, int limit)
: _filterId(filterId)
, _limit(limit) {
	Expects(limit > 0);
}

void PinnedList::setLimit(int limit) {
	Expects(limit > 0);

	if (_limit == limit) {
		return;
	}
	_limit = limit;
	applyLimit(_limit);
}

void PinnedList::addPinned(Key key) {
	Expects(key.entry()->folderKnown());

	addPinnedGetPosition(key);
}

int PinnedList::addPinnedGetPosition(Key key) {
	const auto already = ranges::find(_data, key);
	if (already != end(_data)) {
		return already - begin(_data);
	}
	applyLimit(_limit - 1);
	const auto position = int(_data.size());
	_data.push_back(key);
	key.entry()->cachePinnedIndex(_filterId, position + 1);
	return position;
}

void PinnedList::setPinned(Key key, bool pinned) {
	Expects(key.entry()->folderKnown() || _filterId != 0);

	if (pinned) {
		const int position = addPinnedGetPosition(key);
		if (position) {
			const auto begin = _data.begin();
			std::rotate(begin, begin + position, begin + position + 1);
			for (auto i = 0; i != position + 1; ++i) {
				_data[i].entry()->cachePinnedIndex(_filterId, i + 1);
			}
		}
	} else if (const auto it = ranges::find(_data, key); it != end(_data)) {
		const auto index = int(it - begin(_data));
		_data.erase(it);
		key.entry()->cachePinnedIndex(_filterId, 0);
		for (auto i = index, count = int(size(_data)); i != count; ++i) {
			_data[i].entry()->cachePinnedIndex(_filterId, i + 1);
		}
	}
}

void PinnedList::applyLimit(int limit) {
	Expects(limit >= 0);

	while (_data.size() > limit) {
		setPinned(_data.back(), false);
	}
}

void PinnedList::clear() {
	applyLimit(0);
}

void PinnedList::applyList(
		not_null<Data::Session*> owner,
		const QVector<MTPDialogPeer> &list) {
	clear();
	for (const auto &peer : list) {
		peer.match([&](const MTPDdialogPeer &data) {
			if (const auto peerId = peerFromMTP(data.vpeer())) {
				addPinned(owner->history(peerId));
			}
		}, [&](const MTPDdialogPeerFolder &data) {
			addPinned(owner->folder(data.vfolder_id().v));
		});
	}
}

void PinnedList::applyList(
		not_null<Data::Forum*> forum,
		const QVector<MTPint> &list) {
	clear();
	for (const auto &topicId : list) {
		addPinned(forum->topicFor(topicId.v));
	}
}

void PinnedList::applyList(const std::vector<not_null<History*>> &list) {
	Expects(_filterId != 0);

	const auto old = base::take(_data);

	const auto count = int(list.size());
	_data.reserve(count);
	for (auto i = 0; i != count; ++i) {
		const auto history = list[i];
		_data.emplace_back(history);
		history->cachePinnedIndex(_filterId, i + 1);
	}

	for (const auto &key : old) {
		const auto history = key.history();
		if (!history || !ranges::contains(_data, history, &Key::history)) {
			key.entry()->cachePinnedIndex(_filterId, 0);
		}
	}
}

void PinnedList::reorder(Key key1, Key key2) {
	const auto index1 = ranges::find(_data, key1) - begin(_data);
	const auto index2 = ranges::find(_data, key2) - begin(_data);
	Assert(index1 >= 0 && index1 < _data.size());
	Assert(index2 >= 0 && index2 < _data.size());
	Assert(index1 != index2);
	std::swap(_data[index1], _data[index2]);
	key1.entry()->cachePinnedIndex(_filterId, index2 + 1);
	key2.entry()->cachePinnedIndex(_filterId, index1 + 1);
}

} // namespace Dialogs
