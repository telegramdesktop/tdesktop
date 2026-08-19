/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Calls::Group::Ui {

using namespace ::Ui;

struct StarsColoring {
	int bgLight = 0;
	int bgDark = 0;
	int fromStars = 0;
	TimeId secondsPin = 0;
	int charactersMax = 0;
	int emojiLimit = 0;

	friend inline auto operator<=>(
		const StarsColoring &,
		const StarsColoring &) = default;
	friend inline bool operator==(
		const StarsColoring &,
		const StarsColoring &) = default;
};

[[nodiscard]] StarsColoring StarsColoringForCount(
	const std::vector<StarsColoring> &colorings,
	int stars);

[[nodiscard]] int StarsRequiredForMessage(
	const std::vector<StarsColoring> &colorings,
	const TextWithTags &text);

[[nodiscard]] object_ptr<Ui::RpWidget> VideoStreamStarsLevel(
	not_null<Ui::RpWidget*> box,
	const std::vector<StarsColoring> &colorings,
	rpl::producer<int> starsValue);

} // namespace Calls::Group::Ui
