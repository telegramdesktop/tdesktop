/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "storage/storage_shared_media.h"

#include "base/task_queue.h"

namespace Storage {

SharedMedia::List::Slice::Slice(
		base::flat_set<MsgId> &&messages,
		MsgRange range)
	: messages(std::move(messages))
	, range(range) {
}

template <typename Range>
void SharedMedia::List::Slice::merge(
		const Range &moreMessages,
		MsgRange moreNoSkipRange) {
	Expects(moreNoSkipRange.from <= range.till);
	Expects(range.from <= moreNoSkipRange.till);

	messages.merge(std::begin(moreMessages), std::end(moreMessages));
	range = {
		qMin(range.from, moreNoSkipRange.from),
		qMax(range.till, moreNoSkipRange.till)
	};
}

template <typename Range>
int SharedMedia::List::uniteAndAdd(
		SliceUpdate &update,
		base::flat_set<Slice>::iterator uniteFrom,
		base::flat_set<Slice>::iterator uniteTill,
		const Range &messages,
		MsgRange noSkipRange) {
	auto uniteFromIndex = uniteFrom - _slices.begin();
	auto was = uniteFrom->messages.size();
	_slices.modify(uniteFrom, [&](Slice &slice) {
		slice.merge(messages, noSkipRange);
	});
	auto firstToErase = uniteFrom + 1;
	if (firstToErase != uniteTill) {
		for (auto it = firstToErase; it != uniteTill; ++it) {
			_slices.modify(uniteFrom, [&](Slice &slice) {
				slice.merge(it->messages, it->range);
			});
		}
		_slices.erase(firstToErase, uniteTill);
		uniteFrom = _slices.begin() + uniteFromIndex;
	}
	update.messages = &uniteFrom->messages;
	update.range = uniteFrom->range;
	return uniteFrom->messages.size() - was;
}

template <typename Range>
int SharedMedia::List::addRangeItemsAndCountNew(
		SliceUpdate &update,
		const Range &messages,
		MsgRange noSkipRange) {
	Expects((noSkipRange.from < noSkipRange.till)
		|| (noSkipRange.from == noSkipRange.till && messages.begin() == messages.end()));
	if (noSkipRange.from == noSkipRange.till) {
		return 0;
	}

	auto uniteFrom = base::lower_bound(
		_slices,
		noSkipRange.from,
		[](const Slice &slice, MsgId from) { return slice.range.till < from; });
	auto uniteTill = base::upper_bound(
		_slices,
		noSkipRange.till,
		[](MsgId till, const Slice &slice) { return till < slice.range.from; });
	if (uniteFrom < uniteTill) {
		return uniteAndAdd(update, uniteFrom, uniteTill, messages, noSkipRange);
	}

	auto sliceMessages = base::flat_set<MsgId> {
		std::begin(messages),
		std::end(messages) };
	auto slice = _slices.emplace(
		std::move(sliceMessages),
		noSkipRange);
	update.messages = &slice->messages;
	update.range = slice->range;
	return slice->messages.size();
}

template <typename Range>
void SharedMedia::List::addRange(
		const Range &messages,
		MsgRange noSkipRange,
		base::optional<int> count,
		bool incrementCount) {
	Expects(!count || !incrementCount);

	auto wasCount = _count;
	auto update = SliceUpdate();
	auto result = addRangeItemsAndCountNew(update, messages, noSkipRange);
	if (count) {
		_count = count;
	} else if (incrementCount && _count && result > 0) {
		*_count += result;
	}
	if (_slices.size() == 1) {
		if (_slices.front().range == MsgRange { 0, ServerMaxMsgId }) {
			_count = _slices.front().messages.size();
		}
	}
	update.count = _count;
	_sliceUpdated.fire(std::move(update));
}

void SharedMedia::List::addNew(MsgId messageId) {
	auto range = { messageId };
	addRange(range, { messageId, ServerMaxMsgId }, base::none, true);
}

void SharedMedia::List::addExisting(
		MsgId messageId,
		MsgRange noSkipRange) {
	auto range = { messageId };
	addRange(range, noSkipRange, base::none);
}

void SharedMedia::List::addSlice(
		std::vector<MsgId> &&messageIds,
		MsgRange noSkipRange,
		base::optional<int> count) {
	addRange(messageIds, noSkipRange, count);
}

void SharedMedia::List::removeOne(MsgId messageId) {
	auto slice = base::lower_bound(
		_slices,
		messageId,
		[](const Slice &slice, MsgId from) { return slice.range.till < from; });
	if (slice != _slices.end() && slice->range.from <= messageId) {
		_slices.modify(slice, [messageId](Slice &slice) {
			return slice.messages.remove(messageId);
		});
	}
	if (_count) {
		--*_count;
	}
}

void SharedMedia::List::removeAll() {
	_slices.clear();
	_slices.emplace(base::flat_set<MsgId>{}, MsgRange { 0, ServerMaxMsgId });
	_count = 0;
}

rpl::producer<SharedMediaResult> SharedMedia::List::query(
		SharedMediaQuery &&query) const {
	return [this, query = std::move(query)](auto consumer) {
		auto slice = query.key.messageId
			? base::lower_bound(
				_slices,
				query.key.messageId,
				[](const Slice &slice, MsgId id) {
					return slice.range.till < id;
				})
			: _slices.end();
		if (slice != _slices.end()
			&& slice->range.from <= query.key.messageId) {
			consumer.put_next(queryFromSlice(query, *slice));
		} else if (_count) {
			auto result = SharedMediaResult {};
			result.count = _count;
			consumer.put_next(std::move(result));
		}
		consumer.put_done();
		return rpl::lifetime();
	};
}

SharedMediaResult SharedMedia::List::queryFromSlice(
		const SharedMediaQuery &query,
		const Slice &slice) const {
	auto result = SharedMediaResult {};
	auto position = base::lower_bound(slice.messages, query.key.messageId);
	auto haveBefore = int(position - slice.messages.begin());
	auto haveEqualOrAfter = int(slice.messages.end() - position);
	auto before = qMin(haveBefore, query.limitBefore);
	auto equalOrAfter = qMin(haveEqualOrAfter, query.limitAfter + 1);
	auto ids = std::vector<MsgId>(position - before, position + equalOrAfter);
	result.messageIds.merge(ids.begin(), ids.end());
	if (slice.range.from == 0) {
		result.skippedBefore = haveBefore - before;
	}
	if (slice.range.till == ServerMaxMsgId) {
		result.skippedAfter = haveEqualOrAfter - equalOrAfter;
	}
	if (_count) {
		result.count = _count;
		if (!result.skippedBefore && result.skippedAfter) {
			result.skippedBefore = *result.count
				- *result.skippedAfter
				- int(result.messageIds.size());
		} else if (!result.skippedAfter && result.skippedBefore) {
			result.skippedAfter = *result.count
				- *result.skippedBefore
				- int(result.messageIds.size());
		}
	}
	return result;
}

std::map<PeerId, SharedMedia::Lists>::iterator
		SharedMedia::enforceLists(PeerId peer) {
	auto result = _lists.find(peer);
	if (result != _lists.end()) {
		return result;
	}
	result = _lists.emplace(peer, Lists {}).first;
	for (auto index = 0; index != kSharedMediaTypeCount; ++index) {
		auto &list = result->second[index];
		auto type = static_cast<SharedMediaType>(index);

		list.sliceUpdated()
			| rpl::start([this, peer, type](const SliceUpdate &update) {
				_sliceUpdated.fire(SharedMediaSliceUpdate(
					peer,
					type,
					update.messages,
					update.range,
					update.count));
			}, _lifetime);
	}
	return result;
}

void SharedMedia::add(SharedMediaAddNew &&query) {
	auto peer = query.peerId;
	auto peerIt = enforceLists(peer);
	for (auto index = 0; index != kSharedMediaTypeCount; ++index) {
		auto type = static_cast<SharedMediaType>(index);
		if (query.types.test(type)) {
			peerIt->second[index].addNew(query.messageId);
		}
	}
}

void SharedMedia::add(SharedMediaAddExisting &&query) {
	auto peerIt = enforceLists(query.peerId);
	for (auto index = 0; index != kSharedMediaTypeCount; ++index) {
		auto type = static_cast<SharedMediaType>(index);
		if (query.types.test(type)) {
			peerIt->second[index].addExisting(query.messageId, query.noSkipRange);
		}
	}
}

void SharedMedia::add(SharedMediaAddSlice &&query) {
	Expects(IsValidSharedMediaType(query.type));
	auto peerIt = enforceLists(query.peerId);
	auto index = static_cast<int>(query.type);
	peerIt->second[index].addSlice(std::move(query.messageIds), query.noSkipRange, query.count);
}

void SharedMedia::remove(SharedMediaRemoveOne &&query) {
	auto peerIt = _lists.find(query.peerId);
	if (peerIt != _lists.end()) {
		for (auto index = 0; index != kSharedMediaTypeCount; ++index) {
			auto type = static_cast<SharedMediaType>(index);
			if (query.types.test(type)) {
				peerIt->second[index].removeOne(query.messageId);
				_oneRemoved.fire(std::move(query));
			}
		}
	}
}

void SharedMedia::remove(SharedMediaRemoveAll &&query) {
	auto peerIt = _lists.find(query.peerId);
	if (peerIt != _lists.end()) {
		for (auto index = 0; index != kSharedMediaTypeCount; ++index) {
			peerIt->second[index].removeAll();
		}
		_allRemoved.fire(std::move(query));
	}
}

rpl::producer<SharedMediaResult> SharedMedia::query(SharedMediaQuery &&query) const {
	Expects(IsValidSharedMediaType(query.key.type));
	auto peerIt = _lists.find(query.key.peerId);
	if (peerIt != _lists.end()) {
		auto index = static_cast<int>(query.key.type);
		return peerIt->second[index].query(std::move(query));
	}
	return [](auto consumer) {
		consumer.put_done();
		return rpl::lifetime();
	};
}

} // namespace Storage
