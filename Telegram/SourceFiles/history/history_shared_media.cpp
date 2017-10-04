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
#include "history/history_shared_media.h"

#include "auth_session.h"
#include "apiwrap.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "history/history_media_types.h"

namespace {

using Type = SharedMediaSlice::Type;

inline MediaOverviewType SharedMediaTypeToOverview(Type type) {
	switch (type) {
	case Type::Photo: return OverviewPhotos;
	case Type::Video: return OverviewVideos;
	case Type::MusicFile: return OverviewMusicFiles;
	case Type::File: return OverviewFiles;
	case Type::VoiceFile: return OverviewVoiceFiles;
	case Type::Link: return OverviewLinks;
	default: break;
	}
	return OverviewCount;
}

} // namespace

base::optional<Storage::SharedMediaType> SharedMediaOverviewType(
	Storage::SharedMediaType type) {
	if (SharedMediaTypeToOverview(type) != OverviewCount) {
		return type;
	}
	return base::none;
}

void SharedMediaShowOverview(
	Storage::SharedMediaType type,
	not_null<History*> history) {
	if (SharedMediaOverviewType(type)) {
		Ui::showPeerOverview(history, SharedMediaTypeToOverview(type));
	}
}

class SharedMediaSliceBuilder {
public:
	using Type = Storage::SharedMediaType;
	using Key = Storage::SharedMediaKey;

	SharedMediaSliceBuilder(Key key, int limitBefore, int limitAfter);

	using Result = Storage::SharedMediaResult;
	using SliceUpdate = Storage::SharedMediaSliceUpdate;
	using RemoveOne = Storage::SharedMediaRemoveOne;
	using RemoveAll = Storage::SharedMediaRemoveAll;
	bool applyUpdate(const Result &result);
	bool applyUpdate(const SliceUpdate &update);
	bool applyUpdate(const RemoveOne &update);
	bool applyUpdate(const RemoveAll &update);

	void checkInsufficientMedia();
	using AroundData = std::pair<MsgId, ApiWrap::SliceType>;
	auto insufficientMediaAround() const {
		return _insufficientMediaAround.events();
	}

	SharedMediaSlice snapshot() const;

private:
	enum class RequestDirection {
		Before,
		After,
	};
	void requestMessages(RequestDirection direction);
	void requestMessagesCount();
	void fillSkippedAndSliceToLimits();
	void sliceToLimits();

	void mergeSliceData(
		base::optional<int> count,
		const base::flat_set<MsgId> &messageIds,
		base::optional<int> skippedBefore = base::none,
		base::optional<int> skippedAfter = base::none);

	Key _key;
	base::flat_set<MsgId> _ids;
	MsgRange _range;
	base::optional<int> _fullCount;
	base::optional<int> _skippedBefore;
	base::optional<int> _skippedAfter;
	int _limitBefore = 0;
	int _limitAfter = 0;

	rpl::event_stream<AroundData> _insufficientMediaAround;

};

class SharedMediaMergedSliceBuilder {
public:
	using Type = SharedMediaMergedSlice::Type;
	using Key = SharedMediaMergedSlice::Key;

	SharedMediaMergedSliceBuilder(Key key);

	void applyPartUpdate(SharedMediaSlice &&update);
	void applyMigratedUpdate(SharedMediaSlice &&update);

	SharedMediaMergedSlice snapshot() const;

private:
	Key _key;
	SharedMediaSlice _part;
	base::optional<SharedMediaSlice> _migrated;

};

class SharedMediaWithLastSliceBuilder {
public:
	using Type = SharedMediaWithLastSlice::Type;
	using Key = SharedMediaWithLastSlice::Key;

	SharedMediaWithLastSliceBuilder(Key key);

	void applyViewerUpdate(SharedMediaMergedSlice &&update);
	void applyEndingUpdate(SharedMediaMergedSlice &&update);

