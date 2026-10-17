/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/unsupported_notice.h"

#include "ui/chat/chat_style.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"

#include <QtMath>

#include "styles/style_chat.h"
#include "styles/style_polls.h"

namespace Ui {
namespace {

[[nodiscard]] QPainterPath BadgePath(QRect badge) {
	const auto radius = badge.width() / 2.;
	const auto center = QRectF(badge).center();
	const auto point = [&](float64 degrees, float64 stretch) {
		const auto radians = degrees * M_PI / 180.;
		return center + QPointF(
			std::cos(radians),
			std::sin(radians)) * (radius * stretch);
	};
	auto result = QPainterPath();
	result.setFillRule(Qt::WindingFill);
	result.addEllipse(QRectF(badge));
	auto tail = QPolygonF();
	tail << point(125., 1.) << point(137.5, 1.33) << point(150., 1.);
	result.addPolygon(tail);
	return result;
}

[[nodiscard]] int BadgeSkip(int cardHeight) {
	return std::max((cardHeight - st::unsupportedNoticeBadgeSize) / 2, 0);
}

} // namespace

void UnsupportedNoticeCard::setTexts(
		const QString &title,
		const QString &text,
		const QString &button) {
	_title = Text::String(st::serviceTextStyle, title);
	_text = Text::String(
		st::defaultTextStyle,
		text,
		kDefaultTextOptions,
		st::msgMinWidth);
	_button = Text::String(st::semiboldTextStyle, button);
	_buttonSize = QSize(
		(_button.maxWidth()
			+ st::unsupportedNoticeButtonPadding.left()
			+ st::unsupportedNoticeButtonPadding.right()),
		st::unsupportedNoticeButtonHeight);
}

int UnsupportedNoticeCard::resizeGetHeight(int availableWidth) {
	_width = std::min(availableWidth, st::unsupportedNoticeMaxWidth);
	const auto &padding = st::unsupportedNoticePadding;
	const auto lineHeight = st::msgFont->height;
	const auto heightForLines = [&](int lines) {
		return padding.top()
			+ st::msgServiceFont->height
			+ lines * lineHeight
			+ padding.bottom();
	};
	const auto columnForHeight = [&](int height) {
		return std::max(
			(_width
				- BadgeSkip(height)
				- st::unsupportedNoticeBadgeSize
				- st::unsupportedNoticeBadgeSkip
				- padding.right()
				- _buttonSize.width()
				- padding.right()),
			0);
	};
	_textLines = 1;
	_height = heightForLines(1);
	_columnWidth = columnForHeight(_height);
	const auto textHeight = (_columnWidth > 0)
		? _text.countHeight(_columnWidth)
		: lineHeight;
	if (textHeight > lineHeight) {
		_textLines = 2;
		_height = heightForLines(2);
		_columnWidth = columnForHeight(_height);
	}
	return _height;
}

void UnsupportedNoticeCard::paint(
		Painter &p,
		const ChatPaintContext &context,
		QRect cardRect,
		RippleAnimation *ripple) const {
	const auto st = context.st;
	auto hq = PainterHighQualityEnabler(p);

	p.setPen(Qt::NoPen);
	p.setBrush(st->msgServiceBg());
	const auto radius = st::unsupportedNoticeRadius;
	p.drawRoundedRect(cardRect, radius, radius);

	const auto &padding = st::unsupportedNoticePadding;
	const auto badgeSize = st::unsupportedNoticeBadgeSize;
	const auto badgeSkip = BadgeSkip(cardRect.height());
	const auto badge = QRect(
		cardRect.x() + badgeSkip,
		cardRect.y() + badgeSkip,
		badgeSize,
		badgeSize);
	p.drawPath(BadgePath(badge));
	st::unsupportedNoticePlane.paintInCenter(
		p,
		badge,
		st->msgServiceFg()->c);

	if (_columnWidth > 0) {
		const auto left = cardRect.x()
			+ badgeSkip
			+ badgeSize
			+ st::unsupportedNoticeBadgeSkip;
		const auto top = cardRect.y() + padding.top();
		p.setPen(st->msgServiceFg());
		_title.drawElided(p, left, top, _columnWidth);
		auto dimmed = st->msgServiceFg()->c;
		dimmed.setAlphaF(0.65 * dimmed.alphaF());
		p.setPen(dimmed);
		_text.drawElided(
			p,
			left,
			top + st::msgServiceFont->height,
			_columnWidth,
			_textLines);
	}

	const auto button = buttonRect().translated(cardRect.topLeft());
	const auto buttonRadius = button.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(st->msgServiceBg());
	p.drawRoundedRect(button, buttonRadius, buttonRadius);
	if (ripple) {
		p.save();
		auto clip = QPainterPath();
		clip.addRoundedRect(QRectF(button), buttonRadius, buttonRadius);
		p.setClipPath(clip, Qt::IntersectClip);
		p.setOpacity(st::historyPollRippleOpacity);
		p.translate(button.topLeft());
		ripple->paint(
			p,
			0,
			0,
			button.width(),
			&context.messageStyle()->msgWaveformInactive->c);
		p.restore();
	}
	p.setPen(st->msgServiceFg());
	_button.draw(
		p,
		button.x(),
		button.y() + (button.height() - _button.minHeight()) / 2,
		button.width(),
		style::al_top);
}

int UnsupportedNoticeCard::width() const {
	return _width;
}

int UnsupportedNoticeCard::height() const {
	return _height;
}

QRect UnsupportedNoticeCard::buttonRect() const {
	return QRect(
		QPoint(
			(_width
				- st::unsupportedNoticePadding.right()
				- _buttonSize.width()),
			(_height - _buttonSize.height()) / 2),
		_buttonSize);
}

QSize UnsupportedNoticeCard::buttonSize() const {
	return _buttonSize;
}

} // namespace Ui
