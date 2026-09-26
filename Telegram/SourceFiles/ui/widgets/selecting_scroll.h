/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

class RpWidget;

void SetupSelectingScroll(
	not_null<RpWidget*> widget,
	Fn<void(int pixels)> scrollBy);

} // namespace Ui
