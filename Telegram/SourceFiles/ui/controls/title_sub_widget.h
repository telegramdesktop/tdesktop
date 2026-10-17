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

not_null<RpWidget*> CreateTitleSubWidget(
	not_null<RpWidget*> parent,
	const style::TextStyle &st,
	rpl::producer<QString> text,
	rpl::producer<style::align> align,
	rpl::producer<int> top);

} // namespace Ui
