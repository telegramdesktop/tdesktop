/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/variant.h"
#include "data/data_types.h"

namespace Data {

using FileOriginMessage = FullMsgId;

struct FileOriginUserPhoto {
	FileOriginUserPhoto(UserId userId, PhotoId photoId)
	: userId(userId)
	, photoId(photoId) {
	}

	UserId userId = 0;
	PhotoId photoId = 0;

	inline bool operator<(const FileOriginUserPhoto &other) const {
		return std::tie(userId, photoId)
			< std::tie(other.userId, other.photoId);
	}
};

struct FileOriginPeerPhoto {
	explicit FileOriginPeerPhoto(PeerId peerId) : peerId(peerId) {
	}

	PeerId peerId = 0;

	inline bool operator<(const FileOriginPeerPhoto &other) const {
		return peerId < other.peerId;
	}
};

struct FileOriginStickerSet {
	FileOriginStickerSet(uint64 setId, uint64 accessHash)
	: setId(setId)
	, accessHash(accessHash) {
	}

	uint64 setId = 0;
	uint64 accessHash = 0;

	inline bool operator<(const FileOriginStickerSet &other) const {
		return setId < other.setId;
	}
};

struct FileOriginSavedGifs {
	inline bool operator<(const FileOriginSavedGifs &) const {
		return false;
	}
};

using FileOrigin = base::optional_variant<
	FileOriginMessage,
	FileOriginUserPhoto,
	FileOriginPeerPhoto,
	FileOriginStickerSet,
	FileOriginSavedGifs>;

// Volume_id, dc_id, local_id.
struct SimpleFileLocationId {
	SimpleFileLocationId(uint64 volumeId, int32 dcId, int32 localId);

	uint64 volumeId = 0;
	int32 dcId = 0;
	int32 localId = 0;
};

bool operator<(
	const SimpleFileLocationId &a,
	const SimpleFileLocationId &b);

using DocumentFileLocationId = uint64;
using FileLocationId = base::variant<
	SimpleFileLocationId,
	DocumentFileLocationId>;
using UpdatedFileReferences = std::map<FileLocationId, QByteArray>;

UpdatedFileReferences GetFileReferences(const MTPmessages_Messages &data);
UpdatedFileReferences GetFileReferences(const MTPphotos_Photos &data);
UpdatedFileReferences GetFileReferences(const MTPVector<MTPUser> &data);
UpdatedFileReferences GetFileReferences(const MTPmessages_Chats &data);
UpdatedFileReferences GetFileReferences(
	const MTPmessages_RecentStickers &data);
UpdatedFileReferences GetFileReferences(
	const MTPmessages_FavedStickers &data);
UpdatedFileReferences GetFileReferences(const MTPmessages_StickerSet &data);
UpdatedFileReferences GetFileReferences(const MTPmessages_SavedGifs &data);

} // namespace Data
