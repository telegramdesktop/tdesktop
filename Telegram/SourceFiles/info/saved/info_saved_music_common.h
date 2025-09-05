/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Info::Saved {

struct MusicTag {
	explicit MusicTag(not_null<PeerData*> peer)
	: peer(peer) {
	}

	not_null<PeerData*> peer;
};

} // namespace Info::Saved
