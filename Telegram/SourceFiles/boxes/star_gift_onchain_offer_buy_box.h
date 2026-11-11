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
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {

[[nodiscard]] bool CanOfferOnchainGift(
	not_null<Main::Session*> session,
	const Data::UniqueGift &gift);

void ShowOnchainGiftOfferBox(
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> gift,
	int limitedCount,
	int limitedLeft);

} // namespace Ui
