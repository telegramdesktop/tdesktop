/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_service_layout.h"

#include "history/history_service.h"
#include "history/history_media.h"
#include "data/data_abstract_structure.h"
#include "styles/style_history.h"
#include "mainwidget.h"
#include "lang/lang_keys.h"

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

int historyServiceMsgRadius() {
	static int HistoryServiceMsgRadius = ([]() {
		auto minMsgHeight = (st::msgServiceFont->height + st::msgServicePadding.top() + st::msgServicePadding.bottom());
		return minMsgHeight / 2;
	})();
	return HistoryServiceMsgRadius;
}

int historyServiceMsgInvertedRadius() {
	static int HistoryServiceMsgInvertedRadius = ([]() {
		auto minRowHeight = st::msgServiceFont->height;
		return minRowHeight - historyServiceMsgRadius();
	})();
	return HistoryServiceMsgInvertedRadius;
}

int historyServiceMsgInvertedShrink() {
	static int HistoryServiceMsgInvertedShrink = ([]() {
		return (historyServiceMsgInvertedRadius() * 2) / 3;
	})();
	return HistoryServiceMsgInvertedShrink;
}

void createCircleMasks() {
	serviceMessageStyle.createIfNull();
	if (!serviceMessageStyle->circle[NormalMask].isNull()) return;

	int size = historyServiceMsgRadius() * 2;
	serviceMessageStyle->circle[NormalMask] = style::createCircleMask(size);
	int sizeInverted = historyServiceMsgInvertedRadius() * 2;
	serviceMessageStyle->circle[InvertedMask] = style::createInvertedCircleMask(sizeInverted);
}