	SharedMediaWithLastSlice snapshot() const;

private:
	Key _key;
	SharedMediaWithLastSlice _data;

};

SharedMediaSlice::SharedMediaSlice(Key key) : SharedMediaSlice(
	key,
	{},
	{},
	base::none,
	base::none,
	base::none) {
}

SharedMediaSlice::SharedMediaSlice(
	Key key,
	const base::flat_set<MsgId> &ids,
	MsgRange range,
	base::optional<int> fullCount,
	base::optional<int> skippedBefore,
	base::optional<int> skippedAfter)
	: _key(key)
	, _ids(ids)
	, _range(range)
	, _fullCount(fullCount)
	, _skippedBefore(skippedBefore)
	, _skippedAfter(skippedAfter) {
}

base::optional<int> SharedMediaSlice::indexOf(MsgId msgId) const {
	auto it = _ids.find(msgId);
	if (it != _ids.end()) {
		return (it - _ids.begin());
	}
	return base::none;
}

MsgId SharedMediaSlice::operator[](int index) const {
	Expects(index >= 0 && index < size());

	return *(_ids.begin() + index);
}

base::optional<int> SharedMediaSlice::distance(const Key &a, const Key &b) const {
	if (a.type != _key.type
		|| b.type != _key.type
		|| a.peerId != _key.peerId
		|| b.peerId != _key.peerId) {
		return base::none;
	}
	if (auto i = indexOf(a.messageId)) {
		if (auto j = indexOf(b.messageId)) {
			return *j - *i;
		}
	}
	return base::none;
}

QString SharedMediaSlice::debug() const {
	auto before = _skippedBefore
		? (*_skippedBefore
			? ('(' + QString::number(*_skippedBefore) + ").. ")
			: QString())
		: QString(".. ");
	auto after = _skippedAfter
		? (*_skippedAfter
			? (" ..(" + QString::number(*_skippedAfter) + ')')
			: QString())
		: QString(" ..");
	auto middle = (size() > 2)
		? QString::number((*this)[0]) + " .. " + QString::number((*this)[size() - 1])
		: (size() > 1)
		? QString::number((*this)[0]) + ' ' + QString::number((*this)[1])
		: ((size() > 0) ? QString::number((*this)[0]) : QString());
	return before + middle + after;
}

SharedMediaSliceBuilder::SharedMediaSliceBuilder(
	Key key,
	int limitBefore,
	int limitAfter)
	: _key(key)
	, _limitBefore(limitBefore)
	, _limitAfter(limitAfter) {
}

bool SharedMediaSliceBuilder::applyUpdate(const Result &result) {
	mergeSliceData(
		result.count,
		result.messageIds,
		result.skippedBefore,
		result.skippedAfter);
	return true;
}

bool SharedMediaSliceBuilder::applyUpdate(const SliceUpdate &update) {
	if (update.peerId != _key.peerId || update.type != _key.type) {
		return false;
	}
	auto intersects = [](MsgRange range1, MsgRange range2) {
		return (range1.from <= range2.till)
			&& (range2.from <= range1.till);
	};
	auto needMergeMessages = (update.messages != nullptr)
		&& intersects(update.range, {
			_ids.empty() ? _key.messageId : _ids.front(),
			_ids.empty() ? _key.messageId : _ids.back()
		});
	if (!needMergeMessages && !update.count) {
		return false;
	}
	auto skippedBefore = (update.range.from == 0)
		? 0
		: base::optional<int> {};
	auto skippedAfter = (update.range.till == ServerMaxMsgId)
		? 0
		: base::optional<int> {};
	mergeSliceData(
		update.count,
		needMergeMessages
			? *update.messages
			: base::flat_set<MsgId> {},
		skippedBefore,
		skippedAfter);
	return true;
}

