/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_service_message.h"

#include "history/history.h"
#include "history/history_service.h"
#include "history/view/media/history_view_media.h"
#include "history/history_item_components.h"
#include "history/view/history_view_cursor_state.h"
#include "data/data_abstract_structure.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "ui/text/text_options.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "mainwidget.h"
#include "layout.h"
#include "lang/lang_keys.h"
#include "app.h"
#include "styles/style_chat.h"

namespace HistoryView {
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
	if (Core::App().settings().chatWide()) {
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

bool NeedAboutGroup(not_null<History*> history) {
	if (const auto chat = history->peer->asChat()) {
		return chat->amCreator();
	} else if (const auto channel = history->peer->asMegagroup()) {
		return channel->amCreator();
	}
	return false;
}

} // namepsace

int WideChatWidth() {
	return st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left();
}

void ServiceMessagePainter::paintDate(Painter &p, const QDateTime &date, int y, int w) {
	auto dateText = langDayOfMonthFull(date.date());
	auto dateTextWidth = st::msgServiceFont->width(dateText);
	paintPreparedDate(p, dateText, dateTextWidth, y, w);
}

void ServiceMessagePainter::paintDate(Painter &p, const QString &dateText, int y, int w) {
	paintPreparedDate(p, dateText, st::msgServiceFont->width(dateText), y, w);
}

void ServiceMessagePainter::paintDate(Painter &p, const QString &dateText, int dateTextWidth, int y, int w) {
	paintPreparedDate(p, dateText, dateTextWidth, y, w);
}

void ServiceMessagePainter::paintBubble(Painter &p, int x, int y, int w, int h) {
	createCircleMasks();

	paintBubblePart(p, x, y, w, h, SideStyle::Rounded, SideStyle::Rounded);
}

void ServiceMessagePainter::paintComplexBubble(Painter &p, int left, int width, const Ui::Text::String &text, const QRect &textRect) {
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

QVector<int> ServiceMessagePainter::countLineWidths(const Ui::Text::String &text, const QRect &textRect) {
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

void serviceColorsUpdated() {
	if (serviceMessageStyle) {
		for (auto &corner : serviceMessageStyle->corners) {
			corner = QPixmap();
		}
	}
}

Service::Service(
	not_null<ElementDelegate*> delegate,
	not_null<HistoryService*> data,
	Element *replacing)
: Element(delegate, data, replacing) {
}

not_null<HistoryService*> Service::message() const {
	return static_cast<HistoryService*>(data().get());
}

QRect Service::countGeometry() const {
	auto result = QRect(0, 0, width(), height());
	if (Core::App().settings().chatWide()) {
		result.setWidth(qMin(result.width(), st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
	}
	return result.marginsRemoved(st::msgServiceMargin);
}

QSize Service::performCountCurrentSize(int newWidth) {
	auto newHeight = displayedDateHeight();
	if (const auto bar = Get<UnreadBar>()) {
		newHeight += bar->height();
	}

	if (isHidden()) {
		return { newWidth, newHeight };
	}

	const auto item = message();
	const auto media = this->media();

	if (item->_text.isEmpty()) {
		item->_textHeight = 0;
	} else {
		auto contentWidth = newWidth;
		if (Core::App().settings().chatWide()) {
			accumulate_min(contentWidth, st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left());
		}
		contentWidth -= st::msgServiceMargin.left() + st::msgServiceMargin.left(); // two small margins
		if (contentWidth < st::msgServicePadding.left() + st::msgServicePadding.right() + 1) {
			contentWidth = st::msgServicePadding.left() + st::msgServicePadding.right() + 1;
		}

		auto nwidth = qMax(contentWidth - st::msgServicePadding.left() - st::msgServicePadding.right(), 0);
		if (nwidth != item->_textWidth) {
			item->_textWidth = nwidth;
			item->_textHeight = item->_text.countHeight(nwidth);
		}
		if (contentWidth >= maxWidth()) {
			newHeight += minHeight();
		} else {
			newHeight += item->_textHeight;
		}
		newHeight += st::msgServicePadding.top() + st::msgServicePadding.bottom() + st::msgServiceMargin.top() + st::msgServiceMargin.bottom();
		if (media) {
			newHeight += st::msgServiceMargin.top() + media->resizeGetHeight(media->maxWidth());
		}
	}

	return { newWidth, newHeight };
}

QSize Service::performCountOptimalSize() {
	const auto item = message();
	const auto media = this->media();

	auto maxWidth = item->_text.maxWidth() + st::msgServicePadding.left() + st::msgServicePadding.right();
	auto minHeight = item->_text.minHeight();
	if (media) {
		media->initDimensions();
	}
	return { maxWidth, minHeight };
}

bool Service::isHidden() const {
	return Element::isHidden();
}

int Service::marginTop() const {
	return st::msgServiceMargin.top();
}

int Service::marginBottom() const {
	return st::msgServiceMargin.bottom();
}

void Service::draw(
		Painter &p,
		QRect clip,
		TextSelection selection,
		crl::time ms) const {
	const auto item = message();
	auto g = countGeometry();
	if (g.width() < 1) {
		return;
	}

	auto height = this->height() - st::msgServiceMargin.top() - st::msgServiceMargin.bottom();
	auto dateh = 0;
	auto unreadbarh = 0;
	if (auto date = Get<DateBadge>()) {
		dateh = date->height();
		p.translate(0, dateh);
		clip.translate(0, -dateh);
		height -= dateh;
	}
	if (const auto bar = Get<UnreadBar>()) {
		unreadbarh = bar->height();
		if (clip.intersects(QRect(0, 0, width(), unreadbarh))) {
			bar->paint(p, 0, width());
		}
		p.translate(0, unreadbarh);
		clip.translate(0, -unreadbarh);
		height -= unreadbarh;
	}

	if (isHidden()) {
		if (auto skiph = dateh + unreadbarh) {
			p.translate(0, -skiph);
		}
		return;
	}

	paintHighlight(p, height);

	p.setTextPalette(st::serviceTextPalette);

	if (auto media = this->media()) {
		height -= st::msgServiceMargin.top() + media->height();
		auto left = st::msgServiceMargin.left() + (g.width() - media->maxWidth()) / 2, top = st::msgServiceMargin.top() + height + st::msgServiceMargin.top();
		p.translate(left, top);
		media->draw(p, clip.translated(-left, -top), TextSelection(), ms);
		p.translate(-left, -top);
	}

	auto trect = QRect(g.left(), st::msgServiceMargin.top(), g.width(), height).marginsAdded(-st::msgServicePadding);

	ServiceMessagePainter::paintComplexBubble(p, g.left(), g.width(), item->_text, trect);

	p.setBrush(Qt::NoBrush);
	p.setPen(st::msgServiceFg);
	p.setFont(st::msgServiceFont);
	item->_text.draw(p, trect.x(), trect.y(), trect.width(), Qt::AlignCenter, 0, -1, selection, false);

	p.restoreTextPalette();

	if (auto skiph = dateh + unreadbarh) {
		p.translate(0, -skiph);
	}
}

PointState Service::pointState(QPoint point) const {
	const auto item = message();
	const auto media = this->media();

	auto g = countGeometry();
	if (g.width() < 1 || isHidden()) {
		return PointState::Outside;
	}

	if (const auto dateh = displayedDateHeight()) {
		g.setTop(g.top() + dateh);
	}
	if (const auto bar = Get<UnreadBar>()) {
		g.setTop(g.top() + bar->height());
	}
	if (media) {
		g.setHeight(g.height() - (st::msgServiceMargin.top() + media->height()));
	}
	return g.contains(point) ? PointState::Inside : PointState::Outside;
}

TextState Service::textState(QPoint point, StateRequest request) const {
	const auto item = message();
	const auto media = this->media();

	auto result = TextState(item);

	auto g = countGeometry();
	if (g.width() < 1 || isHidden()) {
		return result;
	}

	if (const auto dateh = displayedDateHeight()) {
		point.setY(point.y() - dateh);
		g.setHeight(g.height() - dateh);
	}
	if (const auto bar = Get<UnreadBar>()) {
		auto unreadbarh = bar->height();
		point.setY(point.y() - unreadbarh);
		g.setHeight(g.height() - unreadbarh);
	}

	if (media) {
		g.setHeight(g.height() - (st::msgServiceMargin.top() + media->height()));
	}
	auto trect = g.marginsAdded(-st::msgServicePadding);
	if (trect.contains(point)) {
		auto textRequest = request.forText();
		textRequest.align = style::al_center;
		result = TextState(item, item->_text.getState(
			point - trect.topLeft(),
			trect.width(),
			textRequest));
		if (!result.link
			&& result.cursor == CursorState::Text
			&& g.contains(point)) {
			if (const auto gamescore = item->Get<HistoryServiceGameScore>()) {
				result.link = gamescore->lnk;
			} else if (const auto payment = item->Get<HistoryServicePayment>()) {
				result.link = payment->invoiceLink;
			} else if (const auto call = item->Get<HistoryServiceOngoingCall>()) {
				const auto peer = history()->peer;
				if (PeerHasThisCall(peer, call->id).value_or(false)) {
					result.link = call->link;
				}
			}
		}
	} else if (media) {
		result = media->textState(point - QPoint(st::msgServiceMargin.left() + (g.width() - media->maxWidth()) / 2, st::msgServiceMargin.top() + g.height() + st::msgServiceMargin.top()), request);
	}
	return result;
}

void Service::updatePressed(QPoint point) {
}

TextForMimeData Service::selectedText(TextSelection selection) const {
	return message()->_text.toTextForMimeData(selection);
}

TextSelection Service::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return message()->_text.adjustSelection(selection, type);
}

EmptyPainter::EmptyPainter(not_null<History*> history) : _history(history) {
	if (NeedAboutGroup(_history)) {
		fillAboutGroup();
	}
}

void EmptyPainter::fillAboutGroup() {
	const auto phrases = {
		tr::lng_group_about1(tr::now),
		tr::lng_group_about2(tr::now),
		tr::lng_group_about3(tr::now),
		tr::lng_group_about4(tr::now),
	};
	const auto setText = [](Ui::Text::String &text, const QString &content) {
		text.setText(
			st::serviceTextStyle,
			content,
			Ui::NameTextOptions());
	};
	setText(_header, tr::lng_group_about_header(tr::now));
	setText(_text, tr::lng_group_about_text(tr::now));
	for (const auto &text : phrases) {
		_phrases.emplace_back(st::msgMinWidth);
		setText(_phrases.back(), text);
	}
}

void EmptyPainter::paint(Painter &p, int width, int height) {
	if (_phrases.empty()) {
		return;
	}
	constexpr auto kMaxTextLines = 3;
	const auto maxPhraseWidth = ranges::max_element(
		_phrases,
		ranges::less(),
		&Ui::Text::String::maxWidth
	)->maxWidth();

	const auto &font = st::serviceTextStyle.font;
	const auto margin = st::msgMargin.left();
	const auto maxBubbleWidth = width - 2 * st::historyGroupAboutMargin;
	const auto padding = st::historyGroupAboutPadding;
	const auto bubbleWidth = std::min(
		maxBubbleWidth,
		std::max({
			maxPhraseWidth + st::historyGroupAboutBulletSkip,
			_header.maxWidth(),
			_text.maxWidth() }) + padding.left() + padding.right());
	const auto innerWidth = bubbleWidth - padding.left() - padding.right();
	const auto textHeight = [&](const Ui::Text::String &text) {
		return std::min(
			text.countHeight(innerWidth),
			kMaxTextLines * font->height);
	};
	const auto bubbleHeight = padding.top()
		+ textHeight(_header)
		+ st::historyGroupAboutHeaderSkip
		+ textHeight(_text)
		+ st::historyGroupAboutTextSkip
		+ ranges::accumulate(_phrases, 0, ranges::plus(), textHeight)
		+ st::historyGroupAboutSkip * int(_phrases.size() - 1)
		+ padding.bottom();
	const auto bubbleLeft = (width - bubbleWidth) / 2;
	const auto bubbleTop = (height - bubbleHeight) / 2;

	ServiceMessagePainter::paintBubble(
		p,
		bubbleLeft,
		bubbleTop,
		bubbleWidth,
		bubbleHeight);

	p.setPen(st::msgServiceFg);
	p.setBrush(st::msgServiceFg);

	const auto left = bubbleLeft + padding.left();
	auto top = bubbleTop + padding.top();

	_header.drawElided(
		p,
		left,
		top,
		innerWidth,
		kMaxTextLines,
		style::al_top);
	top += textHeight(_header) + st::historyGroupAboutHeaderSkip;

	_text.drawElided(
		p,
		left,
		top,
		innerWidth,
		kMaxTextLines);
	top += textHeight(_text) + st::historyGroupAboutTextSkip;

	for (const auto &text : _phrases) {
		p.setPen(st::msgServiceFg);
		text.drawElided(
			p,
			left + st::historyGroupAboutBulletSkip,
			top,
			innerWidth,
			kMaxTextLines);

		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.drawEllipse(
			left,
			top + (font->height - st::mediaUnreadSize) / 2,
			st::mediaUnreadSize,
			st::mediaUnreadSize);
		top += textHeight(text) + st::historyGroupAboutSkip;
	}
}

} // namespace HistoryView
