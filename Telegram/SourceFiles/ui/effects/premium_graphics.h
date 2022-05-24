/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtGui/QBrush>

namespace tr {
template <typename ...>
struct phrase;
} // namespace tr

enum lngtag_count : int;

namespace Ui {

class VerticalLayout;

namespace Premium {

void AddBubbleRow(
	not_null<Ui::VerticalLayout*> parent,
	rpl::producer<> showFinishes,
	int min,
	int current,
	int max,
	std::optional<tr::phrase<lngtag_count>> phrase,
	const style::icon *icon);

void AddLimitRow(
	not_null<Ui::VerticalLayout*> parent,
	int max,
	std::optional<tr::phrase<lngtag_count>> phrase);

[[nodiscard]] QGradientStops LimitGradientStops();
[[nodiscard]] QGradientStops ButtonGradientStops();
[[nodiscard]] QGradientStops LockGradientStops();

} // namespace Premium
} // namespace Ui