bool SharedMediaSliceBuilder::applyUpdate(const RemoveOne &update) {
	if (update.peerId != _key.peerId || !update.types.test(_key.type)) {
		return false;
	}
	auto changed = false;
	if (_fullCount && *_fullCount > 0) {
		--*_fullCount;
		changed = true;
	}
	if (_ids.contains(update.messageId)) {
		_ids.remove(update.messageId);
		changed = true;
	} else if (!_ids.empty()) {
		if (_ids.front() > update.messageId
			&& _skippedBefore
			&& *_skippedBefore > 0) {
			--*_skippedBefore;
			changed = true;
		} else if (_ids.back() < update.messageId
			&& _skippedAfter
			&& *_skippedAfter > 0) {
			--*_skippedAfter;
			changed = true;
		}
	}
	return changed;
}

bool SharedMediaSliceBuilder::applyUpdate(const RemoveAll &update) {
	if (update.peerId != _key.peerId) {
		return false;
	}
	_ids = {};
	_range = { 0, ServerMaxMsgId };
	_fullCount = 0;
	_skippedBefore = 0;
	_skippedAfter = 0;
	return true;
}

void SharedMediaSliceBuilder::checkInsufficientMedia() {
	sliceToLimits();
}

void SharedMediaSliceBuilder::mergeSliceData(
	base::optional<int> count,
	const base::flat_set<MsgId> &messageIds,
	base::optional<int> skippedBefore,
	base::optional<int> skippedAfter) {
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
		_skippedBefore = base::none;
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
		_skippedAfter = base::none;
	}
	fillSkippedAndSliceToLimits();
}

