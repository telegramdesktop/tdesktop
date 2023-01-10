/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/cached_round_corners.h"

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

struct TopicJumpCorners {
	CornersPixmaps normal;
	CornersPixmaps inverted;
	QPixmap small;
	int invertedRadius = 0;
	int smallKey = 0; // = `-radius` if top right else `radius`.
};

struct TopicJumpCache {
	TopicJumpCorners corners;
	TopicJumpCorners over;
	TopicJumpCorners selected;
	TopicJumpCorners rippleMask;
};

struct PaintContext {
	not_null<const style::DialogRow*> st;
	TopicJumpCache *topicJumpCache = nullptr;
	Data::Folder *folder = nullptr;
	Data::Forum *forum = nullptr;
	required<QBrush> currentBg;
	FilterId filter = 0;
	float64 topicsExpanded = 0.;
	crl::time now = 0;
	int width = 0;
	bool active = false;
	bool selected = false;
	bool topicJumpSelected = false;
	bool paused = false;
	bool search = false;
	bool narrow = false;
	bool displayUnreadInfo = false;
};

[[nodiscard]] const style::icon *ChatTypeIcon(
	not_null<PeerData*> peer,
	const PaintContext &context);
[[nodiscard]] const style::icon *ChatTypeIcon(not_null<PeerData*> peer);

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

} // namespace Dialogs::Ui
