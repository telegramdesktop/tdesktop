/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "gram/api/gram_api_nft.h"

#include <memory>

namespace Data {
struct StarGift;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Wallet {

struct CollectibleRecord : Gram::NftItem {
	std::shared_ptr<const Data::StarGift> gift;

	friend bool operator==(
		const CollectibleRecord &,
		const CollectibleRecord &);
};

[[nodiscard]] bool SameServedGift(
	const CollectibleRecord &a,
	const CollectibleRecord &b);

[[nodiscard]] bool FillServedGift(
	not_null<Main::Session*> session,
	CollectibleRecord &record,
	const tl::conditional<MTPStarGift> &served);

} // namespace Wallet
