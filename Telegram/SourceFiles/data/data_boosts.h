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

struct Boost final {
	UserId userId = UserId(0);
	QDateTime expirationDate;
};

struct BoostsListSlice final {
	struct OffsetToken final {
		QString next;
	};
	std::vector<Boost> list;
	int total = 0;
	bool allLoaded = false;
	OffsetToken token;
};

struct BoostStatus final {
	BoostsOverview overview;
	BoostsListSlice firstSlice;
	QString link;
};

} // namespace Data
