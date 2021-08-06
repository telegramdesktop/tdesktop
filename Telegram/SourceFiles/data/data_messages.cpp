/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_messages.h"

namespace Data {

MessagesList::Slice::Slice(
	base::flat_set<MessagePosition> &&messages,
	MessagesRange range)
: messages(std::move(messages))
, range(range) {
}

template <typename Range>
void MessagesList::Slice::merge(
		const Range &moreMessages,
		MessagesRange moreNoSkipRange) {
	Expects(moreNoSkipRange.from <= range.till);
	Expects(range.from <= moreNoSkipRange.till);

	messages.merge(std::begin(moreMessages), std::end(moreMessages));
	range = {
		qMin(range.from, moreNoSkipRange.from),
		qMax(range.till, moreNoSkipRange.till)
	};
}

template <typename Range>
int MessagesList::uniteAndAdd(
		MessagesSliceUpdate &update,
		base::flat_set<Slice>::iterator uniteFrom,
		base::flat_set<Slice>::iterator uniteTill,
		const Range &messages,
		MessagesRange noSkipRange) {
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
int MessagesList::addRangeItemsAndCountNew(
		MessagesSliceUpdate &update,
		const Range &messages,
		MessagesRange noSkipRange) {
	Expects(noSkipRange.from <= noSkipRange.till);

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

	auto sliceMessages = base::flat_set<MessagePosition> {
		std::begin(messages),
		std::end(messages) };
	auto slice = _slices.emplace(
		std::move(sliceMessages),
		noSkipRange
	).first;
	update.messages = &slice->messages;
	update.range = slice->range;
	return slice->messages.size();
}

template <typename Range>
void MessagesList::addRange(
		const Range &messages,
		MessagesRange noSkipRange,
		std::optional<int> count,
		bool incrementCount) {
	Expects(!count || !incrementCount);

	auto update = MessagesSliceUpdate();
	auto result = addRangeItemsAndCountNew(
		update,
		messages,
		noSkipRange);
	if (count) {
		_count = count;
	} else if (incrementCount && _count && result > 0) {
		*_count += result;
	}
	if (_slices.size() == 1) {
		if (_slices.front().range == FullMessagesRange) {
			_count = _slices.front().messages.size();
		}
	}
	update.count = _count;
	_sliceUpdated.fire(std::move(update));
}

void MessagesList::addOne(MessagePosition messageId) {
	auto range = { messageId };
	addRange(range, { messageId, messageId }, std::nullopt, true);
}

void MessagesList::addNew(MessagePosition messageId) {
	auto range = { messageId };
	addRange(range, { messageId, MaxMessagePosition }, std::nullopt, true);
}

void MessagesList::addSlice(
		std::vector<MessagePosition> &&messageIds,
		MessagesRange noSkipRange,
		std::optional<int> count) {
	addRange(messageIds, noSkipRange, count);
}

void MessagesList::removeOne(MessagePosition messageId) {
	auto update = MessagesSliceUpdate();
	auto slice = ranges::lower_bound(
		_slices,
		messageId,
		std::less<>(),
		[](const Slice &slice) { return slice.range.till; });
	if (slice != _slices.end() && slice->range.from <= messageId) {
		_slices.modify(slice, [&](Slice &slice) {
			return slice.messages.remove(messageId);
		});
		update.messages = &slice->messages;
		update.range = slice->range;
	}
	if (_count) {
		--*_count;
	}
	update.count = _count;
	if (update.messages) {
		_sliceUpdated.fire(std::move(update));
	}
}

void MessagesList::removeAll(ChannelId channelId) {
	auto removed = 0;
	for (auto i = begin(_slices); i != end(_slices); ++i) {
		_slices.modify(i, [&](Slice &slice) {
			auto &messages = slice.messages;
			for (auto j = begin(messages); j != end(messages);) {
				if (j->fullId.channel == channelId) {
					j = messages.erase(j);
					++removed;
				} else {
					++j;
				}
			}
		});
	}
	if (removed && _count) {
		*_count -= removed;
	}
}

void MessagesList::removeLessThan(MessagePosition messageId) {
	auto removed = 0;
	for (auto i = begin(_slices); i != end(_slices);) {
		if (i->range.till <= messageId) {
			removed += i->messages.size();
			i = _slices.erase(i);
			continue;
		} else if (i->range.from <= messageId) {
			_slices.modify(i, [&](Slice &slice) {
				slice.range.from = MinMessagePosition;
				auto from = begin(slice.messages);
				auto till = ranges::lower_bound(slice.messages, messageId);
				if (from != till) {
					removed += till - from;
					slice.messages.erase(from, till);
				}
			});
			break;
		} else {
			break;
		}
	}
	if (removed && _count) {
		*_count -= removed;
	}
}

void MessagesList::invalidate() {
	_slices.clear();
	_count = std::nullopt;
}

void MessagesList::invalidateBottom() {
	if (!_slices.empty()) {
		const auto &last = _slices.back();
		if (last.range.till == MaxMessagePosition) {
			_slices.modify(_slices.end() - 1, [](Slice &slice) {
				slice.range.till = slice.messages.empty()
					? slice.range.from
					: slice.messages.back();
			});
		}
	}
	_count = std::nullopt;
}

MessagesResult MessagesList::queryCurrent(const MessagesQuery &query) const {
	if (!query.aroundId) {
		return MessagesResult();
	}
	const auto slice = ranges::lower_bound(
		_slices,
		query.aroundId,
		std::less<>(),
		[](const Slice &slice) { return slice.range.till; });
	return (slice != _slices.end() && slice->range.from <= query.aroundId)
		? queryFromSlice(query, *slice)
		: MessagesResult();
}

rpl::producer<MessagesResult> MessagesList::query(
		MessagesQuery &&query) const {
	return [this, query = std::move(query)](auto consumer) {
		auto current = queryCurrent(query);
		if (current.count.has_value() || !current.messageIds.empty()) {
			consumer.put_next(std::move(current));
		}
		consumer.put_done();
		return rpl::lifetime();
	};
}

rpl::producer<MessagesSliceUpdate> MessagesList::sliceUpdated() const {
	return _sliceUpdated.events();
}

MessagesResult MessagesList::snapshot(MessagesQuery &&query) const {
	return queryCurrent(query);
}

bool MessagesList::empty() const {
	for (const auto &slice : _slices) {
		if (!slice.messages.empty()) {
			return false;
		}
	}
	return true;
}

rpl::producer<MessagesResult> MessagesList::viewer(
		MessagesQuery &&query) const {
	return rpl::single(
		queryCurrent(query)
	) | rpl::then(sliceUpdated() | rpl::map([=] {
		return queryCurrent(query);
	})) | rpl::filter([=](const MessagesResult &value) {
		return value.count.has_value() || !value.messageIds.empty();
	});
}

MessagesResult MessagesList::queryFromSlice(
		const MessagesQuery &query,
		const Slice &slice) const {
	auto result = MessagesResult {};
	auto position = ranges::lower_bound(slice.messages, query.aroundId);
	auto haveBefore = int(position - begin(slice.messages));
	auto haveEqualOrAfter = int(end(slice.messages) - position);
	auto before = qMin(haveBefore, query.limitBefore);
	auto equalOrAfter = qMin(haveEqualOrAfter, query.limitAfter + 1);
	auto ids = std::vector<MessagePosition>(position - before, position + equalOrAfter);
	result.messageIds.merge(ids.begin(), ids.end());
	if (slice.range.from == MinMessagePosition) {
		result.skippedBefore = haveBefore - before;
	}
	if (slice.range.till == MaxMessagePosition) {
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

MessagesSliceBuilder::MessagesSliceBuilder(
	Key key,
	int limitBefore,
	int limitAfter)
: _key(key)
, _limitBefore(limitBefore)
, _limitAfter(limitAfter) {
}

bool MessagesSliceBuilder::applyInitial(const MessagesResult &result) {
	mergeSliceData(
		result.count,
		result.messageIds,
		result.skippedBefore,
		result.skippedAfter);
	return true;
}

bool MessagesSliceBuilder::applyUpdate(const MessagesSliceUpdate &update) {
	auto intersects = [](MessagesRange range1, MessagesRange range2) {
		return (range1.from <= range2.till)
			&& (range2.from <= range1.till);
	};
	auto needMergeMessages = (update.messages != nullptr)
		&& intersects(update.range, {
			_ids.empty() ? _key : _ids.front(),
			_ids.empty() ? _key : _ids.back()
		});
	if (!needMergeMessages && !update.count) {
		return false;
	}
	auto skippedBefore = (update.range.from == MinMessagePosition)
		? 0
		: std::optional<int> {};
	auto skippedAfter = (update.range.till == MaxMessagePosition)
		? 0
		: std::optional<int> {};
	mergeSliceData(
		update.count,
		needMergeMessages
			? *update.messages
			: base::flat_set<MessagePosition> {},
		skippedBefore,
		skippedAfter);
	return true;
}

bool MessagesSliceBuilder::removeOne(MessagePosition messageId) {
	auto changed = false;
	if (_fullCount && *_fullCount > 0) {
		--*_fullCount;
		changed = true;
	}
	if (_ids.contains(messageId)) {
		_ids.remove(messageId);
		changed = true;
	} else if (!_ids.empty()) {
		if (_ids.front() > messageId
			&& _skippedBefore
			&& *_skippedBefore > 0) {
			--*_skippedBefore;
			changed = true;
		} else if (_ids.back() < messageId
			&& _skippedAfter
			&& *_skippedAfter > 0) {
			--*_skippedAfter;
			changed = true;
		}
	}
	return changed;
}

bool MessagesSliceBuilder::removeAll() {
	_ids = {};
	_range = FullMessagesRange;
	_fullCount = 0;
	_skippedBefore = 0;
	_skippedAfter = 0;
	return true;
}

bool MessagesSliceBuilder::removeFromChannel(ChannelId channelId) {
	for (auto i = _ids.begin(); i != _ids.end();) {
		if ((*i).fullId.channel == channelId) {
			i = _ids.erase(i);
			if (_fullCount) {
				--*_fullCount;
			}
		} else {
			++i;
		}
	}
	_skippedBefore = _skippedAfter = std::nullopt;
	checkInsufficient();
	return true;
}

bool MessagesSliceBuilder::invalidated() {
	_fullCount = _skippedBefore = _skippedAfter = std::nullopt;
	_ids.clear();
	checkInsufficient();
	return false;
}

bool MessagesSliceBuilder::bottomInvalidated() {
	_fullCount = _skippedAfter = std::nullopt;
	checkInsufficient();
	return true;
}

void MessagesSliceBuilder::checkInsufficient() {
	sliceToLimits();
}

void MessagesSliceBuilder::mergeSliceData(
		std::optional<int> count,
		const base::flat_set<MessagePosition> &messageIds,
		std::optional<int> skippedBefore,
		std::optional<int> skippedAfter) {
	if (messageIds.empty()) {
		if (count && _fullCount != count) {
			_fullCount = count;
			if (*_fullCount <= _ids.size()) {
				_fullCount = _ids.size();
				_skippedBefore = _skippedAfter = 0;
			}
		}
		fillSkippedAndSliceToLimits();
		return;
	}
	if (count) {
		_fullCount = count;
	}
	const auto impossible = MessagePosition{ .fullId = {}, .date = -1 };
	auto wasMinId = _ids.empty() ? impossible : _ids.front();
	auto wasMaxId = _ids.empty() ? impossible : _ids.back();
	_ids.merge(messageIds.begin(), messageIds.end());

	auto adjustSkippedBefore = [&](MessagePosition oldId, int oldSkippedBefore) {
		auto it = _ids.find(oldId);
		Assert(it != _ids.end());
		_skippedBefore = oldSkippedBefore - (it - _ids.begin());
		accumulate_max(*_skippedBefore, 0);
	};
	if (skippedBefore) {
		adjustSkippedBefore(messageIds.front(), *skippedBefore);
	} else if (wasMinId != impossible && _skippedBefore) {
		adjustSkippedBefore(wasMinId, *_skippedBefore);
	} else {
		_skippedBefore = std::nullopt;
	}

	auto adjustSkippedAfter = [&](MessagePosition oldId, int oldSkippedAfter) {
		auto it = _ids.find(oldId);
		Assert(it != _ids.end());
		_skippedAfter = oldSkippedAfter - (_ids.end() - it - 1);
		accumulate_max(*_skippedAfter, 0);
	};
	if (skippedAfter) {
		adjustSkippedAfter(messageIds.back(), *skippedAfter);
	} else if (wasMaxId != impossible && _skippedAfter) {
		adjustSkippedAfter(wasMaxId, *_skippedAfter);
	} else {
		_skippedAfter = std::nullopt;
	}
	fillSkippedAndSliceToLimits();
}

void MessagesSliceBuilder::fillSkippedAndSliceToLimits() {
	if (_fullCount) {
		if (_skippedBefore && !_skippedAfter) {
			_skippedAfter = *_fullCount
				- *_skippedBefore
				- int(_ids.size());
		} else if (_skippedAfter && !_skippedBefore) {
			_skippedBefore = *_fullCount
				- *_skippedAfter
				- int(_ids.size());
		}
	}
	sliceToLimits();
}

void MessagesSliceBuilder::sliceToLimits() {
	if (!_key) {
		if (!_fullCount) {
			requestMessagesCount();
		}
		return;
	}
	auto requestedSomething = false;
	auto aroundIt = ranges::lower_bound(_ids, _key);
	auto removeFromBegin = (aroundIt - _ids.begin() - _limitBefore);
	auto removeFromEnd = (_ids.end() - aroundIt - _limitAfter - 1);
	if (removeFromBegin > 0) {
		_ids.erase(_ids.begin(), _ids.begin() + removeFromBegin);
		if (_skippedBefore) {
			*_skippedBefore += removeFromBegin;
		}
	} else if (removeFromBegin < 0
		&& (!_skippedBefore || *_skippedBefore > 0)) {
		requestedSomething = true;
		requestMessages(RequestDirection::Before);
	}
	if (removeFromEnd > 0) {
		_ids.erase(_ids.end() - removeFromEnd, _ids.end());
		if (_skippedAfter) {
			*_skippedAfter += removeFromEnd;
		}
	} else if (removeFromEnd < 0
		&& (!_skippedAfter || *_skippedAfter > 0)) {
		requestedSomething = true;
		requestMessages(RequestDirection::After);
	}
	if (!_fullCount && !requestedSomething) {
		requestMessagesCount();
	}
}

void MessagesSliceBuilder::requestMessages(RequestDirection direction) {
	auto requestAroundData = [&]() -> AroundData {
		if (_ids.empty()) {
			return { _key, Data::LoadDirection::Around };
		} else if (direction == RequestDirection::Before) {
			return { _ids.front(), Data::LoadDirection::Before };
		}
		return { _ids.back(), Data::LoadDirection::After };
	};
	_insufficientAround.fire(requestAroundData());
}

void MessagesSliceBuilder::requestMessagesCount() {
	_insufficientAround.fire({
		MessagePosition(),
		Data::LoadDirection::Around });
}

MessagesSlice MessagesSliceBuilder::snapshot() const {
	auto result = MessagesSlice();
	result.ids.reserve(_ids.size());
	auto nearestToAround = std::optional<FullMsgId>();
	for (const auto &position : _ids) {
		result.ids.push_back(position.fullId);
		if (!nearestToAround && position >= _key) {
			nearestToAround = position.fullId;
		}
	}
	result.nearestToAround = nearestToAround.value_or(
		_ids.empty() ? FullMsgId() : _ids.back().fullId);
	result.skippedBefore = _skippedBefore;
	result.skippedAfter = _skippedAfter;
	result.fullCount = _fullCount;
	return result;
}

} // namespace Data
