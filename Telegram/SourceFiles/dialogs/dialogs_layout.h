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
class RippleRow;

namespace Layout {

const style::icon *ChatTypeIcon(
	not_null<PeerData*> peer,
	bool active,
	bool selected);
//const style::icon *FeedTypeIcon( // #feed
//	not_null<Data::Feed*> feed,
//	bool active,
//	bool selected);

class RowPainter {
public:
	static void paint(
		Painter &p,
		not_null<const Row*> row,
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
	const RippleRow &row,
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

	UnreadBadgeSizesCount
};
struct UnreadBadgeStyle {
	UnreadBadgeStyle();

	style::align align;
	bool active;
	bool selected;
	bool muted;
	int textTop = 0;
	int size;
	int padding;
	UnreadBadgeSize sizeId;
	style::font font;
};
void paintUnreadCount(
	Painter &p,
	const QString &text,
	int x,
	int y,
	const UnreadBadgeStyle &st,
	int *outUnreadWidth = nullptr);

void clearUnreadBadgesCache();

} // namespace Layout
} // namespace Dialogs
