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

namespace Ui {
struct ColorCollectible;
} // namespace Ui

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
	int id = 0;
};

struct UniqueGiftOriginalDetails {
	PeerId senderId = 0;
	PeerId recipientId = 0;
	TimeId date = 0;
	TextWithEntities message;
};

struct UniqueGiftValue {
	QString currency;
	int64 valuePrice = 0;
	CreditsAmount initialPriceStars;
	int64 initialSalePrice = 0;
	TimeId initialSaleDate = 0;
	int64 lastSalePrice = 0;
	TimeId lastSaleDate = 0;
	int64 averagePrice = 0;
	int64 minimumPrice = 0;
	int forSaleOnTelegram = 0;
	int forSaleOnFragment = 0;
	QString fragmentUrl;
	bool lastSaleFragment = false;
};

struct UniqueGift {
	CollectibleId id = 0;
	uint64 initialGiftId = 0;
	QString slug;
	QString title;
	QString giftAddress;
	QString ownerAddress;
	QString ownerName;
	PeerId ownerId = 0;
	PeerId hostId = 0;
	PeerData *releasedBy = nullptr;
	PeerData *themeUser = nullptr;
	int64 nanoTonForResale = -1;
	int starsForResale = -1;
	int starsForTransfer = -1;
	int number = 0;
	bool onlyAcceptTon = false;
	bool canBeTheme = false;
	TimeId exportAt = 0;
	TimeId canTransferAt = 0;
	TimeId canResellAt = 0;
	UniqueGiftModel model;
	UniqueGiftPattern pattern;
	UniqueGiftBackdrop backdrop;
	UniqueGiftOriginalDetails originalDetails;
	std::shared_ptr<UniqueGiftValue> value;
	std::shared_ptr<Ui::ColorCollectible> peerColor;
};

[[nodiscard]] QString UniqueGiftName(const UniqueGift &gift);

[[nodiscard]] CreditsAmount UniqueGiftResaleStars(const UniqueGift &gift);
[[nodiscard]] CreditsAmount UniqueGiftResaleTon(const UniqueGift &gift);
[[nodiscard]] CreditsAmount UniqueGiftResaleAsked(const UniqueGift &gift);

[[nodiscard]] TextWithEntities FormatGiftResaleStars(const UniqueGift &gift);
[[nodiscard]] TextWithEntities FormatGiftResaleTon(const UniqueGift &gift);
[[nodiscard]] TextWithEntities FormatGiftResaleAsked(const UniqueGift &gift);

struct StarGift {
	uint64 id = 0;
	std::shared_ptr<UniqueGift> unique;
	int64 stars = 0;
	int64 starsConverted = 0;
	int64 starsToUpgrade = 0;
	int64 starsResellMin = 0;
	not_null<DocumentData*> document;
	PeerData *releasedBy = nullptr;
	QString resellTitle;
	int resellCount = 0;
	int limitedLeft = 0;
	int limitedCount = 0;
	int perUserTotal = 0;
	int perUserRemains = 0;
	TimeId firstSaleDate = 0;
	TimeId lastSaleDate = 0;
	TimeId lockedUntilDate = 0;
	bool resellTonOnly : 1 = false;
	bool requirePremium : 1 = false;
	bool peerColorAvailable : 1 = false;
	bool upgradable : 1 = false;
	bool birthday : 1 = false;
	bool soldOut : 1 = false;

	friend inline bool operator==(
		const StarGift &,
		const StarGift &) = default;
};

class SavedStarGiftId {
public:
	[[nodiscard]] static SavedStarGiftId User(MsgId messageId) {
		auto result = SavedStarGiftId();
		result.entityId = uint64(messageId.bare);
		return result;
	}
	[[nodiscard]] static SavedStarGiftId Chat(
			not_null<PeerData*> peer,
			uint64 savedId) {
		auto result = SavedStarGiftId();
		result.peer = peer;
		result.entityId = savedId;
		return result;
	}

