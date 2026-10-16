/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_message_reaction_id.h"
#include "data/data_star_gift.h"

namespace Data {

struct ShareThemeDefault {
	friend inline bool operator==(
		const ShareThemeDefault &,
		const ShareThemeDefault &) = default;
};

struct ShareThemeEmoji {
	std::vector<ReactionId> emojis;

	friend inline bool operator==(
		const ShareThemeEmoji &,
		const ShareThemeEmoji &) = default;
};

struct ShareThemeColor {
	int colorId = 0;

	friend inline bool operator==(
		const ShareThemeColor &,
		const ShareThemeColor &) = default;
};

struct ShareThemeBackdrop {
	int backdropId = 0;

	friend inline bool operator==(
		const ShareThemeBackdrop &,
		const ShareThemeBackdrop &) = default;
};

struct ShareThemeGift {
	StarGift gift;
	SavedStarGiftId savedId;

	friend inline bool operator==(
			const ShareThemeGift &a,
			const ShareThemeGift &b) {
		return (a.gift.unique && b.gift.unique)
			? (a.gift.unique->id == b.gift.unique->id)
			: (a.gift.id == b.gift.id);
	}
};

using ShareTheme = std::variant<
	ShareThemeDefault,
	ShareThemeEmoji,
	ShareThemeColor,
	ShareThemeBackdrop,
	ShareThemeGift>;

} // namespace Data
