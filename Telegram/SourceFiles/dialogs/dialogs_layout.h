/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
class Feed;
} // namespace Data

namespace Dialogs {

class Row;
class FakeRow;

namespace Layout {

const style::icon *ChatTypeIcon(
	not_null<PeerData*> peer,
	bool active,
	bool selected);
const style::icon *FeedTypeIcon(
	not_null<Data::Feed*> feed,
	bool active,
	bool selected);

class RowPainter {
public:
	static void paint(
		Painter &p,
		not_null<const Row*> row,
		int fullWidth,
		bool active,
		bool selected,
		bool onlyBackground,
		TimeMs ms);
	static void paint(
		Painter &p,
		not_null<const FakeRow*> row,
		int fullWidth,
		bool active,
		bool selected,
		bool onlyBackground,
		TimeMs ms,
		bool displayUnreadInfo);
	static QRect sendActionAnimationRect(
		int animationWidth,
		int animationHeight,
		int fullWidth,
		bool textUpdated);

};

void paintImportantSwitch(
	Painter &p,
	Mode current,
	int fullWidth,
	bool selected,
	bool onlyBackground);

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
