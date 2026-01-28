/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
struct UniqueGift;
} // namespace Data

namespace Info::PeerGifts {
struct GiftTypeStars;
} // namespace Info::PeerGifts

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {

void ShowResaleGiftBoughtToast(
	std::shared_ptr<Main::SessionShow> show,
	not_null<PeerData*> to,
	const Data::UniqueGift &gift);

[[nodiscard]] rpl::lifetime ShowStarGiftResale(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	uint64 giftId,
	QString title,
	Fn<void()> finishRequesting);

} // namespace Ui
