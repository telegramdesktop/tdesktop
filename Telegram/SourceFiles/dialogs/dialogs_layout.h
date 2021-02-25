/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Dialogs {

class Row;
class FakeRow;
class BasicRow;

namespace Layout {

const style::icon *ChatTypeIcon(
	not_null<PeerData*> peer,
	bool active,
	bool selected);

class RowPainter {
public:
	static void paint(
		Painter &p,
		not_null<const Row*> row,
		FilterId filterId,
		int fullWidth,
		bool active,
		bool selected,
		crl::time ms);
	static void paint(
		Painter &p,
		not_null<const FakeRow*> row,
		int fullWidth,
		bool active,
		bool selected,
		crl::time ms,
		bool displayUnreadInfo);
	static QRect sendActionAnimationRect(
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
	UnreadBadgeInHistoryToDown,
	UnreadBadgeInStickersPanel,
	UnreadBadgeInStickersBox,
	UnreadBadgeInTouchBar,

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
void paintUnreadCount(
	Painter &p,
	const QString &t,
	int x,
	int y,
	const UnreadBadgeStyle &st,
	int *outUnreadWidth = nullptr,
	int allowDigits = 0);

void clearUnreadBadgesCache();

} // namespace Layout
} // namespace Dialogs
