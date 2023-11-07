/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
struct TextStyle;
} // namespace style

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Info::Statistics {

[[nodiscard]] QImage CreateBadge(
	const style::TextStyle &textStyle,
	const QString &text,
	int badgeHeight,
	const style::margins &textPadding,
	const style::color &bg,
	const style::color &fg,
	float64 bgOpacity,
	const style::margins &iconPadding,
	const style::icon &icon);

[[nodiscard]] not_null<Ui::RpWidget*> InfiniteRadialAnimationWidget(
	not_null<Ui::RpWidget*> parent,
	int size);

void AddLabelWithBadgeToButton(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<QString> text,
	rpl::producer<int> number,
	rpl::producer<bool> shown);

} // namespace Info::Statistics
