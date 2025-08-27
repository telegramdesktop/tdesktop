/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Info::Statistics {

struct Tag final {
	explicit Tag() = default;
	explicit Tag(
		PeerData *peer,
		FullMsgId contextId,
		FullStoryId storyId)
	: peer(peer)
	, contextId(contextId)
	, storyId(storyId) {
	}

	PeerData *peer = nullptr;
	FullMsgId contextId;
	FullStoryId storyId;

};

} // namespace Info::Statistics
