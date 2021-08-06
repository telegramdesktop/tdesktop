/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_sparse_ids.h"

#include <rpl/combine.h>
#include "storage/storage_sparse_ids_list.h"

SparseIdsMergedSlice::SparseIdsMergedSlice(Key key)
: SparseIdsMergedSlice(
	key,
	SparseIdsSlice(),
	MigratedSlice(key)) {
}

SparseIdsMergedSlice::SparseIdsMergedSlice(
	Key key,
	SparseIdsSlice part,
	std::optional<SparseIdsSlice> migrated)
: _key(key)
, _part(std::move(part))
, _migrated(std::move(migrated)) {
}

SparseIdsMergedSlice::SparseIdsMergedSlice(
	Key key,
	SparseUnsortedIdsSlice scheduled)
: _key(key)
, _scheduled(std::move(scheduled)) {
}

std::optional<int> SparseIdsMergedSlice::fullCount() const {
	return _scheduled
		? _scheduled->fullCount()
		: Add(
			_part.fullCount(),
			_migrated ? _migrated->fullCount() : 0);
}

std::optional<int> SparseIdsMergedSlice::skippedBefore() const {
	return _scheduled
		? _scheduled->skippedBefore()
		: Add(
			isolatedInMigrated() ? 0 : _part.skippedBefore(),
			_migrated
				? (isolatedInPart()
					? _migrated->fullCount()
					: _migrated->skippedBefore())
				: 0
		);
}

std::optional<int> SparseIdsMergedSlice::skippedAfter() const {
	return _scheduled
		? _scheduled->skippedAfter()
		: Add(
			isolatedInMigrated() ? _part.fullCount() : _part.skippedAfter(),
			isolatedInPart() ? 0 : _migrated->skippedAfter()
		);
}

std::optional<int> SparseIdsMergedSlice::indexOf(
		FullMsgId fullId) const {
	return _scheduled
		? _scheduled->indexOf(fullId.msg)
		: isFromPart(fullId)
		? (_part.indexOf(fullId.msg) | func::add(migratedSize()))
		: isolatedInPart()
			? std::nullopt
			: isFromMigrated(fullId)
				? _migrated->indexOf(fullId.msg)
				: std::nullopt;
}

int SparseIdsMergedSlice::size() const {
	return _scheduled
		? _scheduled->size()
		: (isolatedInPart() ? 0 : migratedSize())
			+ (isolatedInMigrated() ? 0 : _part.size());
}

FullMsgId SparseIdsMergedSlice::operator[](int index) const {
	Expects(index >= 0 && index < size());

	if (_scheduled) {
		return ComputeId(_key.peerId, (*_scheduled)[index]);
	}

	if (const auto size = migratedSize()) {
		if (index < size) {
			return ComputeId(_key.migratedPeerId, (*_migrated)[index]);
		}
		index -= size;
	}
	return ComputeId(_key.peerId, _part[index]);
}

std::optional<int> SparseIdsMergedSlice::distance(
		const Key &a,
		const Key &b) const {
	if (const auto i = indexOf(ComputeId(a))) {
		if (const auto j = indexOf(ComputeId(b))) {
			return *j - *i;
		}
	}
	return std::nullopt;
}

auto SparseIdsMergedSlice::nearest(
		UniversalMsgId id) const -> std::optional<FullMsgId> {
	if (_scheduled) {
		if (const auto nearestId = _scheduled->nearest(id)) {
			return ComputeId(_key.peerId, *nearestId);
		}
	}
	const auto convertFromPartNearest = [&](MsgId result) {
		return ComputeId(_key.peerId, result);
	};
	const auto convertFromMigratedNearest = [&](MsgId result) {
		return ComputeId(_key.migratedPeerId, result);
	};
	if (IsServerMsgId(id)) {
		if (auto partNearestId = _part.nearest(id)) {
			return partNearestId
				| convertFromPartNearest;
		} else if (isolatedInPart()) {
			return std::nullopt;
		}
		return _migrated->nearest(ServerMaxMsgId - 1)
			| convertFromMigratedNearest;
	}
	if (auto migratedNearestId = _migrated
		? _migrated->nearest(id + ServerMaxMsgId)
		: std::nullopt) {
		return migratedNearestId
			| convertFromMigratedNearest;
	} else if (isolatedInMigrated()) {
		return std::nullopt;
	}
	return _part.nearest(0)
		| convertFromPartNearest;
}

SparseIdsSliceBuilder::SparseIdsSliceBuilder(
	Key key,
	int limitBefore,
	int limitAfter)
: _key(key)
, _limitBefore(limitBefore)
, _limitAfter(limitAfter) {
}

bool SparseIdsSliceBuilder::applyInitial(
		const Storage::SparseIdsListResult &result) {
	mergeSliceData(
		result.count,
		result.messageIds,
		result.skippedBefore,
		result.skippedAfter);
	return true;
}

