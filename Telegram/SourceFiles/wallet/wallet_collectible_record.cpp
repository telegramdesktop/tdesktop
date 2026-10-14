/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_collectible_record.h"

#include "api/api_premium.h"
#include "data/data_star_gift.h"
#include "ui/chat/chat_style.h"

namespace Wallet {

bool operator==(const CollectibleRecord &a, const CollectibleRecord &b) {
	using Base = Gram::NftItem;
	return (static_cast<const Base&>(a) == static_cast<const Base&>(b))
		&& SameServedGift(a, b);
}

// WHY: each parse allocates the gift's shared parts anew, so the served
// values are compared; what only a later request fills in, like transfer
// dates, is left out, or a box enriching the gift would rebuild its row.
bool SameServedGift(
		const CollectibleRecord &a,
		const CollectibleRecord &b) {
	if (!a.gift || !b.gift) {
		return !a.gift && !b.gift;
	}
	const auto served = [](const Data::StarGift &gift) {
		const auto &unique = *gift.unique;
		const auto &details = unique.originalDetails;
		return std::tie(
			gift.id,
			gift.document,
			gift.releasedBy,
			gift.limitedLeft,
			gift.limitedCount,
			unique.id,
			unique.initialGiftId,
			unique.slug,
			unique.title,
			unique.number,
			unique.giftAddress,
			unique.ownerAddress,
			unique.ownerName,
			unique.ownerId,
			unique.hostId,
			unique.releasedBy,
			unique.themeUser,
			unique.nanoTonForResale,
			unique.starsForResale,
			unique.starsMinOffer,
			unique.craftChancePermille,
			unique.onlyAcceptTon,
			unique.canBeTheme,
			unique.crafted,
			unique.burned,
			unique.model,
			unique.pattern,
			unique.backdrop,
			details.senderId,
			details.recipientId,
			details.date,
			details.message);
	};
	const auto value = [](const Data::UniqueGiftValue &data) {
		return std::tie(data.currency, data.valuePrice, data.valuePriceUsd);
	};
	const auto same = [](const auto &was, const auto &now, auto key) {
		return (!was || !now) ? (!was && !now) : (key(*was) == key(*now));
	};
	const auto &was = *a.gift;
	const auto &now = *b.gift;
	return (served(was) == served(now))
		&& (was.resellTonOnly == now.resellTonOnly)
		&& (was.requirePremium == now.requirePremium)
		&& same(was.unique->value, now.unique->value, value)
		&& same(
			was.unique->peerColor,
			now.unique->peerColor,
			std::identity());
}

bool FillServedGift(
		not_null<Main::Session*> session,
		CollectibleRecord &record,
		const tl::conditional<MTPStarGift> &served) {
	if (!served) {
		return false;
	}
	auto gift = ::Api::FromTL(session, *served);
	if (!gift || !gift->unique) {
		LOG(("Wallet Error: wallet.NftItem gift is not a unique gift."));
		return false;
	}
	record.kind = Gram::NftKind::TelegramGift;
	record.key = gift->unique->slug;
	record.gift = std::make_shared<const Data::StarGift>(std::move(*gift));
	return true;
}

} // namespace Wallet
