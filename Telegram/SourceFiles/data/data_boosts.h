/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct BoostsOverview final {
	int mine = 0;
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
	QDateTime expiresAt;
	int expiresAfterMonths = 0;
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

struct BoostPrepaidGiveaway final {
	int months = 0;
	uint64 id = 0;
	int quantity = 0;
	QDateTime date;
};

struct BoostStatus final {
	BoostsOverview overview;
	BoostsListSlice firstSliceBoosts;
	BoostsListSlice firstSliceGifts;
	std::vector<BoostPrepaidGiveaway> prepaidGiveaway;
	QString link;
};

} // namespace Data
