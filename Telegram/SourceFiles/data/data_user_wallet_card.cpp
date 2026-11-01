/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_user_wallet_card.h"

#include "api/api_premium.h"
#include "base/flat_set.h"
#include "data/data_star_gift.h"
#include "wallet/wallet_address.h"

namespace Data {
namespace {

[[nodiscard]] bool ValidCoordinates(const MTPMediaAreaCoordinates &area) {
	const auto &data = area.data();
	const auto position = [](const MTPdouble &value) {
		return (value.v >= 0.) && (value.v <= 100.);
	};
	const auto extent = [](const MTPdouble &value) {
		return (value.v > 0.) && (value.v <= 100.);
	};
	const auto radius = data.vradius().value_or_empty();
	return position(data.vx())
		&& position(data.vy())
		&& extent(data.vw())
		&& extent(data.vh())
		&& std::isfinite(data.vrotation().v)
		&& std::isfinite(radius)
		&& (radius >= 0.);
}

[[nodiscard]] std::optional<WalletCardSticker> ParseSticker(
		not_null<Main::Session*> session,
		const MTPwallet_CardSticker &sticker) {
	const auto &data = sticker.data();
	const auto &nft = data.vnft().data();
	const auto served = nft.vgift();
	if (!served || !ValidCoordinates(data.vcoordinates())) {
		return std::nullopt;
	}
	auto gift = Api::FromTL(session, *served);
	if (!gift || !gift->unique) {
		return std::nullopt;
	}
	return WalletCardSticker{
		.address = qs(nft.vaddress()),
		.area = ParseArea(data.vcoordinates()),
		.gift = std::make_shared<const StarGift>(std::move(*gift)),
	};
}

[[nodiscard]] QString BackgroundAddress(
		const std::vector<WalletCardSticker> &stickers,
		const MTPwallet_NftItem &background) {
	const auto canonical = Wallet::CanonicalAddress(
		qs(background.data().vaddress()));
	if (canonical.isEmpty()) {
		return QString();
	}
	for (const auto &sticker : stickers) {
		if (Wallet::CanonicalAddress(sticker.address) == canonical) {
			return sticker.address;
		}
	}
	return QString();
}

} // namespace

bool operator==(const WalletCardSticker &a, const WalletCardSticker &b) {
	if (a.address != b.address || a.area != b.area) {
		return false;
	} else if (!a.gift || !b.gift) {
		return !a.gift && !b.gift;
	}
	return SameServedStarGift(*a.gift, *b.gift);
}

bool UniqueWalletCardStickers(const std::vector<WalletCardSticker> &list) {
	auto identities = base::flat_set<QString>();
	for (const auto &sticker : list) {
		auto canonical = Wallet::CanonicalAddress(sticker.address);
		if (canonical.isEmpty()
			|| !identities.emplace(std::move(canonical)).second) {
			return false;
		}
	}
	return true;
}

WalletCard ParseWalletCard(
		not_null<Main::Session*> session,
		const MTPwallet_Card &card) {
	const auto &data = card.data();
	auto result = WalletCard{
		.address = qs(data.vaddress()),
		.balance = int64(data.vbalance().v),
	};
	auto complete = true;
	if (const auto stickers = data.vstickers()) {
		const auto &list = stickers->v;
		const auto limit = kWalletCardMaxStickers;
		complete = (list.size() <= limit);
		for (const auto &sticker : list | ranges::views::take(limit)) {
			if (auto parsed = ParseSticker(session, sticker)) {
				result.stickers.push_back(std::move(*parsed));
			} else {
				complete = false;
				LOG(("API Error: Bad wallet.cardSticker skipped."));
			}
		}
	}
	if (const auto background = data.vbg_nft_item()) {
		result.backgroundAddress = BackgroundAddress(
			result.stickers,
			*background);
		complete = complete && !result.backgroundAddress.isEmpty();
	}
	result.stickersComplete = complete
		&& UniqueWalletCardStickers(result.stickers);
	return result;
}

} // namespace Data
