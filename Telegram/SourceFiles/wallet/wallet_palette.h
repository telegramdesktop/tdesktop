/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/style/style_core_palette.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window::Theme {
enum class EmbeddedType;
} // namespace Window::Theme

namespace Wallet {

[[nodiscard]] QColor CardMatchedAccent(Window::Theme::EmbeddedType type);
[[nodiscard]] const style::palette *WindowPalette();
void UseWindowPalette(not_null<Ui::RpWidget*> window);
[[nodiscard]] style::main_palette::Override WindowPaletteScope(
	not_null<const QWidget*> widget);

} // namespace Wallet
