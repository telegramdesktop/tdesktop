/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/data_star_gift.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {

struct GiftAuctionBidLevel {
	int64 amount = 0;
	int position = 0;
	TimeId date = 0;
};

struct StarGiftAuctionMyState {
	PeerData *to = nullptr;
	int64 minBidAmount = 0;
	int64 bid = 0;
	TimeId date = 0;
	int gotCount = 0;
	bool returned = false;
};

struct GiftAuctionRound {
	int number = 0;
	TimeId duration = 0;
	int extendTop = 0;
	TimeId extendDuration = 0;
};

struct GiftAuctionState {
	std::optional<StarGift> gift;
	StarGiftAuctionMyState my;
	std::vector<GiftAuctionBidLevel> bidLevels;
	std::vector<not_null<UserData*>> topBidders;
	std::vector<GiftAuctionRound> roundParameters;
	crl::time subscribedTill = 0;
	int64 minBidAmount = 0;
	int64 averagePrice = 0;
	TimeId startDate = 0;
	TimeId endDate = 0;
	TimeId nextRoundAt = 0;
	int currentRound = 0;
	int totalRounds = 0;
	int giftsLeft = 0;
	int version = 0;

	[[nodiscard]] bool finished() const {
		return (averagePrice != 0);
	}
};

struct GiftAcquired {
	not_null<PeerData*> to;
	TextWithEntities message;
	TimeId date = 0;
	int64 bidAmount = 0;
	int round = 0;
	int number = 0;
	int position = 0;
	bool nameHidden = false;
};

struct ActiveAuctions {
	std::vector<not_null<GiftAuctionState*>> list;
};

class GiftAuctions final {
public:
	explicit GiftAuctions(not_null<Main::Session*> session);
	~GiftAuctions();

	[[nodiscard]] rpl::producer<GiftAuctionState> state(const QString &slug);

	void apply(const MTPDupdateStarGiftAuctionState &data);
	void apply(const MTPDupdateStarGiftAuctionUserState &data);

	void requestAcquired(
		uint64 giftId,
		Fn<void(std::vector<Data::GiftAcquired>)> done);

	[[nodiscard]] std::optional<Data::UniqueGiftAttributes> attributes(
		uint64 giftId) const;
	void requestAttributes(uint64 giftId, Fn<void()> ready);

	[[nodiscard]] rpl::producer<ActiveAuctions> active() const;
	[[nodiscard]] rpl::producer<bool> hasActiveChanges() const;
	[[nodiscard]] bool hasActive() const;

private:
	struct Entry {
		GiftAuctionState state;
		rpl::event_stream<> changes;
		bool requested = false;
	};
	struct MyStateKey {
		int bid = 0;
		int position = 0;
		int version = 0;

		explicit operator bool() const {
			return bid != 0;
		}
		friend inline bool operator==(MyStateKey, MyStateKey) = default;
	};
	struct Attributes {
		Data::UniqueGiftAttributes lists;
		std::vector<Fn<void()>> waiters;
	};

	void request(const QString &slug);
	Entry *find(uint64 giftId) const;
	void apply(
		not_null<Entry*> entry,
		const MTPStarGiftAuctionState &state);
	void apply(
		not_null<GiftAuctionState*> entry,
		const MTPStarGiftAuctionState &state);
	void apply(
		not_null<Entry*> entry,
		const MTPStarGiftAuctionUserState &state);
	void apply(
		not_null<StarGiftAuctionMyState*> entry,
		const MTPStarGiftAuctionUserState &state);
	void checkSubscriptions();

	[[nodiscard]] MyStateKey myStateKey(const GiftAuctionState &state) const;
	[[nodiscard]] ActiveAuctions collectActive() const;
	[[nodiscard]] uint64 countActiveHash() const;
	void requestActive();

	const not_null<Main::Session*> _session;

	base::Timer _timer;
	base::flat_map<QString, std::unique_ptr<Entry>> _map;
	base::flat_map<uint64, Attributes> _attributes;

	rpl::event_stream<> _activeChanged;
	mtpRequestId _activeRequestId = 0;

	rpl::lifetime _lifetime;

};

[[nodiscard]] int MyAuctionPosition(const GiftAuctionState &state);

} // namespace Data
