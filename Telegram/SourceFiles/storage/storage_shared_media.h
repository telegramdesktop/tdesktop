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

#include "storage/storage_facade.h"

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

	kCount = 9,
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

struct SharedMediaQuery {
	SharedMediaQuery(
		PeerId peerId,
		SharedMediaType type,
		MsgId messageId,
		int limitBefore,
		int limitAfter)
		: peerId(peerId)
		, messageId(messageId)
		, limitBefore(limitBefore)
		, limitAfter(limitAfter)
		, type(type) {
	}

	PeerId peerId = 0;
	MsgId messageId = 0;
	int limitBefore = 0;
	int limitAfter = 0;
	SharedMediaType type = SharedMediaType::kCount;

};

struct SharedMediaResult {
	base::optional<int> count;
	base::optional<int> skippedBefore;
	base::optional<int> skippedAfter;
	std::vector<MsgId> messageIds;
};

class SharedMedia {
public:
	using Type = SharedMediaType;

	void add(SharedMediaAddNew &&query);
	void add(SharedMediaAddExisting &&query);
	void add(SharedMediaAddSlice &&query);
	void remove(SharedMediaRemoveOne &&query);
	void remove(SharedMediaRemoveAll &&query);
	void query(
		const SharedMediaQuery &query,
		base::lambda_once<void(SharedMediaResult&&)> &&callback);

private:
	class List {
	public:
		void addNew(MsgId messageId);
		void addExisting(MsgId messageId, MsgRange noSkipRange);
		void addSlice(
			std::vector<MsgId> &&messageIds,
			MsgRange noSkipRange,
			base::optional<int> count);
		void removeOne(MsgId messageId);
		void removeAll();
		void query(
			const SharedMediaQuery &query,
			base::lambda_once<void(SharedMediaResult&&)> &&callback);

	private:
		struct Slice {
			Slice(base::flat_set<MsgId> &&messages, MsgRange range);

			template <typename Range>
			void merge(const Range &moreMessages, MsgRange moreNoSkipRange);

			base::flat_set<MsgId> messages;
			MsgRange range;

			inline bool operator<(const Slice &other) const {
				return range.from < other.range.from;
			}

		};

		template <typename Range>
		int uniteAndAdd(
			base::flat_set<Slice>::iterator uniteFrom,
			base::flat_set<Slice>::iterator uniteTill,
			const Range &messages,
			MsgRange noSkipRange);
		template <typename Range>
		int addRangeItemsAndCount(
			const Range &messages,
			MsgRange noSkipRange,
			base::optional<int> count);
		template <typename Range>
		int addRange(
			const Range &messages,
			MsgRange noSkipRange,
			base::optional<int> count);

		SharedMediaResult queryFromSlice(
			const SharedMediaQuery &query,
			const Slice &slice);

		base::optional<int> _count;
		base::flat_set<Slice> _slices;

	};
	using Lists = std::array<List, kSharedMediaTypeCount>;

	std::map<PeerId, Lists> _lists;

};

} // namespace Storage
