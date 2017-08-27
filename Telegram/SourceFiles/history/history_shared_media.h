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
#pragma once

#include "storage/storage_shared_media.h"
#include "mtproto/sender.h"
#include "base/weak_unique_ptr.h"

base::optional<Storage::SharedMediaType> SharedMediaOverviewType(
	Storage::SharedMediaType type);
void SharedMediaShowOverview(
	Storage::SharedMediaType type,
	not_null<History*> history);

class SharedMediaViewer;
class SharedMediaSlice {
public:
	using Key = Storage::SharedMediaKey;

	SharedMediaSlice(
		Key key,
		base::optional<int> fullCount = base::none)
		: _key(key)
		, _fullCount(fullCount) {
	}

	const Key &key() const {
		return _key;
	}

	base::optional<int> fullCount() const {
		return _fullCount;
	}
	base::optional<int> skippedBefore() const {
		return _skippedBefore;
	}
	base::optional<int> skippedAfter() const {
		return _skippedAfter;
	}
	base::optional<int> indexOf(MsgId msgId) const {
		auto it = _ids.find(msgId);
		if (it != _ids.end()) {
			return (it - _ids.begin());
		}
		return base::none;
	}
	int size() const {
		return _ids.size();
	}

	MsgId operator[](int index) const {
		Expects(index >= 0 && index < size());

		return *(_ids.begin() + index);
	}

	base::optional<int> distance(const Key &a, const Key &b) const {
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

	QString debug() const;

private:
	Key _key;
	base::flat_set<MsgId> _ids;
	MsgRange _range;
	base::optional<int> _fullCount;
	base::optional<int> _skippedBefore;
	base::optional<int> _skippedAfter;

	friend class SharedMediaViewer;

};

class SharedMediaViewer :
	private base::Subscriber,
	public base::enable_weak_from_this {
public:
	using Type = Storage::SharedMediaType;
	using Key = Storage::SharedMediaKey;

	SharedMediaViewer(
		Key key,
		int limitBefore,
		int limitAfter);
	SharedMediaViewer(const SharedMediaViewer &other) = delete;
	SharedMediaViewer(SharedMediaViewer &&other) = default;

	void start();

	base::Observable<SharedMediaSlice> updated;

private:
	using InitialResult = Storage::SharedMediaResult;
	using SliceUpdate = Storage::SharedMediaSliceUpdate;
	using OneRemoved = Storage::SharedMediaRemoveOne;
	using AllRemoved = Storage::SharedMediaRemoveAll;

	void loadInitial();
	enum class RequestDirection {
		Before,
		After,
	};
	void requestMessages(RequestDirection direction);
	void applyStoredResult(InitialResult &&result);
	void applyUpdate(const SliceUpdate &update);
	void applyUpdate(const OneRemoved &update);
	void applyUpdate(const AllRemoved &update);
	void sliceToLimits();

	void mergeSliceData(
		base::optional<int> count,
		const base::flat_set<MsgId> &messageIds,
		base::optional<int> skippedBefore = base::none,
		base::optional<int> skippedAfter = base::none);

	Key _key;
	int _limitBefore = 0;
	int _limitAfter = 0;
	mtpRequestId _beforeRequestId = 0;
	mtpRequestId _afterRequestId = 0;
	SharedMediaSlice _data;

};

class SharedMediaViewerMerged;
class SharedMediaSliceMerged {
public:
	using Type = Storage::SharedMediaType;
	using UniversalMsgId = MsgId;
	struct Key {
		Key(
			PeerId peerId,
			PeerId migratedPeerId,
			Type type,
			UniversalMsgId universalId)
			: peerId(peerId)
			, migratedPeerId(migratedPeerId)
			, type(type)
			, universalId(universalId) {
		}

		bool operator==(const Key &other) const {
			return (peerId == other.peerId)
				&& (migratedPeerId == other.migratedPeerId)
				&& (type == other.type)
				&& (universalId == other.universalId);
		}

		PeerId peerId = 0;
		PeerId migratedPeerId = 0;
		Type type = Type::kCount;
		UniversalMsgId universalId = 0;

	};
	SharedMediaSliceMerged(
		Key key,
		SharedMediaSlice part,
		base::optional<SharedMediaSlice> migrated)
		: _key(key)
		, _part(part)
		, _migrated(migrated) {
	}

