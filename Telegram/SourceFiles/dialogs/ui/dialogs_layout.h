/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
} // namespace Ui

namespace Dialogs {
class Row;
class FakeRow;
class BasicRow;
} // namespace Dialogs

namespace Dialogs::Ui {

using namespace ::Ui;

class VideoUserpic;

const style::icon *ChatTypeIcon(
	not_null<PeerData*> peer,
	bool active,
	bool selected);

class RowPainter {
public:
	static void paint(
		Painter &p,
		not_null<const Row*> row,
		VideoUserpic *videoUserpic,
		FilterId filterId,
		int fullWidth,
		bool active,
		bool selected,
		crl::time ms,
		bool paused);
	static void paint(
		Painter &p,
		not_null<const FakeRow*> row,
		int fullWidth,
		bool active,
		bool selected,
		crl::time ms,
		bool paused,
		bool displayUnreadInfo);
	static QRect sendActionAnimationRect(
		int animationLeft,
		int animationWidth,
		int animationHeight,
		int fullWidth,
		bool textUpdated);

};

void PaintCollapsedRow(
	Painter &p,
	const BasicRow &row,
	Data::Folder *folder,
	const QString &text,
	int unread,
	int fullWidth,
	bool selected);

enum UnreadBadgeSize {
	UnreadBadgeInDialogs = 0,
	UnreadBadgeInMainMenu,
	UnreadBadgeInHistoryToDown,
	UnreadBadgeInStickersPanel,
	UnreadBadgeInStickersBox,
	UnreadBadgeInTouchBar,
	UnreadBadgeReactionInDialogs,

	UnreadBadgeSizesCount
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
	UnreadBadgeSize sizeId = UnreadBadgeInDialogs;
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

void clearUnreadBadgesCache();

} // namespace Dialogs::Ui
