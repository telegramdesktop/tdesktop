/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/peer_qr_colors_panel.h"

class PeerData;

namespace Data {
struct UniqueGift;
struct ShareThemeGift;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Info::Profile {

struct QrServedTheme {
	Ui::PeerQrSwatch swatch;
	bool kept = false;

	friend inline bool operator==(
		const QrServedTheme &,
		const QrServedTheme &) = default;
};

[[nodiscard]] auto QrThemeSwatchesValue(not_null<Main::Session*> session)
-> rpl::producer<std::vector<Ui::PeerQrSwatch>>;

[[nodiscard]] Ui::PeerQrSwatch QrGiftSwatch(const Data::UniqueGift &gift);

[[nodiscard]] rpl::producer<QrServedTheme> QrServedThemeValue(
	not_null<PeerData*> peer);

[[nodiscard]] rpl::producer<bool> QrThemeCanSaveValue(
	not_null<PeerData*> peer);

void SaveQrTheme(
	not_null<PeerData*> peer,
	Ui::PeerQrThemeKey key,
	Fn<void()> done,
	Fn<void(QString)> fail);

void SaveQrGiftTheme(
	not_null<PeerData*> peer,
	const Data::ShareThemeGift &theme,
	Fn<void()> done,
	Fn<void(QString)> fail);

} // namespace Info::Profile
