/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Info::Stories {

[[nodiscard]] int ArchiveId();

struct Tag {
	explicit Tag(
		not_null<PeerData*> peer,
		int albumId = 0,
		int addingToAlbumId = 0)
	: peer(peer)
	, albumId(albumId)
	, addingToAlbumId(addingToAlbumId) {
	}

	not_null<PeerData*> peer;
	int albumId = 0;
	int addingToAlbumId = 0;
};

} // namespace Info::Stories
