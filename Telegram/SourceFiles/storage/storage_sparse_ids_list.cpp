/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_sparse_ids_list.h"

namespace Storage {

SparseIdsList::Slice::Slice(
	base::flat_set<MsgId> &&messages,
	MsgRange range)
: messages(std::move(messages))
, range(range) {
}

template <typename Range>
void SparseIdsList::Slice::merge(
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
SparseIdsList::AddResult SparseIdsList::uniteAndAdd(
		SparseIdsSliceUpdate &update,
		base::flat_set<Slice>::iterator uniteFrom,
		base::flat_set<Slice>::iterator uniteTill,
		const Range &messages,
		MsgRange noSkipRange) {
	const auto uniteFromIndex = uniteFrom - _slices.begin();
	const auto was = int(uniteFrom->messages.size());
	_slices.modify(uniteFrom, [&](Slice &slice) {
		slice.merge(messages, noSkipRange);
	});
	const auto firstToErase = uniteFrom + 1;
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
	return { int(uniteFrom->messages.size()) - was };
}

template <typename Range>
SparseIdsList::AddResult SparseIdsList::addRangeItemsAndCountNew(
		SparseIdsSliceUpdate &update,
		const Range &messages,
		MsgRange noSkipRange) {
	Expects(noSkipRange.from <= noSkipRange.till);

	if (noSkipRange.from == noSkipRange.till
		&& std::begin(messages) == std::end(messages)) {
		return { 0 };
	}
	auto uniteFrom = ranges::lower_bound(
		_slices,
		noSkipRange.from,
		std::less<>(),
		[](const Slice &slice) { return slice.range.till; });
	auto uniteTill = ranges::upper_bound(
		_slices,
		noSkipRange.till,
		std::less<>(),
		[](const Slice &slice) { return slice.range.from; });
	if (uniteFrom < uniteTill) {
		return uniteAndAdd(update, uniteFrom, uniteTill, messages, noSkipRange);
	}

	auto sliceMessages = base::flat_set<MsgId> {
		std::begin(messages),
		std::end(messages) };
	auto slice = _slices.emplace(
		std::move(sliceMessages),
		noSkipRange
	).first;
	update.messages = &slice->messages;
	update.range = slice->range;
	return { int(slice->messages.size()) };
}

template <typename Range>
void SparseIdsList::addRange(
		const Range &messages,
		MsgRange noSkipRange,
		std::optional<int> count,
		bool incrementCount) {
	Expects(!count || !incrementCount);

	auto update = SparseIdsSliceUpdate();
	const auto result = addRangeItemsAndCountNew(
		update,
		messages,
		noSkipRange);
	if (count) {
		_count = count;
	} else if (incrementCount && _count && result.added > 0) {
		*_count += result.added;
	}
	if (_slices.size() == 1) {
		if (_count && _slices.front().messages.size() >= *_count) {
			_slices.modify(_slices.begin(), [&](Slice &slice) {
				slice.range = { 0, ServerMaxMsgId };
			});
		}
		if (_slices.front().range == MsgRange{ 0, ServerMaxMsgId }) {
			_count = _slices.front().messages.size();
		}
	}
	if (_count && update.messages) {
		accumulate_max(*_count, int(update.messages->size()));
	}
	update.count = _count;
	_sliceUpdated.fire(std::move(update));
}

void SparseIdsList::addNew(MsgId messageId) {
	auto range = { messageId };
	addRange(range, { messageId, ServerMaxMsgId }, std::nullopt, true);
}

void SparseIdsList::addExisting(
		MsgId messageId,
		MsgRange noSkipRange) {
	auto range = { messageId };
	addRange(range, noSkipRange, std::nullopt);
}

void SparseIdsList::addSlice(
		std::vector<MsgId> &&messageIds,
		MsgRange noSkipRange,
		std::optional<int> count) {
	addRange(messageIds, noSkipRange, count);
}

void SparseIdsList::removeOne(MsgId messageId) {
	auto slice = ranges::lower_bound(
		_slices,
		messageId,
		std::less<>(),
		[](const Slice &slice) { return slice.range.till; });
	if (slice != _slices.end() && slice->range.from <= messageId) {
		_slices.modify(slice, [messageId](Slice &slice) {
			return slice.messages.remove(messageId);
		});
	}
	if (_count) {
		--*_count;
	}
}

void SparseIdsList::removeAll() {
	_slices.clear();
	_slices.emplace(base::flat_set<MsgId>{}, MsgRange { 0, ServerMaxMsgId });
	_count = 0;
}

void SparseIdsList::invalidateBottom() {
	if (!_slices.empty()) {
		const auto &last = _slices.back();
		if (last.range.till == ServerMaxMsgId) {
			_slices.modify(_slices.end() - 1, [](Slice &slice) {
				slice.range.till = slice.messages.empty()
					? slice.range.from
					: slice.messages.back();
			});
		}
	}
	_count = std::nullopt;
}

rpl::producer<SparseIdsListResult> SparseIdsList::query(
		SparseIdsListQuery &&query) const {
	return [this, query = std::move(query)](auto consumer) {
		auto slice = query.aroundId
			? ranges::lower_bound(
				_slices,
				query.aroundId,
				std::less<>(),
				[](const Slice &slice) { return slice.range.till; })
			: _slices.end();
		if (slice != _slices.end()
			&& slice->range.from <= query.aroundId) {
			consumer.put_next(queryFromSlice(query, *slice));
		} else if (_count) {
			auto result = SparseIdsListResult {};
			result.count = _count;
			consumer.put_next(std::move(result));
		}
		consumer.put_done();
		return rpl::lifetime();
	};
}

SparseIdsListResult SparseIdsList::snapshot(
		const SparseIdsListQuery &query) const {
	auto slice = query.aroundId
		? ranges::lower_bound(
			_slices,
			query.aroundId,
			std::less<>(),
			[](const Slice &slice) { return slice.range.till; })
		: _slices.end();
	if (slice != _slices.end()
		&& slice->range.from <= query.aroundId) {
		return queryFromSlice(query, *slice);
	} else if (_count) {
		auto result = SparseIdsListResult{};
		result.count = _count;
		return result;
	}
	return {};
}

bool SparseIdsList::empty() const {
	for (const auto &slice : _slices) {
		if (!slice.messages.empty()) {
			return false;
		}
	}
	return true;
}

rpl::producer<SparseIdsSliceUpdate> SparseIdsList::sliceUpdated() const {
	return _sliceUpdated.events();
}

SparseIdsListResult SparseIdsList::queryFromSlice(
		const SparseIdsListQuery &query,
		const Slice &slice) const {
	auto result = SparseIdsListResult {};
	auto position = ranges::lower_bound(slice.messages, query.aroundId);
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

} // namespace Storage
