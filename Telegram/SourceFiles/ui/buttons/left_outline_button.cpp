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
#include "ui/buttons/left_outline_button.h"

namespace Ui {

LeftOutlineButton::LeftOutlineButton(QWidget *parent, const QString &text, const style::OutlineButton &st) : Button(parent)
, _text(text)
, _fullText(text)
, _textWidth(st.font->width(_text))
, _fullTextWidth(_textWidth)
, _st(st) {
	resizeToWidth(_textWidth + _st.padding.left() + _st.padding.right());

	setCursor(style::cur_pointer);
}

void LeftOutlineButton::setText(const QString &text) {
	_text = text;
	_fullText = text;
	_fullTextWidth = _textWidth = _st.font->width(_text);
	resizeToWidth(width());
	update();
}

void LeftOutlineButton::resizeToWidth(int newWidth) {
	int availableWidth = qMax(newWidth - _st.padding.left() - _st.padding.right(), 1);
	if ((availableWidth < _fullTextWidth) || (_textWidth < availableWidth)) {
		_text = _st.font->elided(_fullText, availableWidth);
		_textWidth = _st.font->width(_text);
	}
	resize(newWidth, _st.padding.top() + _st.font->height + _st.padding.bottom());
}

void LeftOutlineButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	bool over = (_state & Button::StateOver);
	if (width() > _st.outlineWidth) {
		p.fillRect(rtlrect(0, 0, _st.outlineWidth, height(), width()), over ? _st.outlineFgOver : _st.outlineFg);
		p.fillRect(rtlrect(_st.outlineWidth, 0, width() - _st.outlineWidth, height(), width()), over ? _st.textBgOver : _st.textBg);
	}
	p.setFont(_st.font);
	p.setPen(over ? _st.textFgOver : _st.textFg);
	p.drawTextLeft(_st.padding.left(), _st.padding.top(), width(), _text, _textWidth);
}

void LeftOutlineButton::onStateChanged(int oldState, ButtonStateChangeSource source) {
	update();
}

} // namespace Ui
