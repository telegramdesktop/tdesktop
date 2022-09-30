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

enum class BubbleCornerRounding : uchar {
	Large,
	Small,
	None,
	Tail,
};

struct BubbleRounding {
	BubbleCornerRounding topLeft = BubbleCornerRounding();
	BubbleCornerRounding topRight = BubbleCornerRounding();
	BubbleCornerRounding bottomLeft = BubbleCornerRounding();
	BubbleCornerRounding bottomRight = BubbleCornerRounding();
};

struct BubbleSelectionInterval {
	int top = 0;
	int height = 0;
};

struct BubblePattern {
	QPixmap pixmap;
	std::array<QImage, 4> cornersSmall;
	std::array<QImage, 4> cornersLarge;
	QImage tailLeft;
	QImage tailRight;
	mutable QImage cornerTopSmallCache;
	mutable QImage cornerTopLargeCache;
	mutable QImage cornerBottomSmallCache;
	mutable QImage cornerBottomLargeCache;
	mutable QImage tailCache;
};

[[nodiscard]] std::unique_ptr<BubblePattern> PrepareBubblePattern(
	not_null<const style::palette*> st);
void FinishBubblePatternOnMain(not_null<BubblePattern*> pattern);

struct SimpleBubble {
	not_null<const ChatStyle*> st;
	QRect geometry;
	const BubblePattern *pattern = nullptr;
	QRect patternViewport;
	int outerWidth = 0;
	bool selected = false;
	bool shadowed = true;
	bool outbg = false;
	BubbleRounding rounding;
};

struct ComplexBubble {
	SimpleBubble simple;
	const std::vector<BubbleSelectionInterval> &selection;
};

void PaintBubble(QPainter &p, const SimpleBubble &args);
void PaintBubble(QPainter &p, const ComplexBubble &args);

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
	Fn<void(QPainter&)> paintContent,
	QImage &cache);

} // namespace Ui
