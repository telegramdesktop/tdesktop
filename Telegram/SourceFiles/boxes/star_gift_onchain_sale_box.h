/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
struct UniqueGift;
class SavedStarGiftId;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Settings {
struct GiftWearBoxStyleOverride;
} // namespace Settings

namespace Ui {

enum class OwnOnchainGift : uchar {
	None,
	Held,
	Pending,
	Listed,
};

[[nodiscard]] OwnOnchainGift LookupOwnOnchainGift(
	not_null<Main::Session*> session,
	const Data::UniqueGift &gift);
[[nodiscard]] bool HasOnchainSaleEntries(OwnOnchainGift own);
[[nodiscard]] bool HasPendingGiftSale(
	not_null<Main::Session*> session,
	const Data::UniqueGift &gift);

void ShowOnchainGiftSellBox(
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> gift,
	Settings::GiftWearBoxStyleOverride st);
void ShowOwnGiftUnlistBox(
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> gift,
	Data::SavedStarGiftId savedId);

} // namespace Ui
