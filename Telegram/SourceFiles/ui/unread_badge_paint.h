/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

enum class UnreadBadgeSize {
	Dialogs,
	MainMenu,
	HistoryToDown,
	StickersPanel,
	StickersBox,
	TouchBar,
	ReactionInDialogs,

	kCount,
};
struct UnreadBadgeStyle {
	UnreadBadgeStyle();

	style::align align = style::al_right;
	bool active = false;
	bool selected = false;
	bool muted = false;
	int textTop = 0;
	int size = 0;
	int padding = 0;
	UnreadBadgeSize sizeId = UnreadBadgeSize::Dialogs;
	style::font font;
};

[[nodiscard]] QSize CountUnreadBadgeSize(
	const QString &unreadCount,
	const UnreadBadgeStyle &st,
	int allowDigits = 0);

QRect PaintUnreadBadge(
	QPainter &p,
	const QString &t,
	int x,
	int y,
	const UnreadBadgeStyle &st,
	int allowDigits = 0);

} // namespace Ui
