/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Data {

class RecentSharedMediaGifts final {
public:
	explicit RecentSharedMediaGifts(not_null<Main::Session*> session);
	~RecentSharedMediaGifts();

	void request(
		not_null<PeerData*> peer,
		Fn<void(std::vector<DocumentId>)> done);

private:
	struct Entry {
		std::deque<DocumentId> ids;
		crl::time lastRequestTime = 0;
		mtpRequestId requestId = 0;
	};

	const not_null<Main::Session*> _session;

	base::flat_map<PeerId, Entry> _recent;

};

} // namespace Data
