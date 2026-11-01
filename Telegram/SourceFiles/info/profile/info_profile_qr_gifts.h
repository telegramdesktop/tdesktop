/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/peer_qr_colors_panel.h"
#include "ui/effects/peer_qr_gift_field.h"

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Info::Profile {

struct QrGiftsSlice {
	std::vector<Ui::PeerQrSwatch> gifts;
	bool complete = false;
};

struct QrGiftList {
	rpl::producer<QrGiftsSlice> slices;
	rpl::producer<bool> browseShown;
	Fn<void()> refresh;
	Fn<void()> loadMore;
	Fn<void()> browse;
	Fn<void(
		const Ui::PeerQrThemeKey&,
		Fn<void()>,
		Fn<void(QString)>)> save;
};

[[nodiscard]] QrGiftList CreateQrGiftList(
	not_null<PeerData*> peer,
	Window::SessionController *controller,
	rpl::lifetime &lifetime);

[[nodiscard]] rpl::producer<Ui::PeerQrGiftSource> QrGiftSourceValue(
	not_null<Main::Session*> session);

} // namespace Info::Profile
