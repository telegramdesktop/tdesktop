/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_story.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {

struct StarGift;

inline constexpr auto kWalletCardMaxStickers = 10;

struct WalletCardSticker {
	QString address;
	StoryArea area;
	std::shared_ptr<const StarGift> gift;

	friend bool operator==(
		const WalletCardSticker &,
		const WalletCardSticker &);
};

struct WalletCard {
	QString address;
	int64 balance = 0;
	std::vector<WalletCardSticker> stickers;
	QString backgroundAddress;
	bool stickersComplete = true;

	friend inline bool operator==(
		const WalletCard &,
		const WalletCard &) = default;
};

[[nodiscard]] bool UniqueWalletCardStickers(
	const std::vector<WalletCardSticker> &list);

[[nodiscard]] WalletCard ParseWalletCard(
	not_null<Main::Session*> session,
	const MTPwallet_Card &card);

} // namespace Data
