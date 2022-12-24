/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
struct DialogRow;
} // namespace style

namespace st {
extern const style::DialogRow &defaultDialogRow;
} // namespace st

namespace Ui {
} // namespace Ui

namespace Data {
class Forum;
class Folder;
} // namespace Data

namespace Dialogs {
class Row;
class FakeRow;
class BasicRow;
} // namespace Dialogs

namespace Dialogs::Ui {

using namespace ::Ui;

class VideoUserpic;

struct PaintContext {
	not_null<const style::DialogRow*> st;
	Data::Folder *folder = nullptr;
	Data::Forum *forum = nullptr;
	FilterId filter = 0;
	crl::time now = 0;
	int width = 0;
	bool active = false;
	bool selected = false;
	bool paused = false;
	bool search = false;
	bool narrow = false;
	bool displayUnreadInfo = false;
};

const style::icon *ChatTypeIcon(
	not_null<PeerData*> peer,
	const PaintContext &context = { .st = &st::defaultDialogRow });

class RowPainter {
public:
	static void Paint(
		Painter &p,
		not_null<const Row*> row,
		VideoUserpic *videoUserpic,
		const PaintContext &context);
	static void Paint(
		Painter &p,
		not_null<const FakeRow*> row,
		const PaintContext &context);
	static QRect SendActionAnimationRect(
		not_null<const style::DialogRow*> st,
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
	const PaintContext &context);

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

} // namespace Dialogs::Ui
