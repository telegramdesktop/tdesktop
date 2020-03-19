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
	All,
	Unread,
	Unmuted,
	Bots,
	Channels,
	Groups,
	Private,
	Custom,
	Setup,

	Cat,
	Crown,
	Favorite,
	Flower,
	Game,
	Home,
	Love,
	Mask,
	Party,
	Sport,
	Study,
	Trade,
	Travel,
	Work,
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