void SharedMediaSliceBuilder::fillSkippedAndSliceToLimits() {
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

void SharedMediaSliceBuilder::sliceToLimits() {
	if (!_key.messageId) {
		if (!_fullCount) {
			requestMessagesCount();
		}
		return;
	}
	auto requestedSomething = false;
	auto aroundIt = base::lower_bound(_ids, _key.messageId);
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

void SharedMediaSliceBuilder::requestMessages(
		RequestDirection direction) {
	using SliceType = ApiWrap::SliceType;
	auto requestAroundData = [&]() -> AroundData {
		if (_ids.empty()) {
			return { _key.messageId, SliceType::Around };
		} else if (direction == RequestDirection::Before) {
			return { _ids.front(), SliceType::Before };
		}
		return { _ids.back(), SliceType::After };
	};
	_insufficientMediaAround.fire(requestAroundData());
}

void SharedMediaSliceBuilder::requestMessagesCount() {
	_insufficientMediaAround.fire({ 0, ApiWrap::SliceType::Around });
}

SharedMediaSlice SharedMediaSliceBuilder::snapshot() const {
	return SharedMediaSlice(
		_key,
		_ids,
		_range,
		_fullCount,
		_skippedBefore,
		_skippedAfter);
}

rpl::producer<SharedMediaSlice> SharedMediaViewer(
		SharedMediaSlice::Key key,
		int limitBefore,
		int limitAfter) {
	Expects(IsServerMsgId(key.messageId) || (key.messageId == 0));
	Expects((key.messageId != 0) || (limitBefore == 0 && limitAfter == 0));

	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		auto builder = lifetime.make_state<SharedMediaSliceBuilder>(
			key,
			limitBefore,
			limitAfter);
		auto applyUpdate = [=](auto &&update) {
			if (builder->applyUpdate(std::forward<decltype(update)>(update))) {
				consumer.put_next(builder->snapshot());
			}
		};
		auto requestMediaAround = [
			peer = App::peer(key.peerId),
			type = key.type
		](const SharedMediaSliceBuilder::AroundData &data) {
			Auth().api().requestSharedMedia(
				peer,
				type,
				data.first,
				data.second);
		};
		builder->insufficientMediaAround()
			| rpl::start_with_next(requestMediaAround, lifetime);

		Auth().storage().sharedMediaSliceUpdated()
			| rpl::start_with_next(applyUpdate, lifetime);
		Auth().storage().sharedMediaOneRemoved()
			| rpl::start_with_next(applyUpdate, lifetime);
		Auth().storage().sharedMediaAllRemoved()
			| rpl::start_with_next(applyUpdate, lifetime);

		Auth().storage().query(Storage::SharedMediaQuery(
			key,
			limitBefore,
			limitAfter))
			| rpl::start_with_next_done(
				applyUpdate,
				[=] { builder->checkInsufficientMedia(); },
				lifetime);

		return lifetime;
	};
}

SharedMediaMergedSlice::SharedMediaMergedSlice(Key key) : SharedMediaMergedSlice(
	key,
	SharedMediaSlice(PartKey(key)),
	MigratedSlice(key)) {
}

SharedMediaMergedSlice::SharedMediaMergedSlice(
	Key key,
	SharedMediaSlice part,
	base::optional<SharedMediaSlice> migrated)
: _key(key)
, _part(std::move(part))
, _migrated(std::move(migrated)) {
}

base::optional<int> SharedMediaMergedSlice::fullCount() const {
	return Add(
		_part.fullCount(),
		_migrated ? _migrated->fullCount() : 0);
}

base::optional<int> SharedMediaMergedSlice::skippedBefore() const {
	return Add(
		isolatedInMigrated() ? 0 : _part.skippedBefore(),
		_migrated
			? (isolatedInPart()
				? _migrated->fullCount()
				: _migrated->skippedBefore())
			: 0
	);
}

base::optional<int> SharedMediaMergedSlice::skippedAfter() const {
	return Add(
		isolatedInMigrated() ? _part.fullCount() : _part.skippedAfter(),
		isolatedInPart() ? 0 : _migrated->skippedAfter()
	);
}

base::optional<int> SharedMediaMergedSlice::indexOf(FullMsgId fullId) const {
	return isFromPart(fullId)
		? (_part.indexOf(fullId.msg) | func::add(migratedSize()))
		: isolatedInPart()
			? base::none
			: isFromMigrated(fullId)
				? _migrated->indexOf(fullId.msg)
				: base::none;
}

int SharedMediaMergedSlice::size() const {
	return (isolatedInPart() ? 0 : migratedSize())
		+ (isolatedInMigrated() ? 0 : _part.size());
}

FullMsgId SharedMediaMergedSlice::operator[](int index) const {
	Expects(index >= 0 && index < size());

	if (auto size = migratedSize()) {
		if (index < size) {
			return ComputeId(*_migrated, index);
		}
		index -= size;
	}
	return ComputeId(_part, index);
}

base::optional<int> SharedMediaMergedSlice::distance(const Key &a, const Key &b) const {
	if (a.type != _key.type
		|| b.type != _key.type
		|| a.peerId != _key.peerId
		|| b.peerId != _key.peerId
		|| a.migratedPeerId != _key.migratedPeerId
		|| b.migratedPeerId != _key.migratedPeerId) {
		return base::none;
	}
	if (auto i = indexOf(ComputeId(a))) {
		if (auto j = indexOf(ComputeId(b))) {
			return *j - *i;
		}
	}
	return base::none;
}

QString SharedMediaMergedSlice::debug() const {
	return (_migrated ? (_migrated->debug() + '|') : QString()) + _part.debug();
}

SharedMediaMergedSliceBuilder::SharedMediaMergedSliceBuilder(Key key)
	: _key(key)
	, _part(SharedMediaMergedSlice::PartKey(_key))
	, _migrated(SharedMediaMergedSlice::MigratedSlice(_key)) {
}

void SharedMediaMergedSliceBuilder::applyPartUpdate(SharedMediaSlice &&update) {
	_part = std::move(update);
}

void SharedMediaMergedSliceBuilder::applyMigratedUpdate(SharedMediaSlice &&update) {
	_migrated = std::move(update);
}

SharedMediaMergedSlice SharedMediaMergedSliceBuilder::snapshot() const {
	return SharedMediaMergedSlice(
		_key,
		_part,
		_migrated
	);
}

rpl::producer<SharedMediaMergedSlice> SharedMediaMergedViewer(
		SharedMediaMergedSlice::Key key,
		int limitBefore,
		int limitAfter) {
	Expects(IsServerMsgId(key.universalId)
		|| (key.universalId == 0)
		|| (IsServerMsgId(-key.universalId) && key.migratedPeerId != 0));
	Expects((key.universalId != 0) || (limitBefore == 0 && limitAfter == 0));

	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		auto builder = lifetime.make_state<SharedMediaMergedSliceBuilder>(key);

		SharedMediaViewer(
			SharedMediaMergedSlice::PartKey(key),
			limitBefore,
			limitAfter
		) | rpl::start_with_next([=](SharedMediaSlice &&update) {
			builder->applyPartUpdate(std::move(update));
			consumer.put_next(builder->snapshot());
		}, lifetime);

		if (key.migratedPeerId) {
			SharedMediaViewer(
				SharedMediaMergedSlice::MigratedKey(key),
				limitBefore,
				limitAfter
			) | rpl::start_with_next([=](SharedMediaSlice &&update) {
				builder->applyMigratedUpdate(std::move(update));
				consumer.put_next(builder->snapshot());
			}, lifetime);
		}

		return lifetime;
	};
}

