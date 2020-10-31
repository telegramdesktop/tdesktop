/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/event_stream.h>
#include "storage/storage_facade.h"
#include "storage/storage_sparse_ids_list.h"

namespace Storage {

// Allow forward declarations.
enum class SharedMediaType : signed char {
	Photo,
	Video,
	PhotoVideo,
	MusicFile,
	File,
	VoiceFile,
	Link,
	ChatPhoto,
	RoundVoiceFile,
	GIF,
	RoundFile,
	Pinned,

	kCount,
};
constexpr auto kSharedMediaTypeCount = static_cast<int>(SharedMediaType::kCount);
constexpr bool IsValidSharedMediaType(SharedMediaType type) {
	return (static_cast<int>(type) >= 0)
		&& (static_cast<int>(type) < kSharedMediaTypeCount);
}

using SharedMediaTypesMask = base::enum_mask<SharedMediaType>;

struct SharedMediaAddNew {
	SharedMediaAddNew(PeerId peerId, SharedMediaTypesMask types, MsgId messageId)
		: peerId(peerId), messageId(messageId), types(types) {
	}

	PeerId peerId = 0;
	MsgId messageId = 0;
	SharedMediaTypesMask types;

};

struct SharedMediaAddExisting {
	SharedMediaAddExisting(
		PeerId peerId,
		SharedMediaTypesMask types,
		MsgId messageId,
		MsgRange noSkipRange)
		: peerId(peerId)
		, messageId(messageId)
		, noSkipRange(noSkipRange)
		, types(types) {
	}

	PeerId peerId = 0;
	MsgId messageId = 0;
	MsgRange noSkipRange;
	SharedMediaTypesMask types;

};

struct SharedMediaAddSlice {
	SharedMediaAddSlice(
		PeerId peerId,
		SharedMediaType type,
		std::vector<MsgId> &&messageIds,
		MsgRange noSkipRange,
		std::optional<int> count = std::nullopt)
		: peerId(peerId)
		, messageIds(std::move(messageIds))
		, noSkipRange(noSkipRange)
		, type(type)
		, count(count) {
	}

	PeerId peerId = 0;
	std::vector<MsgId> messageIds;
	MsgRange noSkipRange;
	SharedMediaType type = SharedMediaType::kCount;
	std::optional<int> count;

};

struct SharedMediaRemoveOne {
	SharedMediaRemoveOne(
		PeerId peerId,
		SharedMediaTypesMask types,
		MsgId messageId)
	: peerId(peerId)
	, messageId(messageId)
	, types(types) {
	}

	PeerId peerId = 0;
	MsgId messageId = 0;
	SharedMediaTypesMask types;

};

struct SharedMediaRemoveAll {
	SharedMediaRemoveAll(
		PeerId peerId,
		SharedMediaTypesMask types = SharedMediaTypesMask::All())
	: peerId(peerId)
	, types(types) {
	}

	PeerId peerId = 0;
	SharedMediaTypesMask types;

};

struct SharedMediaInvalidateBottom {
	SharedMediaInvalidateBottom(PeerId peerId) : peerId(peerId) {
	}

	PeerId peerId = 0;

};

struct SharedMediaKey {
	SharedMediaKey(
		PeerId peerId,
		SharedMediaType type,
		MsgId messageId)
	: peerId(peerId)
	, type(type)
	, messageId(messageId) {
	}

	bool operator==(const SharedMediaKey &other) const {
		return (peerId == other.peerId)
			&& (type == other.type)
			&& (messageId == other.messageId);
	}
	bool operator!=(const SharedMediaKey &other) const {
		return !(*this == other);
	}

	PeerId peerId = 0;
	SharedMediaType type = SharedMediaType::kCount;
	MsgId messageId = 0;

};

struct SharedMediaQuery {
	SharedMediaQuery(
		SharedMediaKey key,
		int limitBefore,
		int limitAfter)
	: key(key)
	, limitBefore(limitBefore)
	, limitAfter(limitAfter) {
	}

	SharedMediaKey key;
	int limitBefore = 0;
	int limitAfter = 0;

};

using SharedMediaResult = SparseIdsListResult;

struct SharedMediaSliceUpdate {
	SharedMediaSliceUpdate(
		PeerId peerId,
		SharedMediaType type,
		const SparseIdsSliceUpdate &data)
	: peerId(peerId)
	, type(type)
	, data(data) {
	}

	PeerId peerId = 0;
	SharedMediaType type = SharedMediaType::kCount;
	SparseIdsSliceUpdate data;
};

class SharedMedia {
public:
	using Type = SharedMediaType;

	void add(SharedMediaAddNew &&query);
	void add(SharedMediaAddExisting &&query);
	void add(SharedMediaAddSlice &&query);
	void remove(SharedMediaRemoveOne &&query);
	void remove(SharedMediaRemoveAll &&query);
	void invalidate(SharedMediaInvalidateBottom &&query);

	rpl::producer<SharedMediaResult> query(SharedMediaQuery &&query) const;
	SharedMediaResult snapshot(const SharedMediaQuery &query) const;
	bool empty(const SharedMediaKey &key) const;
	rpl::producer<SharedMediaSliceUpdate> sliceUpdated() const;
	rpl::producer<SharedMediaRemoveOne> oneRemoved() const;
	rpl::producer<SharedMediaRemoveAll> allRemoved() const;
	rpl::producer<SharedMediaInvalidateBottom> bottomInvalidated() const;

private:
	using Lists = std::array<SparseIdsList, kSharedMediaTypeCount>;

	std::map<PeerId, Lists>::iterator enforceLists(PeerId peer);

	std::map<PeerId, Lists> _lists;

	rpl::event_stream<SharedMediaSliceUpdate> _sliceUpdated;
	rpl::event_stream<SharedMediaRemoveOne> _oneRemoved;
	rpl::event_stream<SharedMediaRemoveAll> _allRemoved;
	rpl::event_stream<SharedMediaInvalidateBottom> _bottomInvalidated;

	rpl::lifetime _lifetime;

};

} // namespace Storage
