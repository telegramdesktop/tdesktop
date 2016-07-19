/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "history/history_service_layout.h"

#include "data/data_abstract_structure.h"
#include "styles/style_history.h"
#include "mainwidget.h"
#include "lang.h"

namespace HistoryLayout {
namespace {

enum CircleMask {
	NormalMask     = 0x00,
	InvertedMask   = 0x01,
};
enum CircleMaskMultiplier {
	MaskMultiplier = 0x04,
};
enum CornerVerticalSide {
	CornerTop      = 0x00,
	CornerBottom   = 0x02,
};
enum CornerHorizontalSide {
	CornerLeft     = 0x00,
	CornerRight    = 0x01,
};

class ServiceMessageStyleData : public Data::AbstractStructure {
public:
	// circle[CircleMask value]
	QImage circle[2];

	// corners[(CircleMask value) * MaskMultiplier | (CornerVerticalSide value) | (CornerHorizontalSide value)]
	QPixmap corners[8];
};
Data::GlobalStructurePointer<ServiceMessageStyleData> serviceMessageStyle;

void createCircleMasks() {
	serviceMessageStyle.createIfNull();
	if (!serviceMessageStyle->circle[NormalMask].isNull()) return;

	int size = st::historyServiceMsgRadius * 2;
	serviceMessageStyle->circle[NormalMask] = style::createCircleMask(size);
	int sizeInverted = st::historyServiceMsgInvertedRadius * 2;
	serviceMessageStyle->circle[InvertedMask] = style::createInvertedCircleMask(sizeInverted);
}

QPixmap circleCorner(int corner) {
	if (serviceMessageStyle->corners[corner].isNull()) {
		int maskType = corner / MaskMultiplier;
		int radius = (maskType == NormalMask ? st::historyServiceMsgRadius : st::historyServiceMsgInvertedRadius);
		int size = radius * cIntRetinaFactor();

		int xoffset = 0, yoffset = 0;
		if (corner & CornerRight) {
			xoffset = size;
		}
		if (corner & CornerBottom) {
			yoffset = size;
		}
		auto part = QRect(xoffset, yoffset, size, size);
		auto result = style::colorizeImage(serviceMessageStyle->circle[maskType], App::msgServiceBg(), part);
		result.setDevicePixelRatio(cRetinaFactor());
		serviceMessageStyle->corners[corner] = App::pixmapFromImageInPlace(std_::move(result));
	}
	return serviceMessageStyle->corners[corner];
}

enum class SideStyle {
	Rounded,
	Plain,
	Inverted,
};

// Returns amount of pixels already painted vertically (so you can skip them in the complex rect shape).
int paintBubbleSide(Painter &p, int x, int y, int width, SideStyle style, CornerVerticalSide side) {
	if (style == SideStyle::Rounded) {
		auto left = circleCorner((NormalMask * MaskMultiplier) | side | CornerLeft);
		int leftWidth = left.width() / cIntRetinaFactor();
		p.drawPixmap(x, y, left);

		auto right = circleCorner((NormalMask * MaskMultiplier) | side | CornerRight);
		int rightWidth = right.width() / cIntRetinaFactor();
		p.drawPixmap(x + width - rightWidth, y, right);

		int cornerHeight = left.height() / cIntRetinaFactor();
		p.fillRect(x + leftWidth, y, width - leftWidth - rightWidth, cornerHeight, App::msgServiceBg());
		return cornerHeight;
	} else if (style == SideStyle::Inverted) {
		// CornerLeft and CornerRight are inverted for SideStyle::Inverted sprites.
		auto left = circleCorner((InvertedMask * MaskMultiplier) | side | CornerRight);
		int leftWidth = left.width() / cIntRetinaFactor();
		p.drawPixmap(x - leftWidth, y, left);

		auto right = circleCorner((InvertedMask * MaskMultiplier) | side | CornerLeft);
		p.drawPixmap(x + width, y, right);
	}
	return 0;
}

void paintBubblePart(Painter &p, int x, int y, int width, int height, SideStyle topStyle, SideStyle bottomStyle) {
	if (topStyle == SideStyle::Inverted || bottomStyle == SideStyle::Inverted) {
		width -= st::historyServiceMsgInvertedShrink * 2;
		x += st::historyServiceMsgInvertedShrink;
	}

	if (int skip = paintBubbleSide(p, x, y, width, topStyle, CornerTop)) {
		y += skip;
		height -= skip;
	}
	int bottomSize = 0;
	if (bottomStyle == SideStyle::Rounded) {
		bottomSize = st::historyServiceMsgRadius;
	} else if (bottomStyle == SideStyle::Inverted) {
		bottomSize = st::historyServiceMsgInvertedRadius;
	}
	if (int skip = paintBubbleSide(p, x, y + height - bottomSize, width, bottomStyle, CornerBottom)) {
		height -= skip;
	}

	p.fillRect(x, y, width, height, App::msgServiceBg());
}

void paintPreparedDate(Painter &p, const QString &dateText, int dateTextWidth, int y, int w) {
	int left = st::msgServiceMargin.left();
	int maxwidth = w;
	if (Adaptive::Wide()) {
		maxwidth = qMin(maxwidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
	}
	w = maxwidth - st::msgServiceMargin.left() - st::msgServiceMargin.left();

	left += (w - dateTextWidth - st::msgServicePadding.left() - st::msgServicePadding.right()) / 2;
	int height = st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom();
	ServiceMessagePainter::paintBubble(p, left, y + st::msgServiceMargin.top(), dateTextWidth + st::msgServicePadding.left() + st::msgServicePadding.left(), height);

	p.setFont(st::msgServiceFont);
	p.setPen(st::msgServiceColor);
	p.drawText(left + st::msgServicePadding.left(), y + st::msgServiceMargin.top() + st::msgServicePadding.top() + st::msgServiceFont->ascent, dateText);
}

} // namepsace

void ServiceMessagePainter::paint(Painter &p, const HistoryService *message, const PaintContext &context, int height) {
	int left = 0, width = 0;
	message->countPositionAndSize(left, width);
	if (width < 1) return;

	uint64 fullAnimMs = App::main() ? App::main()->animActiveTimeStart(message) : 0;
	if (fullAnimMs > 0 && fullAnimMs <= context.ms) {
		int animms = context.ms - fullAnimMs;
		if (animms > st::activeFadeInDuration + st::activeFadeOutDuration) {
			App::main()->stopAnimActive();
		} else {
			int skiph = st::msgServiceMargin.top() - st::msgServiceMargin.bottom();

			textstyleSet(&st::inTextStyle);
			float64 dt = (animms > st::activeFadeInDuration) ? (1 - (animms - st::activeFadeInDuration) / float64(st::activeFadeOutDuration)) : (animms / float64(st::activeFadeInDuration));
			float64 o = p.opacity();
			p.setOpacity(o * dt);
			p.fillRect(0, skiph, message->history()->width, message->height() - skiph, textstyleCurrent()->selectOverlay->b);
			p.setOpacity(o);
		}
	}

	textstyleSet(&st::serviceTextStyle);

	if (auto media = message->getMedia()) {
		height -= st::msgServiceMargin.top() + media->height();
		int32 left = st::msgServiceMargin.left() + (width - media->maxWidth()) / 2, top = st::msgServiceMargin.top() + height + st::msgServiceMargin.top();
		p.translate(left, top);
		media->draw(p, context.clip.translated(-left, -top), message->toMediaSelection(context.selection), context.ms);
		p.translate(-left, -top);
	}

	QRect trect(QRect(left, st::msgServiceMargin.top(), width, height).marginsAdded(-st::msgServicePadding));

	paintComplexBubble(p, left, width, message->_text, trect);

	if (width > message->maxWidth()) {
		left += (width - message->maxWidth()) / 2;
		width = message->maxWidth();
	}

	p.setBrush(Qt::NoBrush);
	p.setPen(st::msgServiceColor);
	p.setFont(st::msgServiceFont);
	message->_text.draw(p, trect.x(), trect.y(), trect.width(), Qt::AlignCenter, 0, -1, context.selection, false);

	textstyleRestore();
}

void ServiceMessagePainter::paintDate(Painter &p, const QDateTime &date, int y, int w) {
	auto dateText = langDayOfMonthFull(date.date());
	auto dateTextWidth = st::msgServiceFont->width(dateText);
	paintPreparedDate(p, dateText, dateTextWidth, y, w);
}

void ServiceMessagePainter::paintDate(Painter &p, const QString &dateText, int dateTextWidth, int y, int w) {
	paintPreparedDate(p, dateText, dateTextWidth, y, w);
}

void ServiceMessagePainter::paintBubble(Painter &p, int x, int y, int w, int h) {
	createCircleMasks();

	paintBubblePart(p, x, y, w, h, SideStyle::Rounded, SideStyle::Rounded);
}

void ServiceMessagePainter::paintComplexBubble(Painter &p, int left, int width, const Text &text, const QRect &textRect) {
	createCircleMasks();

	auto lineWidths = countLineWidths(text, textRect);

	int y = st::msgServiceMargin.top();
	SideStyle topStyle = SideStyle::Rounded, bottomStyle;
	for (int i = 0, count = lineWidths.size(); i < count; ++i) {
		auto lineWidth = lineWidths.at(i);
		if (i + 1 < count) {
			auto nextLineWidth = lineWidths.at(i + 1);
			if (nextLineWidth > lineWidth) {
				bottomStyle = SideStyle::Inverted;
			} else if (nextLineWidth < lineWidth) {
				bottomStyle = SideStyle::Rounded;
			} else {
				bottomStyle = SideStyle::Plain;
			}
		} else {
			bottomStyle = SideStyle::Rounded;
		}

		auto richWidth = lineWidth + st::msgServicePadding.left() + st::msgServicePadding.right();
		auto richHeight = st::msgServiceFont->height;
		if (topStyle == SideStyle::Rounded) {
			richHeight += st::msgServicePadding.top();
		} else if (topStyle == SideStyle::Inverted) {
			richHeight -= st::msgServicePadding.bottom();
		}
		if (bottomStyle == SideStyle::Rounded) {
			richHeight += st::msgServicePadding.bottom();
		} else if (bottomStyle == SideStyle::Inverted) {
			richHeight -= st::msgServicePadding.top();
		}
		paintBubblePart(p, left + ((width - richWidth) / 2), y, richWidth, richHeight, topStyle, bottomStyle);
		y += richHeight;

		if (bottomStyle == SideStyle::Inverted) {
			topStyle = SideStyle::Rounded;
		} else if (bottomStyle == SideStyle::Rounded) {
			topStyle = SideStyle::Inverted;
		} else {
			topStyle = SideStyle::Plain;
		}
	}
}

QVector<int> ServiceMessagePainter::countLineWidths(const Text &text, const QRect &textRect) {
	int linesCount = qMax(textRect.height() / st::msgServiceFont->height, 1);
	QVector<int> lineWidths;
	lineWidths.reserve(linesCount);
	text.countLineWidths(textRect.width(), &lineWidths);

	int minDelta = 2 * (st::historyServiceMsgRadius + st::historyServiceMsgInvertedRadius - st::historyServiceMsgInvertedShrink);
	for (int i = 0, count = lineWidths.size(); i < count; ++i) {
		int width = qMax(lineWidths.at(i), 0);
		if (i > 0) {
			int widthBefore = lineWidths.at(i - 1);
			if (width < widthBefore && width + minDelta > widthBefore) {
				width = widthBefore;
			}
		}
		if (i + 1 < count) {
			int widthAfter = lineWidths.at(i + 1);
			if (width < widthAfter && width + minDelta > widthAfter) {
				width = widthAfter;
			}
		}
		if (width > lineWidths.at(i)) {
			lineWidths[i] = width;
			if (i > 0) {
				int widthBefore = lineWidths.at(i - 1);
				if (widthBefore != width && widthBefore < width + minDelta && widthBefore + minDelta > width) {
					i -= 2;
				}
			}
		}
	}
	return lineWidths;
}

void serviceColorsUpdated() {
	if (serviceMessageStyle) {
		for (auto &corner : serviceMessageStyle->corners) {
			corner = QPixmap();
		}
	}
}

} // namespace HistoryLayout
