/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_messages.h"

namespace Storage {
struct SparseIdsListResult;
struct SparseIdsSliceUpdate;
} // namespace Storage

class SparseIdsSlice {
public:
	using Key = MsgId;

	SparseIdsSlice() = default;
	SparseIdsSlice(
		const base::flat_set<MsgId> &ids,
		MsgRange range,
		base::optional<int> fullCount,
		base::optional<int> skippedBefore,
		base::optional<int> skippedAfter);

	base::optional<int> fullCount() const { return _fullCount; }
	base::optional<int> skippedBefore() const { return _skippedBefore; }
	base::optional<int> skippedAfter() const { return _skippedAfter; }
	base::optional<int> indexOf(MsgId msgId) const;
	int size() const { return _ids.size(); }
	MsgId operator[](int index) const;
	base::optional<int> distance(MsgId a, MsgId b) const;
	base::optional<MsgId> nearest(MsgId msgId) const;

private:
	base::flat_set<MsgId> _ids;
	MsgRange _range;
	base::optional<int> _fullCount;
	base::optional<int> _skippedBefore;
	base::optional<int> _skippedAfter;

};

class SparseIdsMergedSlice {
public:
	using UniversalMsgId = MsgId;
	struct Key {
		Key(
			PeerId peerId,
			PeerId migratedPeerId,
			UniversalMsgId universalId)
		: peerId(peerId)
		, migratedPeerId(migratedPeerId)
		, universalId(universalId) {
		}

		bool operator==(const Key &other) const {
			return (peerId == other.peerId)
				&& (migratedPeerId == other.migratedPeerId)
				&& (universalId == other.universalId);
		}
		bool operator!=(const Key &other) const {
			return !(*this == other);
		}

		PeerId peerId = 0;
		PeerId migratedPeerId = 0;
		UniversalMsgId universalId = 0;

	};

	SparseIdsMergedSlice(Key key);
	SparseIdsMergedSlice(
		Key key,
		SparseIdsSlice part,
		base::optional<SparseIdsSlice> migrated);

	base::optional<int> fullCount() const;
	base::optional<int> skippedBefore() const;
	base::optional<int> skippedAfter() const;
	base::optional<int> indexOf(FullMsgId fullId) const;
	int size() const;
	FullMsgId operator[](int index) const;
	base::optional<int> distance(const Key &a, const Key &b) const;
	base::optional<FullMsgId> nearest(UniversalMsgId id) const;

	using SimpleViewerFunction = rpl::producer<SparseIdsSlice>(
		PeerId peerId,
		SparseIdsSlice::Key simpleKey,
		int limitBefore,
		int limitAfter);
	static rpl::producer<SparseIdsMergedSlice> CreateViewer(
		SparseIdsMergedSlice::Key key,
		int limitBefore,
		int limitAfter,
		Fn<SimpleViewerFunction> simpleViewer);

private:
	static SparseIdsSlice::Key PartKey(const Key &key) {
		return (key.universalId < 0) ? 1 : key.universalId;
	}
	static SparseIdsSlice::Key MigratedKey(const Key &key) {
		return (key.universalId < 0)
			? (ServerMaxMsgId + key.universalId)
			: (key.universalId > 0) ? (ServerMaxMsgId - 1) : 0;
	}
	static base::optional<SparseIdsSlice> MigratedSlice(const Key &key) {
		return key.migratedPeerId
			? base::make_optional(SparseIdsSlice())
			: base::none;
	}

	static bool IsFromSlice(PeerId peerId, FullMsgId fullId) {
		return peerIsChannel(peerId)
			? (peerId == peerFromChannel(fullId.channel))
			: !fullId.channel;
	}
	static FullMsgId ComputeId(PeerId peerId, MsgId msgId) {
		return FullMsgId(
			peerIsChannel(peerId) ? peerToBareInt(peerId) : 0,
			msgId);
	}
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
		return IsFromSlice(_key.peerId, fullId);
	}
	bool isFromMigrated(FullMsgId fullId) const {
		return _migrated
			? IsFromSlice(_key.migratedPeerId, fullId)
			: false;
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
	SparseIdsSlice _part;
	base::optional<SparseIdsSlice> _migrated;

};

class SparseIdsSliceBuilder {
public:
	using Key = SparseIdsSlice::Key;

	SparseIdsSliceBuilder(Key key, int limitBefore, int limitAfter);

	bool applyInitial(const Storage::SparseIdsListResult &result);
	bool applyUpdate(const Storage::SparseIdsSliceUpdate &update);
	bool removeOne(MsgId messageId);
	bool removeAll();
	bool invalidateBottom();

	void checkInsufficient();
	struct AroundData {
		MsgId aroundId = 0;
		Data::LoadDirection direction = Data::LoadDirection::Around;

		inline bool operator<(const AroundData &other) const {
			return (aroundId < other.aroundId)
				|| ((aroundId == other.aroundId)
					&& (direction < other.direction));
		}
	};
	auto insufficientAround() const {
		return _insufficientAround.events();
	}

	SparseIdsSlice snapshot() const;

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

	rpl::event_stream<AroundData> _insufficientAround;

};
