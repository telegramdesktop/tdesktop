/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct BoostsOverview final {
	bool isBoosted = false;
	int level = 0;
	int boostCount = 0;
	int currentLevelBoostCount = 0;
	int nextLevelBoostCount = 0;
	int premiumMemberCount = 0;
	float64 premiumMemberPercentage = 0;
};

struct GiftCodeLink final {
	QString text;
	QString link;
	QString slug;
};

struct Boost final {
	bool isGift = false;
	bool isGiveaway = false;
	bool isUnclaimed = false;

	QString id;
	UserId userId = UserId(0);
	FullMsgId giveawayMessage;
	QDateTime date;
	crl::time expiresAt = 0;
	GiftCodeLink giftCodeLink;
	int multiplier = 0;
};

struct BoostsListSlice final {
	struct OffsetToken final {
		QString next;
		bool gifts = false;
	};
	std::vector<Boost> list;
	int multipliedTotal = 0;
	bool allLoaded = false;
	OffsetToken token;
};

struct BoostStatus final {
	BoostsOverview overview;
	BoostsListSlice firstSliceBoosts;
	BoostsListSlice firstSliceGifts;
	QString link;
};

} // namespace Data
