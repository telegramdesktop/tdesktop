/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_star_gift.h"

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

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
		Fn<void(std::vector<Data::SavedStarGift>)> done,
		bool onlyPinnedToTop = false);

	void clearLastRequestTime(not_null<PeerData*> peer);

	void togglePinned(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		const Data::SavedStarGiftId &manageId,
		bool pinned,
		std::shared_ptr<Data::UniqueGift> uniqueData,
		std::shared_ptr<Data::UniqueGift> replacingData = nullptr);

	void reorderPinned(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		int oldPosition,
		int newPosition);

private:
	void updatePinnedOrder(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		const std::vector<SavedStarGift> &gifts,
		const std::vector<Data::SavedStarGiftId> &manageIds,
		Fn<void()> done);

	[[nodiscard]] std::vector<Data::SavedStarGift> filterGifts(
		const std::deque<SavedStarGift> &gifts,
		bool onlyPinnedToTop);

	struct Entry {
		std::deque<SavedStarGift> gifts;
		crl::time lastRequestTime = 0;
		mtpRequestId requestId = 0;
		std::vector<Fn<void()>> pendingCallbacks;
	};

	const not_null<Main::Session*> _session;

	base::flat_map<PeerId, Entry> _recent;

};

} // namespace Data
