/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/toast/toast_widget.h"

#include "ui/image/image_prepare.h"

namespace Ui {
namespace Toast {
namespace internal {

Widget::Widget(QWidget *parent, const Config &config) : TWidget(parent)
, _multiline(config.multiline)
, _maxWidth((config.maxWidth > 0) ? config.maxWidth : st::toastMaxWidth)
, _padding((config.padding.left() > 0) ? config.padding : st::toastPadding)
, _maxTextWidth(widthWithoutPadding(_maxWidth))
, _maxTextHeight(
	st::toastTextStyle.font->height * (_multiline ? config.maxLines : 1))
, _text(_multiline ? widthWithoutPadding(config.minWidth) : QFIXED_MAX) {
	const auto toastOptions = TextParseOptions{
		TextParseMultiline,
		_maxTextWidth,
		_maxTextHeight,
		Qt::LayoutDirectionAuto
	};
	_text.setText(
		st::toastTextStyle,
		_multiline ? config.text : TextUtilities::SingleLine(config.text),
		toastOptions);

	setAttribute(Qt::WA_TransparentForMouseEvents);

	onParentResized();
	show();
}

void Widget::onParentResized() {
	auto newWidth = _maxWidth;
	accumulate_min(newWidth, _padding.left() + _text.maxWidth() + _padding.right());
	accumulate_min(newWidth, parentWidget()->width() - 2 * st::toastMinMargin);
	_textWidth = widthWithoutPadding(newWidth);
	const auto textHeight = _multiline
		? qMin(_text.countHeight(_textWidth), _maxTextHeight)
		: _text.minHeight();
	const auto newHeight = _padding.top() + textHeight + _padding.bottom();
	setGeometry((parentWidget()->width() - newWidth) / 2, (parentWidget()->height() - newHeight) / 2, newWidth, newHeight);
}

void Widget::setShownLevel(float64 shownLevel) {
	_shownLevel = shownLevel;
}

void Widget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	PainterHighQualityEnabler hq(p);

	p.setOpacity(_shownLevel);
	App::roundRect(p, rect(), st::toastBg, ImageRoundRadius::Large);

	const auto lines = _maxTextHeight / st::toastTextStyle.font->height;
	p.setPen(st::toastFg);
	_text.drawElided(p, _padding.left(), _padding.top(), _textWidth + 1, lines);
}

} // namespace internal
} // namespace Toast
} // namespace Ui
