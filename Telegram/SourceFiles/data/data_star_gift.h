/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct UniqueGiftAttribute {
	QString name;
	int rarityPermille = 0;
};

struct UniqueGiftModel : UniqueGiftAttribute {
	not_null<DocumentData*> document;
};

struct UniqueGiftPattern : UniqueGiftAttribute {
	not_null<DocumentData*> document;
};

struct UniqueGiftBackdrop : UniqueGiftAttribute {
	QColor centerColor;
	QColor edgeColor;
	QColor patternColor;
	QColor textColor;
};

struct UniqueGiftOriginalDetails {
	PeerId senderId = 0;
	PeerId recipientId = 0;
	TimeId date = 0;
	TextWithEntities message;
};

struct UniqueGift {
	QString title;
	PeerId ownerId = 0;
	int number = 0;
	int starsForTransfer = -1;
	TimeId exportAt = 0;
	UniqueGiftModel model;
	UniqueGiftPattern pattern;
	UniqueGiftBackdrop backdrop;
	UniqueGiftOriginalDetails originalDetails;
};

[[nodiscard]] inline QString UniqueGiftName(const UniqueGift &gift) {
	return gift.title + u" #"_q + QString::number(gift.number);
}

struct StarGift {
	uint64 id = 0;
	std::shared_ptr<UniqueGift> unique;
	int64 stars = 0;
	int64 starsConverted = 0;
	int64 starsToUpgrade = 0;
	not_null<DocumentData*> document;
	int limitedLeft = 0;
	int limitedCount = 0;
	TimeId firstSaleDate = 0;
	TimeId lastSaleDate = 0;
	bool upgradable = false;
	bool birthday = false;

	friend inline bool operator==(
		const StarGift &,
		const StarGift &) = default;
};

struct UserStarGift {
	StarGift info;
	TextWithEntities message;
	int64 starsConverted = 0;
	int64 starsUpgradedBySender = 0;
	PeerId fromId = 0;
	MsgId messageId = 0;
	TimeId date = 0;
	bool upgradable = false;
	bool anonymous = false;
	bool hidden = false;
	bool mine = false;
};

} // namespace Data