QPixmap circleCorner(int corner) {
	if (serviceMessageStyle->corners[corner].isNull()) {
		int maskType = corner / MaskMultiplier;
		int radius = (maskType == NormalMask ? historyServiceMsgRadius() : historyServiceMsgInvertedRadius());
		int size = radius * cIntRetinaFactor();

		int xoffset = 0, yoffset = 0;
		if (corner & CornerRight) {
			xoffset = size;
		}
		if (corner & CornerBottom) {
			yoffset = size;
		}
		auto part = QRect(xoffset, yoffset, size, size);
		auto result = style::colorizeImage(serviceMessageStyle->circle[maskType], st::msgServiceBg, part);
		result.setDevicePixelRatio(cRetinaFactor());
		serviceMessageStyle->corners[corner] = App::pixmapFromImageInPlace(std::move(result));
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
		p.fillRect(x + leftWidth, y, width - leftWidth - rightWidth, cornerHeight, st::msgServiceBg);
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

void paintBubblePart(Painter &p, int x, int y, int width, int height, SideStyle topStyle, SideStyle bottomStyle, bool forceShrink = false) {
	if (topStyle == SideStyle::Inverted || bottomStyle == SideStyle::Inverted || forceShrink) {
		width -= historyServiceMsgInvertedShrink() * 2;
		x += historyServiceMsgInvertedShrink();
	}

	if (int skip = paintBubbleSide(p, x, y, width, topStyle, CornerTop)) {
		y += skip;
		height -= skip;
	}
	int bottomSize = 0;
	if (bottomStyle == SideStyle::Rounded) {
		bottomSize = historyServiceMsgRadius();
	} else if (bottomStyle == SideStyle::Inverted) {
		bottomSize = historyServiceMsgInvertedRadius();
	}
	if (int skip = paintBubbleSide(p, x, y + height - bottomSize, width, bottomStyle, CornerBottom)) {
		height -= skip;
	}

	p.fillRect(x, y, width, height, st::msgServiceBg);
}

void paintPreparedDate(Painter &p, const QString &dateText, int dateTextWidth, int y, int w) {
	int left = st::msgServiceMargin.left();
	int maxwidth = w;
	if (Adaptive::ChatWide()) {
		maxwidth = qMin(maxwidth, WideChatWidth());
	}
	w = maxwidth - st::msgServiceMargin.left() - st::msgServiceMargin.left();

	left += (w - dateTextWidth - st::msgServicePadding.left() - st::msgServicePadding.right()) / 2;
	int height = st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom();
	ServiceMessagePainter::paintBubble(p, left, y + st::msgServiceMargin.top(), dateTextWidth + st::msgServicePadding.left() + st::msgServicePadding.left(), height);

	p.setFont(st::msgServiceFont);
	p.setPen(st::msgServiceFg);
	p.drawText(left + st::msgServicePadding.left(), y + st::msgServiceMargin.top() + st::msgServicePadding.top() + st::msgServiceFont->ascent, dateText);
}

} // namepsace

int WideChatWidth() {
	return st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left();
}

void ServiceMessagePainter::paint(
		Painter &p,
		not_null<const HistoryService*> message,
		const PaintContext &context,
		int height) {
	auto g = message->countGeometry();
	if (g.width() < 1) return;

	auto fullAnimMs = App::main() ? App::main()->highlightStartTime(message) : 0LL;
	if (fullAnimMs > 0 && fullAnimMs <= context.ms) {
		auto animms = context.ms - fullAnimMs;
		if (animms < st::activeFadeInDuration + st::activeFadeOutDuration) {
			auto top = st::msgServiceMargin.top();
			auto bottom = st::msgServiceMargin.bottom();
			auto fill = qMin(top, bottom);
			auto skiptop = top - fill;
			auto fillheight = fill + height + fill;

			auto dt = (animms > st::activeFadeInDuration) ? (1. - (animms - st::activeFadeInDuration) / float64(st::activeFadeOutDuration)) : (animms / float64(st::activeFadeInDuration));
			auto o = p.opacity();
			p.setOpacity(o * dt);
			p.fillRect(0, skiptop, message->history()->width, fillheight, st::defaultTextPalette.selectOverlay);
			p.setOpacity(o);
		}
	}

	p.setTextPalette(st::serviceTextPalette);

	if (auto media = message->getMedia()) {
		height -= st::msgServiceMargin.top() + media->height();
		auto left = st::msgServiceMargin.left() + (g.width() - media->maxWidth()) / 2, top = st::msgServiceMargin.top() + height + st::msgServiceMargin.top();
		p.translate(left, top);
		media->draw(p, context.clip.translated(-left, -top), message->skipTextSelection(context.selection), context.ms);
		p.translate(-left, -top);
	}

	auto trect = QRect(g.left(), st::msgServiceMargin.top(), g.width(), height).marginsAdded(-st::msgServicePadding);

	paintComplexBubble(p, g.left(), g.width(), message->_text, trect);

	p.setBrush(Qt::NoBrush);
	p.setPen(st::msgServiceFg);
	p.setFont(st::msgServiceFont);
	message->_text.draw(p, trect.x(), trect.y(), trect.width(), Qt::AlignCenter, 0, -1, context.selection, false);

	p.restoreTextPalette();
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

	int y = st::msgServiceMargin.top(), previousRichWidth = 0;
	bool previousShrink = false, forceShrink = false;
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
		forceShrink = previousShrink && (richWidth == previousRichWidth);
		paintBubblePart(p, left + ((width - richWidth) / 2), y, richWidth, richHeight, topStyle, bottomStyle, forceShrink);
		y += richHeight;

		previousShrink = forceShrink || (topStyle == SideStyle::Inverted) || (bottomStyle == SideStyle::Inverted);
		previousRichWidth = richWidth;

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

	int minDelta = 2 * (historyServiceMsgRadius() + historyServiceMsgInvertedRadius() - historyServiceMsgInvertedShrink());
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

void paintEmpty(Painter &p, int width, int height) {
}

void serviceColorsUpdated() {
	if (serviceMessageStyle) {
		for (auto &corner : serviceMessageStyle->corners) {
			corner = QPixmap();
		}
	}
}

void paintBubble(Painter &p, QRect rect, int outerWidth, bool selected, bool outbg, RectPart tailSide) {
	auto &bg = selected ? (outbg ? st::msgOutBgSelected : st::msgInBgSelected) : (outbg ? st::msgOutBg : st::msgInBg);
	auto &sh = selected ? (outbg ? st::msgOutShadowSelected : st::msgInShadowSelected) : (outbg ? st::msgOutShadow : st::msgInShadow);
	auto cors = selected ? (outbg ? MessageOutSelectedCorners : MessageInSelectedCorners) : (outbg ? MessageOutCorners : MessageInCorners);
	auto parts = RectPart::FullTop | RectPart::NoTopBottom | RectPart::Bottom;
	if (tailSide == RectPart::Right) {
		parts |= RectPart::BottomLeft;
		p.fillRect(rect.x() + rect.width() - st::historyMessageRadius, rect.y() + rect.height() - st::historyMessageRadius, st::historyMessageRadius, st::historyMessageRadius, bg);
		auto &tail = selected ? st::historyBubbleTailOutRightSelected : st::historyBubbleTailOutRight;
		tail.paint(p, rect.x() + rect.width(), rect.y() + rect.height() - tail.height(), outerWidth);
		p.fillRect(rect.x() + rect.width() - st::historyMessageRadius, rect.y() + rect.height(), st::historyMessageRadius + tail.width(), st::msgShadow, sh);
	} else if (tailSide == RectPart::Left) {
		parts |= RectPart::BottomRight;
		p.fillRect(rect.x(), rect.y() + rect.height() - st::historyMessageRadius, st::historyMessageRadius, st::historyMessageRadius, bg);
		auto &tail = selected ? (outbg ? st::historyBubbleTailOutLeftSelected : st::historyBubbleTailInLeftSelected) : (outbg ? st::historyBubbleTailOutLeft : st::historyBubbleTailInLeft);
		tail.paint(p, rect.x() - tail.width(), rect.y() + rect.height() - tail.height(), outerWidth);
		p.fillRect(rect.x() - tail.width(), rect.y() + rect.height(), st::historyMessageRadius + tail.width(), st::msgShadow, sh);
	} else {
		parts |= RectPart::FullBottom;
	}
	App::roundRect(p, rect, bg, cors, &sh, parts);
}

} // namespace HistoryLayout
