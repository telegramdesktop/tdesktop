/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
namespace internal {
class Icon;
} // namespace internal
} // namespace style

namespace Data {
class ChatFilter;
} // namespace Data

namespace Ui {

enum class FilterIcon : uchar {
	Cat,
	Book,
	Money,
	// Camera,
	Game,
	// House,
	Light,
	Like,
	// Plus,
	Note,
	Palette,
	Travel,
	Sport,
	Favorite,
	Study,
	Airplane,
	// Microbe,
	// Worker,
	Private,
	Groups,
	All,
	Unread,
	// Check,
	Bots,
	// Folders,
	Crown,
	Flower,
	Home,
	Love,
	Mask,
	Party,
	Trade,
	Work,
	Unmuted,
	Channels,
	Custom,
	Setup,
	// Poo,

	Edit,
};

struct FilterIcons {
	not_null<const style::internal::Icon*> normal;
	not_null<const style::internal::Icon*> active;
	QString emoji;
};

[[nodiscard]] const FilterIcons &LookupFilterIcon(FilterIcon icon);
[[nodiscard]] std::optional<FilterIcon> LookupFilterIconByEmoji(
	const QString &emoji);

[[nodiscard]] FilterIcon ComputeDefaultFilterIcon(
	const Data::ChatFilter &filter);
[[nodiscard]] FilterIcon ComputeFilterIcon(const Data::ChatFilter &filter);

} // namespace Ui
