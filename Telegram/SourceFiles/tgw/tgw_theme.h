/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace twidget {
struct WidgetTheme;
} // namespace twidget

namespace Ui {
class ChatStyle;
struct ChatPaintContext;
} // namespace Ui

namespace Tgw {

struct ThemeKey {
	const Ui::ChatStyle *style = nullptr;
	QRgb bubble = 0;
	QRgb under = 0;
	int paletteVersion = 0;
	bool outbg = false;

	friend inline bool operator==(
		const ThemeKey &,
		const ThemeKey &) = default;
};

[[nodiscard]] ThemeKey BubbleThemeKey(const Ui::ChatPaintContext &context);
[[nodiscard]] std::shared_ptr<const twidget::WidgetTheme> BubbleTheme(
	const Ui::ChatPaintContext &context);
[[nodiscard]] bool SameTheme(
	const twidget::WidgetTheme &a,
	const twidget::WidgetTheme &b);

} // namespace Tgw
