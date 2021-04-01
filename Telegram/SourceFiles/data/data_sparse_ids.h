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
		std::optional<int> fullCount,
		std::optional<int> skippedBefore,
		std::optional<int> skippedAfter);

	std::optional<int> fullCount() const { return _fullCount; }
	std::optional<int> skippedBefore() const { return _skippedBefore; }
	std::optional<int> skippedAfter() const { return _skippedAfter; }
	std::optional<int> indexOf(MsgId msgId) const;
	int size() const { return _ids.size(); }
	MsgId operator[](int index) const;
	std::optional<int> distance(MsgId a, MsgId b) const;
	std::optional<MsgId> nearest(MsgId msgId) const;

private:
	base::flat_set<MsgId> _ids;
	MsgRange _range;
	std::optional<int> _fullCount;
	std::optional<int> _skippedBefore;
	std::optional<int> _skippedAfter;

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
		std::optional<SparseIdsSlice> migrated);

	std::optional<int> fullCount() const;
	std::optional<int> skippedBefore() const;
	std::optional<int> skippedAfter() const;
	std::optional<int> indexOf(FullMsgId fullId) const;
	int size() const;
	FullMsgId operator[](int index) const;
	std::optional<int> distance(const Key &a, const Key &b) const;
	std::optional<FullMsgId> nearest(UniversalMsgId id) const;

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
	static std::optional<SparseIdsSlice> MigratedSlice(const Key &key) {
		return key.migratedPeerId
			? base::make_optional(SparseIdsSlice())
			: std::nullopt;
	}

	static bool IsFromSlice(PeerId peerId, FullMsgId fullId) {
		return peerIsChannel(peerId)
			? (peerId == peerFromChannel(fullId.channel))
			: !fullId.channel;
	}
	static FullMsgId ComputeId(PeerId peerId, MsgId msgId) {
		return FullMsgId(peerToChannel(peerId), msgId);
	}
	static FullMsgId ComputeId(const Key &key) {
		return (key.universalId >= 0)
			? ComputeId(key.peerId, key.universalId)
			: ComputeId(key.migratedPeerId, ServerMaxMsgId + key.universalId);
	}
	static std::optional<int> Add(
			const std::optional<int> &a,
			const std::optional<int> &b) {
		return (a && b) ? base::make_optional(*a + *b) : std::nullopt;
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
	std::optional<SparseIdsSlice> _migrated;

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
		std::optional<int> count,
		const base::flat_set<MsgId> &messageIds,
		std::optional<int> skippedBefore = std::nullopt,
		std::optional<int> skippedAfter = std::nullopt);

	Key _key;
	base::flat_set<MsgId> _ids;
	MsgRange _range;
	std::optional<int> _fullCount;
	std::optional<int> _skippedBefore;
	std::optional<int> _skippedAfter;
	int _limitBefore = 0;
	int _limitAfter = 0;

	rpl::event_stream<AroundData> _insufficientAround;

};