SharedMediaWithLastSlice::SharedMediaWithLastSlice(Key key) : SharedMediaWithLastSlice(
	key,
	SharedMediaMergedSlice(ViewerKey(key)),
	EndingSlice(key)) {
}

SharedMediaWithLastSlice::SharedMediaWithLastSlice(
	Key key,
	SharedMediaMergedSlice slice,
	base::optional<SharedMediaMergedSlice> ending)
	: _key(key)
	, _slice(std::move(slice))
	, _ending(std::move(ending))
	, _lastPhotoId(LastPeerPhotoId(key.peerId))
	, _isolatedLastPhoto(_key.type == Type::ChatPhoto
		? IsLastIsolated(_slice, _ending, _lastPhotoId)
		: false) {
}

base::optional<int> SharedMediaWithLastSlice::fullCount() const {
	return Add(
		_slice.fullCount(),
		_isolatedLastPhoto | [](bool isolated) { return isolated ? 1 : 0; });
}

base::optional<int> SharedMediaWithLastSlice::skippedBefore() const {
	return _slice.skippedBefore();
}

base::optional<int> SharedMediaWithLastSlice::skippedAfter() const {
	return isolatedInSlice()
		? Add(
			_slice.skippedAfter(),
			lastPhotoSkip())
		: (lastPhotoSkip() | [](int) { return 0; });
}

base::optional<int> SharedMediaWithLastSlice::indexOf(Value value) const {
	return base::get_if<FullMsgId>(&value)
		? _slice.indexOf(*base::get_if<FullMsgId>(&value))
		: (isolatedInSlice()
			|| (*base::get_if<not_null<PhotoData*>>(&value))->id != _lastPhotoId)
			? base::none
			: Add(_slice.size() - 1, lastPhotoSkip());
}

int SharedMediaWithLastSlice::size() const {
	return _slice.size()
		+ ((!isolatedInSlice() && lastPhotoSkip() == 1) ? 1 : 0);
}

SharedMediaWithLastSlice::Value SharedMediaWithLastSlice::operator[](int index) const {
	Expects(index >= 0 && index < size());

	return (index < _slice.size())
		? Value(_slice[index])
		: Value(App::photo(_lastPhotoId));
}

