/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_subscriptions.h"

namespace Data {

struct UniqueGift;

struct CreditTopupOption final {
	uint64 credits = 0;
	QString product;
	QString currency;
	uint64 amount = 0;
	bool extended = false;
	uint64 giftBarePeerId = 0;
};

using CreditTopupOptions = std::vector<CreditTopupOption>;

enum class CreditsHistoryMediaType {
	Photo,
	Video,
};

struct CreditsHistoryMedia {
	CreditsHistoryMediaType type = CreditsHistoryMediaType::Photo;
	uint64 id = 0;
};

struct CreditsHistoryEntry final {
	explicit operator bool() const {
		return !id.isEmpty();
	}

	using PhotoId = uint64;
	enum class PeerType {
		Peer,
		AppStore,
		PlayMarket,
		Fragment,
		Unsupported,
		PremiumBot,
		Ads,
		API,
	};

	QString id;
	QString title;
	TextWithEntities description;
	QDateTime date;
	QDateTime firstSaleDate;
	QDateTime lastSaleDate;
	PhotoId photoId = 0;
	std::vector<CreditsHistoryMedia> extended;
	StarsAmount credits;
	uint64 bareMsgId = 0;
	uint64 barePeerId = 0;
	uint64 bareGiveawayMsgId = 0;
	uint64 bareGiftStickerId = 0;
	uint64 bareGiftOwnerId = 0;
	uint64 bareActorId = 0;
	uint64 bareGiftListPeerId = 0;
	uint64 giftSavedId = 0;
	uint64 stargiftId = 0;
	std::shared_ptr<UniqueGift> uniqueGift;
	StarsAmount starrefAmount;
	int starrefCommission = 0;
	uint64 starrefRecipientId = 0;
	PeerType peerType;
	QDateTime subscriptionUntil;
	QDateTime successDate;
	QString successLink;
	int limitedCount = 0;
	int limitedLeft = 0;
	int starsConverted = 0;
	int starsToUpgrade = 0;
	int starsUpgradedBySender = 0;
	int floodSkip = 0;
	bool converted : 1 = false;
	bool anonymous : 1 = false;
	bool stargift : 1 = false;
	bool giftTransferred : 1 = false;
	bool giftRefunded : 1 = false;
	bool giftUpgraded : 1 = false;
	bool savedToProfile : 1 = false;
	bool fromGiftsList : 1 = false;
	bool fromGiftSlug : 1 = false;
	bool soldOutInfo : 1 = false;
	bool canUpgradeGift : 1 = false;
	bool hasGiftComment : 1 = false;
	bool reaction : 1 = false;
	bool refunded : 1 = false;
	bool pending : 1 = false;
	bool failed : 1 = false;
	bool in : 1 = false;
	bool gift : 1 = false;
};

struct CreditsStatusSlice final {
	using OffsetToken = QString;
	std::vector<CreditsHistoryEntry> list;
	std::vector<SubscriptionEntry> subscriptions;
	StarsAmount balance;
	uint64 subscriptionsMissingBalance = 0;
	bool allLoaded = false;
	OffsetToken token;
	OffsetToken tokenSubscriptions;
};

struct CreditsGiveawayOption final {
	struct Winner final {
		int users = 0;
		uint64 perUserStars = 0;
		bool isDefault = false;
	};
	std::vector<Winner> winners;
	QString storeProduct;
	QString currency;
	uint64 amount = 0;
	uint64 credits = 0;
	int yearlyBoosts = 0;
	bool isExtended = false;
	bool isDefault = false;
};

using CreditsGiveawayOptions = std::vector<CreditsGiveawayOption>;

} // namespace Data
