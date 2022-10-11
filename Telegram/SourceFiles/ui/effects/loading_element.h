/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

template <typename Object>
class object_ptr;

namespace style {
struct FlatLabel;
} // namespace style

namespace Ui {

class RpWidget;

object_ptr<Ui::RpWidget> CreateLoadingTextWidget(
	not_null<Ui::RpWidget*> parent,
	const style::FlatLabel &st,
	int lines,
	rpl::producer<bool> rtl);

} // namespace Ui
