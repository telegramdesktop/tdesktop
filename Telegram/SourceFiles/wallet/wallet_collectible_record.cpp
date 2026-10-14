/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_collectible_record.h"

#include "api/api_premium.h"
#include "data/data_star_gift.h"

namespace Wallet {

bool operator==(const CollectibleRecord &a, const CollectibleRecord &b) {
	using Base = Gram::NftItem;
	return (static_cast<const Base&>(a) == static_cast<const Base&>(b))
		&& SameServedGift(a, b);
}

bool SameServedGift(
		const CollectibleRecord &a,
		const CollectibleRecord &b) {
	if (!a.gift || !b.gift) {
		return !a.gift && !b.gift;
	}
	return Data::SameServedStarGift(*a.gift, *b.gift);
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
