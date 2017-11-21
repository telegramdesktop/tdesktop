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

#include <rpl/event_stream.h>
#include "storage/storage_facade.h"
#include "storage/storage_sparse_ids_list.h"

namespace Storage {

// Allow forward declarations.
enum class SharedMediaType : char {
	Photo = 0,
	Video = 1,
	MusicFile = 2,
	File = 3,
	VoiceFile = 4,
	Link = 5,
	ChatPhoto = 6,
	RoundVoiceFile = 7,
	GIF = 8,
	RoundFile = 9,

	kCount = 10,
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
		base::optional<int> count = base::none)
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
	base::optional<int> count;

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
	SharedMediaRemoveAll(PeerId peerId) : peerId(peerId) {
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

	rpl::producer<SharedMediaResult> query(SharedMediaQuery &&query) const;
	rpl::producer<SharedMediaSliceUpdate> sliceUpdated() const;
	rpl::producer<SharedMediaRemoveOne> oneRemoved() const;
	rpl::producer<SharedMediaRemoveAll> allRemoved() const;

private:
	using Lists = std::array<SparseIdsList, kSharedMediaTypeCount>;

	std::map<PeerId, Lists>::iterator enforceLists(PeerId peer);

	std::map<PeerId, Lists> _lists;

	rpl::event_stream<SharedMediaSliceUpdate> _sliceUpdated;
	rpl::event_stream<SharedMediaRemoveOne> _oneRemoved;
	rpl::event_stream<SharedMediaRemoveAll> _allRemoved;

	rpl::lifetime _lifetime;

};

} // namespace Storage