bool SparseIdsSliceBuilder::applyUpdate(
		const Storage::SparseIdsSliceUpdate &update) {
	auto intersects = [](MsgRange range1, MsgRange range2) {
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
	auto skippedBefore = (update.range.from == 0)
		? 0
		: std::optional<int> {};
	auto skippedAfter = (update.range.till == ServerMaxMsgId)
		? 0
		: std::optional<int> {};
	mergeSliceData(
		update.count,
		needMergeMessages
			? *update.messages
			: base::flat_set<MsgId> {},
		skippedBefore,
		skippedAfter);
	return true;
}

bool SparseIdsSliceBuilder::removeOne(MsgId messageId) {
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
	if (changed) {
		checkInsufficient();
	}
	return changed;
}

bool SparseIdsSliceBuilder::removeAll() {
	_ids = {};
	_fullCount = 0;
	_skippedBefore = 0;
	_skippedAfter = 0;
	return true;
}

bool SparseIdsSliceBuilder::invalidateBottom() {
	_fullCount = _skippedAfter = std::nullopt;
	checkInsufficient();
	return true;
}

void SparseIdsSliceBuilder::checkInsufficient() {
	sliceToLimits();
}

void SparseIdsSliceBuilder::mergeSliceData(
		std::optional<int> count,
		const base::flat_set<MsgId> &messageIds,
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
	auto wasMinId = _ids.empty() ? -1 : _ids.front();
	auto wasMaxId = _ids.empty() ? -1 : _ids.back();
	_ids.merge(messageIds.begin(), messageIds.end());

	auto adjustSkippedBefore = [&](MsgId oldId, int oldSkippedBefore) {
		auto it = _ids.find(oldId);
		Assert(it != _ids.end());
		_skippedBefore = oldSkippedBefore - (it - _ids.begin());
		accumulate_max(*_skippedBefore, 0);
	};
	if (skippedBefore) {
		adjustSkippedBefore(messageIds.front(), *skippedBefore);
	} else if (wasMinId >= 0 && _skippedBefore) {
		adjustSkippedBefore(wasMinId, *_skippedBefore);
	} else {
		_skippedBefore = std::nullopt;
	}

	auto adjustSkippedAfter = [&](MsgId oldId, int oldSkippedAfter) {
		auto it = _ids.find(oldId);
		Assert(it != _ids.end());
		_skippedAfter = oldSkippedAfter - (_ids.end() - it - 1);
		accumulate_max(*_skippedAfter, 0);
	};
	if (skippedAfter) {
		adjustSkippedAfter(messageIds.back(), *skippedAfter);
	} else if (wasMaxId >= 0 && _skippedAfter) {
		adjustSkippedAfter(wasMaxId, *_skippedAfter);
	} else {
		_skippedAfter = std::nullopt;
	}
	fillSkippedAndSliceToLimits();
}

void SparseIdsSliceBuilder::fillSkippedAndSliceToLimits() {
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

void SparseIdsSliceBuilder::sliceToLimits() {
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

void SparseIdsSliceBuilder::requestMessages(
		RequestDirection direction) {
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

void SparseIdsSliceBuilder::requestMessagesCount() {
	_insufficientAround.fire({ 0, Data::LoadDirection::Around });
}

SparseIdsSlice SparseIdsSliceBuilder::snapshot() const {
	return SparseIdsSlice(
		_ids,
		_fullCount,
		_skippedBefore,
		_skippedAfter);
}

rpl::producer<SparseIdsMergedSlice> SparseIdsMergedSlice::CreateViewer(
		SparseIdsMergedSlice::Key key,
		int limitBefore,
		int limitAfter,
		Fn<SimpleViewerFunction> simpleViewer) {
	Expects(IsServerMsgId(key.universalId)
		|| (key.universalId == 0)
		|| (IsServerMsgId(ServerMaxMsgId + key.universalId) && key.migratedPeerId != 0));
	Expects((key.universalId != 0)
		|| (limitBefore == 0 && limitAfter == 0));

	return [=](auto consumer) {
		auto partViewer = simpleViewer(
			key.peerId,
			SparseIdsMergedSlice::PartKey(key),
			limitBefore,
			limitAfter
		);
		if (!key.migratedPeerId) {
			return std::move(
				partViewer
			) | rpl::start_with_next([=](SparseIdsSlice &&part) {
				consumer.put_next(SparseIdsMergedSlice(
					key,
					std::move(part),
					std::nullopt));
			});
		}
		auto migratedViewer = simpleViewer(
			key.migratedPeerId,
			SparseIdsMergedSlice::MigratedKey(key),
			limitBefore,
			limitAfter);
		return rpl::combine(
			std::move(partViewer),
			std::move(migratedViewer)
		) | rpl::start_with_next([=](
				SparseIdsSlice &&part,
				SparseIdsSlice &&migrated) {
			consumer.put_next(SparseIdsMergedSlice(
				key,
				std::move(part),
				std::move(migrated)));
		});
	};
}
