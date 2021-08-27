/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rect_part.h"

class Painter;

namespace Ui {

class ChatTheme;
class ChatStyle;

struct BubbleSelectionInterval {
	int top = 0;
	int height = 0;
};

struct BubblePattern {
	QPixmap pixmap;
	std::array<QImage, 4> corners;
	QImage tailLeft;
	QImage tailRight;
	mutable QImage cornerTopCache;
	mutable QImage cornerBottomCache;
	mutable QImage tailCache;
};

[[nodiscard]] std::unique_ptr<BubblePattern> PrepareBubblePattern(
	not_null<const style::palette*> st);

struct SimpleBubble {
	not_null<const ChatStyle*> st;
	QRect geometry;
	const BubblePattern *pattern = nullptr;
	QRect patternViewport;
	int outerWidth = 0;
	bool selected = false;
	bool outbg = false;
	RectPart tailSide = RectPart::None;
	RectParts skip = RectPart::None;
};

struct ComplexBubble {
	SimpleBubble simple;
	const std::vector<BubbleSelectionInterval> &selection;
};

void PaintBubble(Painter &p, const SimpleBubble &args);
void PaintBubble(Painter &p, const ComplexBubble &args);

void PaintPatternBubblePart(
	QPainter &p,
	const QRect &viewport,
	const QPixmap &pixmap,
	const QRect &target);

void PaintPatternBubblePart(
	QPainter &p,
	const QRect &viewport,
	const QPixmap &pixmap,
	const QRect &target,
	const QImage &mask,
	QImage &cache);

void PaintPatternBubblePart(
	QPainter &p,
	const QRect &viewport,
	const QPixmap &pixmap,
	const QRect &target,
	Fn<void(Painter&)> paintContent,
	QImage &cache);

} // namespace Ui