	base::optional<int> fullCount() const {
		return Add(
			_part.fullCount(),
			_migrated ? _migrated->fullCount() : 0);
	}
	base::optional<int> skippedBefore() const {
		return Add(
			isolatedInMigrated() ? 0 : _part.skippedBefore(),
			_migrated
				? (isolatedInPart()
					? _migrated->fullCount()
					: _migrated->skippedBefore())
				: 0
		);
	}
	base::optional<int> skippedAfter() const {
		return Add(
			isolatedInMigrated() ? _part.fullCount() : _part.skippedAfter(),
			isolatedInPart() ? 0 : _migrated->skippedAfter()
		);
	}
	base::optional<int> indexOf(FullMsgId fullId) const {
		return isFromPart(fullId)
			? (_part.indexOf(fullId.msg) | func::add(migratedSize()))
			: isolatedInPart()
				? base::none
				: isFromMigrated(fullId)
					? _migrated->indexOf(fullId.msg)
					: base::none;
	}
	int size() const {
		return (isolatedInPart() ? 0 : migratedSize())
			+ (isolatedInMigrated() ? 0 : _part.size());
	}

	FullMsgId operator[](int index) const {
		Expects(index >= 0 && index < size());

		if (auto size = migratedSize()) {
			if (index < size) {
				return ComputeId(*_migrated, index);
			}
			index -= size;
		}
		return ComputeId(_part, index);
	}

	base::optional<int> distance(const Key &a, const Key &b) const {
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

	QString debug() const {
		return (_migrated ? (_migrated->debug() + '|') : QString()) + _part.debug();
	}

private:
	static bool IsFromSlice(const SharedMediaSlice &slice, FullMsgId fullId) {
		auto peer = slice.key().peerId;
		return peerIsChannel(peer)
			? (peer == peerFromChannel(fullId.channel))
			: !fullId.channel;
	}
	static FullMsgId ComputeId(PeerId peerId, MsgId msgId) {
		return FullMsgId(
			peerIsChannel(peerId) ? peerToBareInt(peerId) : 0,
			msgId);
	}
	static FullMsgId ComputeId(const SharedMediaSlice &slice, int index) {
		return ComputeId(slice.key().peerId, slice[index]);
	};
	static FullMsgId ComputeId(const Key &key) {
		return (key.universalId > 0)
			? ComputeId(key.peerId, key.universalId)
			: ComputeId(key.migratedPeerId, -key.universalId);
	}
	static base::optional<int> Add(
			const base::optional<int> &a,
			const base::optional<int> &b) {
		return (a && b) ? base::make_optional(*a + *b) : base::none;
	}

	bool isFromPart(FullMsgId fullId) const {
		return IsFromSlice(_part, fullId);
	}
	bool isFromMigrated(FullMsgId fullId) const {
		return _migrated ? IsFromSlice(*_migrated, fullId) : false;
	}
	int migratedSize() const {
		return isolatedInPart() ? 0 : _migrated->size();
	}
	bool isolatedInPart() const {
		return IsServerMsgId(_key.universalId)
			&& (!_migrated || _part.skippedBefore() != 0);
	}
	bool isolatedInMigrated() const {
		return IsServerMsgId(-_key.universalId)
			&& (_migrated->skippedAfter() != 0);
	}

	Key _key;
	SharedMediaSlice _part;
	base::optional<SharedMediaSlice> _migrated;

	friend class SharedMediaViewerMerged;

};

class SharedMediaViewerMerged : private base::Subscriber {
public:
	using Type = SharedMediaSliceMerged::Type;
	using Key = SharedMediaSliceMerged::Key;

	SharedMediaViewerMerged(
		Key key,
		int limitBefore,
		int limitAfter);
	SharedMediaViewerMerged(const SharedMediaViewerMerged &other) = delete;
	SharedMediaViewerMerged(SharedMediaViewerMerged &&other) = default;

	void start();

	base::Observable<SharedMediaSliceMerged> updated;

private:
	static SharedMediaSlice::Key PartKey(const Key &key);
	static SharedMediaSlice::Key MigratedKey(const Key &key);
	static std::unique_ptr<SharedMediaViewer> MigratedViewer(
		const Key &key,
		int limitBefore,
		int limitAfter);
	static base::optional<SharedMediaSlice> MigratedSlice(const Key &key);

	Key _key;
	int _limitBefore = 0;
	int _limitAfter = 0;
	SharedMediaViewer _part;
	std::unique_ptr<SharedMediaViewer> _migrated;
	SharedMediaSliceMerged _data;

};