	[[nodiscard]] bool isUser() const {
		return !peer;
	}
	[[nodiscard]] bool isChat() const {
		return peer != nullptr;
	}

	[[nodiscard]] MsgId userMessageId() const {
		return peer ? MsgId(0) : MsgId(entityId);
	}
	[[nodiscard]] PeerData *chat() const {
		return peer;
	}
	[[nodiscard]] uint64 chatSavedId() const {
		return peer ? entityId : 0;
	}

	explicit operator bool() const {
		return entityId != 0;
	}

	friend inline bool operator==(
		const SavedStarGiftId &,
		const SavedStarGiftId &) = default;
	friend inline auto operator<=>(
		const SavedStarGiftId &,
		const SavedStarGiftId &) = default;

private:
	PeerData *peer = nullptr;
	uint64 entityId = 0;

};

struct SavedStarGift {
	StarGift info;
	SavedStarGiftId manageId;
	std::vector<int> collectionIds;
	TextWithEntities message;
	int64 starsConverted = 0;
	int64 starsUpgradedBySender = 0;
	int64 starsForDetailsRemove = 0;
	QString giftPrepayUpgradeHash;
	PeerId fromId = 0;
	TimeId date = 0;
	bool upgradeSeparate = false;
	bool upgradable = false;
	bool anonymous = false;
	bool pinned = false;
	bool hidden = false;
	bool mine = false;
};

struct GiftCollection {
	int id = 0;
	int count = 0;
	QString title;
	DocumentData *icon = nullptr;
	uint64 hash = 0;
};

struct UniqueGiftModelCount {
	UniqueGiftModel model;
	int count = 0;
};

struct UniqueGiftBackdropCount {
	UniqueGiftBackdrop backdrop;
	int count = 0;
};

struct UniqueGiftPatternCount {
	UniqueGiftPattern pattern;
	int count = 0;
};

enum class ResaleGiftsSort {
	Date,
	Price,
	Number,
};

enum class GiftAttributeIdType {
	Model,
	Pattern,
	Backdrop,
};

struct GiftAttributeId {
	uint64 value = 0;
	GiftAttributeIdType type = GiftAttributeIdType::Model;

	friend inline auto operator<=>(
		GiftAttributeId,
		GiftAttributeId) = default;
	friend inline bool operator==(
		GiftAttributeId,
		GiftAttributeId) = default;
};

[[nodiscard]] GiftAttributeId IdFor(const UniqueGiftBackdrop &value);
[[nodiscard]] GiftAttributeId IdFor(const UniqueGiftModel &value);
[[nodiscard]] GiftAttributeId IdFor(const UniqueGiftPattern &value);

struct MyGiftsDescriptor {
	std::vector<SavedStarGift> list;
	QString offset;
};

enum class MyUniqueType {
	OwnedAndHosted,
	OnlyOwned,
};

[[nodiscard]] rpl::producer<MyGiftsDescriptor> MyUniqueGiftsSlice(
	not_null<Main::Session*> session,
	MyUniqueType type,
	QString offset = QString());

struct ResaleGiftsDescriptor {
	uint64 giftId = 0;
	QString title;
	QString offset;
	std::vector<StarGift> list;
	std::vector<UniqueGiftModelCount> models;
	std::vector<UniqueGiftBackdropCount> backdrops;
	std::vector<UniqueGiftPatternCount> patterns;
	uint64 attributesHash = 0;
	int count = 0;
	ResaleGiftsSort sort = ResaleGiftsSort::Date;
};

struct ResaleGiftsFilter {
	uint64 attributesHash = 0;
	base::flat_set<GiftAttributeId> attributes;
	ResaleGiftsSort sort = ResaleGiftsSort::Price;

	friend inline bool operator==(
		const ResaleGiftsFilter &,
		const ResaleGiftsFilter &) = default;
};

[[nodiscard]] rpl::producer<ResaleGiftsDescriptor> ResaleGiftsSlice(
	not_null<Main::Session*> session,
	uint64 giftId,
	ResaleGiftsFilter filter = {},
	QString offset = QString());

} // namespace Data
