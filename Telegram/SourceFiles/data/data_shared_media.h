/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/storage_shared_media.h"
#include "base/weak_ptr.h"
#include "data/data_sparse_ids.h"

class History;

namespace Main {
class Session;
} // namespace Main

std::optional<Storage::SharedMediaType> SharedMediaOverviewType(
	Storage::SharedMediaType type);
void SharedMediaShowOverview(
	Storage::SharedMediaType type,
	not_null<History*> history);
bool SharedMediaAllowSearch(Storage::SharedMediaType type);

rpl::producer<SparseIdsSlice> SharedMediaViewer(
	not_null<Main::Session*> session,
	Storage::SharedMediaKey key,
	int limitBefore,
	int limitAfter);

struct SharedMediaMergedKey {
	using Type = Storage::SharedMediaType;

	SharedMediaMergedKey(
		SparseIdsMergedSlice::Key mergedKey,
		Type type)
	: mergedKey(mergedKey)
	, type(type) {
	}

	bool operator==(const SharedMediaMergedKey &other) const {
		return (mergedKey == other.mergedKey)
			&& (type == other.type);
	}

	SparseIdsMergedSlice::Key mergedKey;
	Type type = Type::kCount;

};

rpl::producer<SparseIdsMergedSlice> SharedScheduledMediaViewer(
	not_null<Main::Session*> session,
	SharedMediaMergedKey key,
	int limitBefore,
	int limitAfter);

rpl::producer<SparseIdsMergedSlice> SharedMediaMergedViewer(
	not_null<Main::Session*> session,
	SharedMediaMergedKey key,
	int limitBefore,
	int limitAfter);

class SharedMediaWithLastSlice {
public:
	using Type = Storage::SharedMediaType;

	using Value = std::variant<FullMsgId, not_null<PhotoData*>>;
	using MessageId = SparseIdsMergedSlice::UniversalMsgId;
	using UniversalMsgId = std::variant<
		MessageId,
		not_null<PhotoData*>>;

	struct Key {
		Key(
			PeerId peerId,
			PeerId migratedPeerId,
			Type type,
			UniversalMsgId universalId,
			bool scheduled = false)
		: peerId(peerId)
		, migratedPeerId(migratedPeerId)
		, type(type)
		, universalId(universalId)
		, scheduled(scheduled) {
			Expects(v::is<MessageId>(universalId) || type == Type::ChatPhoto);
		}

		bool operator==(const Key &other) const {
			return (peerId == other.peerId)
				&& (migratedPeerId == other.migratedPeerId)
				&& (type == other.type)
				&& (universalId == other.universalId);
		}
		bool operator!=(const Key &other) const {
			return !(*this == other);
		}

		PeerId peerId = 0;
		PeerId migratedPeerId = 0;
		Type type = Type::kCount;
		UniversalMsgId universalId;
		bool scheduled = false;

	};

	SharedMediaWithLastSlice(
		not_null<Main::Session*> session,
		Key key);
	SharedMediaWithLastSlice(
		not_null<Main::Session*> session,
		Key key,
		SparseIdsMergedSlice slice,
		std::optional<SparseIdsMergedSlice> ending);

	std::optional<int> fullCount() const;
	std::optional<int> skippedBefore() const;
	std::optional<int> skippedAfter() const;
	std::optional<int> indexOf(Value fullId) const;
	int size() const;
	Value operator[](int index) const;
	std::optional<int> distance(const Key &a, const Key &b) const;

	void reverse();

	static SparseIdsMergedSlice::Key ViewerKey(const Key &key) {
		return {
			key.peerId,
			key.migratedPeerId,
			v::is<MessageId>(key.universalId)
				? v::get<MessageId>(key.universalId)
				: ServerMaxMsgId - 1
		};
	}
	static SparseIdsMergedSlice::Key EndingKey(const Key &key) {
		return {
			key.peerId,
			key.migratedPeerId,
			ServerMaxMsgId - 1
		};
	}

private:
	static std::optional<SparseIdsMergedSlice> EndingSlice(const Key &key) {
		return v::is<MessageId>(key.universalId)
			? base::make_optional(SparseIdsMergedSlice(EndingKey(key)))
			: std::nullopt;
	}

	static std::optional<PhotoId> LastPeerPhotoId(
		not_null<Main::Session*> session,
		PeerId peerId);
	static std::optional<bool> IsLastIsolated(
		not_null<Main::Session*> session,
		const SparseIdsMergedSlice &slice,
		const std::optional<SparseIdsMergedSlice> &ending,
		std::optional<PhotoId> lastPeerPhotoId);
	static std::optional<FullMsgId> LastFullMsgId(
		const SparseIdsMergedSlice &slice);
	static std::optional<int> Add(
			const std::optional<int> &a,
			const std::optional<int> &b) {
		return (a && b) ? base::make_optional(*a + *b) : std::nullopt;
	}
	static Value ComputeId(PeerId peerId, MsgId msgId) {
		return FullMsgId(peerToChannel(peerId), msgId);
	}
	static Value ComputeId(const Key &key) {
		if (const auto messageId = std::get_if<MessageId>(&key.universalId)) {
			return (*messageId >= 0)
				? ComputeId(key.peerId, *messageId)
				: ComputeId(key.migratedPeerId, ServerMaxMsgId + *messageId);
		}
		return v::get<not_null<PhotoData*>>(key.universalId);
	}

	bool isolatedInSlice() const {
		return (_slice.skippedAfter() != 0);
	}
	std::optional<int> lastPhotoSkip() const {
		return _isolatedLastPhoto
			| [](bool isolated) { return isolated ? 1 : 0; };
	}

	std::optional<int> skippedBeforeImpl() const;
	std::optional<int> skippedAfterImpl() const;
	std::optional<int> indexOfImpl(Value fullId) const;

	not_null<Main::Session*> _session;
	Key _key;
	SparseIdsMergedSlice _slice;
	std::optional<SparseIdsMergedSlice> _ending;
	std::optional<PhotoId> _lastPhotoId;
	std::optional<bool> _isolatedLastPhoto;
	bool _reversed = false;

};

rpl::producer<SharedMediaWithLastSlice> SharedMediaWithLastViewer(
	not_null<Main::Session*> session,
	SharedMediaWithLastSlice::Key key,
	int limitBefore,
	int limitAfter);

rpl::producer<SharedMediaWithLastSlice> SharedMediaWithLastReversedViewer(
	not_null<Main::Session*> session,
	SharedMediaWithLastSlice::Key key,
	int limitBefore,
	int limitAfter);