base::optional<int> SharedMediaWithLastSlice::distance(const Key &a, const Key &b) const {
	if (a.type != _key.type
		|| b.type != _key.type
		|| a.peerId != _key.peerId
		|| b.peerId != _key.peerId
		|| a.migratedPeerId != _key.migratedPeerId
		|| b.migratedPeerId != _key.migratedPeerId) {
		return base::none;
	}
	if (auto i = indexOf(ComputeId(a))) {
		if (auto j = indexOf(ComputeId(b))) {
			return *j - *i;
		}
	}
	return base::none;
}

QString SharedMediaWithLastSlice::debug() const {
	return _slice.debug() + (_isolatedLastPhoto
		? (*_isolatedLastPhoto ? "@" : "")
		: "?");
}

PhotoId SharedMediaWithLastSlice::LastPeerPhotoId(PeerId peerId) {
	if (auto peer = App::peerLoaded(peerId)) {
		return peer->photoId;
	}
	return UnknownPeerPhotoId;
}

base::optional<bool> SharedMediaWithLastSlice::IsLastIsolated(
		const SharedMediaMergedSlice &slice,
		const base::optional<SharedMediaMergedSlice> &ending,
		PhotoId lastPeerPhotoId) {
	if (lastPeerPhotoId == UnknownPeerPhotoId) {
		return base::none;
	} else if (!lastPeerPhotoId) {
		return false;
	}
	return LastFullMsgId(ending ? *ending : slice)
		| [](FullMsgId msgId) {	return App::histItemById(msgId); }
		| [](HistoryItem *item) { return item ? item->getMedia() : nullptr; }
		| [](HistoryMedia *media) {
			return (media && media->type() == MediaTypePhoto)
				? static_cast<HistoryPhoto*>(media)->photo()
				: nullptr;
		}
		| [](PhotoData *photo) { return photo ? photo->id : 0; }
		| [&](PhotoId photoId) { return lastPeerPhotoId != photoId; };
}

base::optional<FullMsgId> SharedMediaWithLastSlice::LastFullMsgId(
		const SharedMediaMergedSlice &slice) {
	if (slice.fullCount() == 0) {
		return FullMsgId();
	} else if (slice.size() == 0 || slice.skippedAfter() != 0) {
		return base::none;
	}
	return slice[slice.size() - 1];
}

rpl::producer<SharedMediaWithLastSlice> SharedMediaWithLastViewer(
		SharedMediaWithLastSlice::Key key,
		int limitBefore,
		int limitAfter) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		auto builder = lifetime.make_state<SharedMediaWithLastSliceBuilder>(key);

		SharedMediaMergedViewer(
			SharedMediaWithLastSlice::ViewerKey(key),
			limitBefore,
			limitAfter
		) | rpl::start_with_next([=](SharedMediaMergedSlice &&update) {
			builder->applyViewerUpdate(std::move(update));
			consumer.put_next(builder->snapshot());
		}, lifetime);

		if (base::get_if<SharedMediaWithLastSlice::MessageId>(&key.universalId)) {
			SharedMediaMergedViewer(
				SharedMediaWithLastSlice::EndingKey(key),
				1,
				1
			) | rpl::start_with_next([=](SharedMediaMergedSlice &&update) {
				builder->applyEndingUpdate(std::move(update));
				consumer.put_next(builder->snapshot());
			}, lifetime);
		}

		return lifetime;
	};
}

SharedMediaWithLastSliceBuilder::SharedMediaWithLastSliceBuilder(Key key)
	: _key(key)
	, _data(_key) {
}

void SharedMediaWithLastSliceBuilder::applyViewerUpdate(
		SharedMediaMergedSlice &&update) {
	_data = SharedMediaWithLastSlice(
		_key,
		std::move(update),
		std::move(_data._ending));
}

void SharedMediaWithLastSliceBuilder::applyEndingUpdate(
		SharedMediaMergedSlice &&update) {
	_data = SharedMediaWithLastSlice(
		_key,
		std::move(_data._slice),
		std::move(update));
}

SharedMediaWithLastSlice SharedMediaWithLastSliceBuilder::snapshot() const {
	return _data;
}
