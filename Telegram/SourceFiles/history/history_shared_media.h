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
#include "base/weak_unique_ptr.h"

base::optional<Storage::SharedMediaType> SharedMediaOverviewType(
	Storage::SharedMediaType type);
void SharedMediaShowOverview(
	Storage::SharedMediaType type,
	not_null<History*> history);

class SharedMediaSlice {
public:
	using Type = Storage::SharedMediaType;
	using Key = Storage::SharedMediaKey;

	SharedMediaSlice(Key key);
	SharedMediaSlice(
		Key key,
		const base::flat_set<MsgId> &ids,
		MsgRange range,
		base::optional<int> fullCount,
		base::optional<int> skippedBefore,
		base::optional<int> skippedAfter);

	const Key &key() const { return _key; }

	base::optional<int> fullCount() const { return _fullCount; }
	base::optional<int> skippedBefore() const { return _skippedBefore; }
	base::optional<int> skippedAfter() const { return _skippedAfter; }
	base::optional<int> indexOf(MsgId msgId) const;
	int size() const { return _ids.size(); }
	MsgId operator[](int index) const;
	base::optional<int> distance(const Key &a, const Key &b) const;
	base::optional<MsgId> nearest(MsgId msgId) const;

	QString debug() const;

private:
	Key _key;
	base::flat_set<MsgId> _ids;
	MsgRange _range;
	base::optional<int> _fullCount;
	base::optional<int> _skippedBefore;
	base::optional<int> _skippedAfter;

	class SharedMediaSliceBuilder;

};

rpl::producer<SharedMediaSlice> SharedMediaViewer(
	SharedMediaSlice::Key key,
	int limitBefore,
	int limitAfter);

class SharedMediaMergedSlice {
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

	SharedMediaMergedSlice(Key key);
	SharedMediaMergedSlice(
		Key key,
		SharedMediaSlice part,
		base::optional<SharedMediaSlice> migrated);

	const Key &key() const { return _key; }

	base::optional<int> fullCount() const;
	base::optional<int> skippedBefore() const;
	base::optional<int> skippedAfter() const;
	base::optional<int> indexOf(FullMsgId fullId) const;
	int size() const;
	FullMsgId operator[](int index) const;
	base::optional<int> distance(const Key &a, const Key &b) const;
	base::optional<UniversalMsgId> nearest(UniversalMsgId id) const;

	QString debug() const;

	static SharedMediaSlice::Key PartKey(const Key &key) {
		return {
			key.peerId,
			key.type,
			(key.universalId < 0) ? 1 : key.universalId
		};
	}
	static SharedMediaSlice::Key MigratedKey(const Key &key) {
		return {
			key.migratedPeerId,
			key.type,
			(key.universalId < 0)
				? (ServerMaxMsgId + key.universalId)
				: (key.universalId > 0) ? (ServerMaxMsgId - 1) : 0
		};
	}

private:
	static base::optional<SharedMediaSlice> MigratedSlice(const Key &key) {
		return key.migratedPeerId
			? base::make_optional(SharedMediaSlice(MigratedKey(key)))
			: base::none;
	}

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
		return (key.universalId >= 0)
			? ComputeId(key.peerId, key.universalId)
			: ComputeId(key.migratedPeerId, ServerMaxMsgId + key.universalId);
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
		return IsServerMsgId(ServerMaxMsgId + _key.universalId)
			&& (_migrated->skippedAfter() != 0);
	}

	Key _key;
	SharedMediaSlice _part;
	base::optional<SharedMediaSlice> _migrated;

	friend class SharedMediaMergedSliceBuilder;

};

rpl::producer<SharedMediaMergedSlice> SharedMediaMergedViewer(
	SharedMediaMergedSlice::Key key,
	int limitBefore,
	int limitAfter);

class SharedMediaWithLastSlice {
public:
	using Type = Storage::SharedMediaType;

	// base::none in those mean CurrentPeerPhoto.
	using Value = base::variant<FullMsgId, not_null<PhotoData*>>;
	using MessageId = SharedMediaMergedSlice::UniversalMsgId;
	using UniversalMsgId = base::variant<
		MessageId,
		not_null<PhotoData*>>;

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
			Expects(base::get_if<MessageId>(&universalId) != nullptr
				|| type == Type::ChatPhoto);
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
		UniversalMsgId universalId;

	};

	SharedMediaWithLastSlice(Key key);
	SharedMediaWithLastSlice(
		Key key,
		SharedMediaMergedSlice slice,
		base::optional<SharedMediaMergedSlice> ending);

	base::optional<int> fullCount() const;
	base::optional<int> skippedBefore() const;
	base::optional<int> skippedAfter() const;
	base::optional<int> indexOf(Value fullId) const;
	int size() const;
	Value operator[](int index) const;
	base::optional<int> distance(const Key &a, const Key &b) const;

	QString debug() const;

	static SharedMediaMergedSlice::Key ViewerKey(const Key &key) {
		return {
			key.peerId,
			key.migratedPeerId,
			key.type,
			base::get_if<MessageId>(&key.universalId)
			? (*base::get_if<MessageId>(&key.universalId))
			: ServerMaxMsgId - 1
		};
	}
	static SharedMediaMergedSlice::Key EndingKey(const Key &key) {
		return {
			key.peerId,
			key.migratedPeerId,
			key.type,
			ServerMaxMsgId - 1
		};
	}

private:
	static base::optional<SharedMediaMergedSlice> EndingSlice(const Key &key) {
		return base::get_if<MessageId>(&key.universalId)
			? base::make_optional(SharedMediaMergedSlice(EndingKey(key)))
			: base::none;
	}

	static PhotoId LastPeerPhotoId(PeerId peerId);
	static base::optional<bool> IsLastIsolated(
		const SharedMediaMergedSlice &slice,
		const base::optional<SharedMediaMergedSlice> &ending,
		PhotoId lastPeerPhotoId);
	static base::optional<FullMsgId> LastFullMsgId(
		const SharedMediaMergedSlice &slice);
	static base::optional<int> Add(
			const base::optional<int> &a,
			const base::optional<int> &b) {
		return (a && b) ? base::make_optional(*a + *b) : base::none;
	}
	static Value ComputeId(PeerId peerId, MsgId msgId) {
		return FullMsgId(
			peerIsChannel(peerId) ? peerToBareInt(peerId) : 0,
			msgId);
	}
	static Value ComputeId(const Key &key) {
		if (auto messageId = base::get_if<MessageId>(&key.universalId)) {
			return (*messageId >= 0)
				? ComputeId(key.peerId, *messageId)
				: ComputeId(key.migratedPeerId, ServerMaxMsgId + *messageId);
		}
		return *base::get_if<not_null<PhotoData*>>(&key.universalId);
	}

	bool isolatedInSlice() const {
		return (_slice.skippedAfter() != 0);
	}
	base::optional<int> lastPhotoSkip() const {
		return _isolatedLastPhoto
			| [](bool isolated) { return isolated ? 1 : 0; };
	}

	Key _key;
	SharedMediaMergedSlice _slice;
	base::optional<SharedMediaMergedSlice> _ending;
	PhotoId _lastPhotoId = 0;
	base::optional<bool> _isolatedLastPhoto;

	friend class SharedMediaWithLastSliceBuilder;

};

rpl::producer<SharedMediaWithLastSlice> SharedMediaWithLastViewer(
	SharedMediaWithLastSlice::Key key,
	int limitBefore,
	int limitAfter);
