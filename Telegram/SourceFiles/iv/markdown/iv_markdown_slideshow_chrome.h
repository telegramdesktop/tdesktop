/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "ui/painter.h"

#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QPainterPath>

namespace style {
struct MarkdownGroupedMedia;
} // namespace style

namespace Iv::Markdown {

[[nodiscard]] int MediaHeightForWidth(
	int width,
	int aspectWidth,
	int aspectHeight);

[[nodiscard]] QPainterPath RoundedRectPath(QRect rect, int radius);

void PaintRoundButton(
	Painter &p,
	QRect rect,
	const style::color &bg,
	const style::icon &icon);

[[nodiscard]] QImage PrepareWithBlurredBackground(
	QSize outer,
	QSize inner,
	QImage large,
	QImage blurred);

[[nodiscard]] int SlideshowFrameHeight(
	int width,
	int slideshowMinHeight,
	gsl::span<const QSize> slideOriginalSizes);

struct SlideshowNavRects {
	QRect previous;
	QRect next;
};

[[nodiscard]] SlideshowNavRects ComputeSlideshowNavRects(
	QRect frame,
	int frameHeight,
	int navButtonSize,
	int navButtonSkip);

struct SlideshowDotsGeometry {
	QRect core;
	QRect outer;
	int first = 0;
	int visible = 0;
};

struct SlideshowDotsBackdrop {
	QImage image;
	QColor color;
};

[[nodiscard]] SlideshowDotsGeometry ComputeSlideshowDots(
	QRect media,
	int count,
	int active,
	const style::MarkdownGroupedMedia &st);

void PaintSlideshowDots(
	Painter &p,
	const SlideshowDotsGeometry &dots,
	int active,
	const style::MarkdownGroupedMedia &st,
	SlideshowDotsBackdrop &backdrop);

} // namespace Iv::Markdown
