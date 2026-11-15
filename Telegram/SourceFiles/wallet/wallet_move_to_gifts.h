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
class SessionShow;
} // namespace Main

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Wallet {

struct CollectibleTransfer;

[[nodiscard]] bool AddMoveToMyGiftsRow(
	not_null<Ui::VerticalLayout*> container,
	std::shared_ptr<Main::SessionShow> show,
	std::shared_ptr<const CollectibleTransfer> collectible,
	rpl::producer<bool> hidden);

void ShowAddressHeldGiftTransfer(
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> unique,
	Fn<void()> locked);

} // namespace Wallet
