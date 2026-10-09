/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
class palette;
} // namespace style

namespace Ui {

class RpWidget;

void SetWindowPalette(
	not_null<RpWidget*> window,
	Fn<const style::palette*()> resolve);
[[nodiscard]] bool HasWindowPalettes();
[[nodiscard]] const style::palette *WindowPaletteFor(
	not_null<const QWidget*> widget);

} // namespace Ui
