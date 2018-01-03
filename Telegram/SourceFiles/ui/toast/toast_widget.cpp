/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/toast/toast_widget.h"

namespace Ui {
namespace Toast {
namespace internal {
namespace {

constexpr auto kToastMaxLines = 16;

} // namespace

Widget::Widget(QWidget *parent, const Config &config) : TWidget(parent)
, _multiline(config.maxWidth > 0)
, _maxWidth((config.maxWidth > 0) ? config.maxWidth : st::toastMaxWidth)
, _padding((config.padding.left() > 0) ? config.padding : st::toastPadding)
, _maxTextWidth(_maxWidth - _padding.left() - _padding.right())
, _text(_multiline ? _maxTextWidth : QFIXED_MAX) {
	TextParseOptions toastOptions = { 0, _maxTextWidth, st::toastTextStyle.font->height, Qt::LayoutDirectionAuto };
	if (_multiline) {
		toastOptions.maxh *= kToastMaxLines;
	}
	_text.setText(st::toastTextStyle, _multiline ? config.text : TextUtilities::SingleLine(config.text), toastOptions);

	setAttribute(Qt::WA_TransparentForMouseEvents);

	onParentResized();
	show();
}

void Widget::onParentResized() {
	auto newWidth = _maxWidth;
	accumulate_min(newWidth, _padding.left() + _text.maxWidth() + _padding.right());
	accumulate_min(newWidth, parentWidget()->width() - 2 * st::toastMinMargin);
	_textWidth = newWidth - _padding.left() - _padding.right();
	auto maxHeight = kToastMaxLines * st::toastTextStyle.font->height;
	auto textHeight = _multiline ? qMin(_text.countHeight(_textWidth), maxHeight) : _text.minHeight();
	auto newHeight = _padding.top() + textHeight + _padding.bottom();
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

	auto lines = _multiline ? kToastMaxLines : 1;
	p.setPen(st::toastFg);
	_text.drawElided(p, _padding.left(), _padding.top(), _textWidth + 1, lines);
}

} // namespace internal
} // namespace Toast
} // namespace Ui
