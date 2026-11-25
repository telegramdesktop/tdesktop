/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_collectible_record.h"

#include "api/api_premium.h"
#include "data/data_star_gift.h"
#include "wallet/wallet_address.h"

namespace Wallet {
namespace {

// A row whose served gift names another TON DNS owner is older than the gift.
[[nodiscard]] bool OtherNamedOwner(
		const CollectibleRecord &record,
		const Data::UniqueGift &gift) {
	const auto served = record.gift ? record.gift->unique.get() : nullptr;
	if (!served
		|| served->ownerAddress.isEmpty()
		|| gift.ownerAddress.isEmpty()
		|| !CanonicalAddress(served->ownerAddress).isEmpty()
		|| !CanonicalAddress(gift.ownerAddress).isEmpty()) {
		return false;
	}
	const auto &name = served->ownerAddress;
	return name.compare(gift.ownerAddress, Qt::CaseInsensitive) != 0;
}

} // namespace

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

bool OnOwnSale(const CollectibleRecord &record) {
	return record.gift
		&& record.gift->unique
		&& record.gift->unique->mySale;
}

const CollectibleRecord *FindListedGift(
		const std::vector<CollectibleRecord> &list,
		const Data::UniqueGift &gift) {
	const auto address = CanonicalAddress(gift.giftAddress);
	const auto matches = [&](const CollectibleRecord &item) {
		if (!address.isEmpty()) {
			const auto &stored = item.address;
			return !stored.compare(address, Qt::CaseInsensitive);
		}
		return !gift.slug.isEmpty()
			&& (item.kind == Gram::NftKind::TelegramGift)
			&& !item.key.compare(gift.slug, Qt::CaseInsensitive);
	};
	const auto i = ranges::find_if(list, matches);
	return (i != end(list) && !OtherNamedOwner(*i, gift)) ? &*i : nullptr;
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
