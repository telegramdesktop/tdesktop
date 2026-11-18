/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
struct UniqueGift;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {

struct OnchainBuyMemory final : base::has_weak_ptr {
	bool withdrawalDisabled = false;
};

enum class OnchainBuyRefusal : uchar {
	None,
	SaleFlagsInvalid,
	NotOnchainLot,
	WalletUnavailable,
	SupportMode,
	SlugMissing,
	OwnGift,
};

[[nodiscard]] OnchainBuyRefusal LookupOnchainBuyRefusal(
	not_null<Main::Session*> session,
	const Data::UniqueGift &gift);
[[nodiscard]] QString OnchainBuyRefusalText(OnchainBuyRefusal refusal);
[[nodiscard]] bool CanBuyOnchainGift(
	not_null<PeerData*> recipient,
	const Data::UniqueGift &gift);

void ShowBuyOnchainGiftBox(
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> gift,
	not_null<PeerData*> to,
	base::weak_ptr<OnchainBuyMemory> memory,
	Fn<void()> bought);

} // namespace Ui
