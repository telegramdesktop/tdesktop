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
#include "ui/buttons/round_button.h"

namespace Ui {

RoundButton::RoundButton(QWidget *parent, const QString &text, const style::BoxButton &st) : Button(parent)
, _text(text.toUpper())
, _fullText(text.toUpper())
, _textWidth(st.font->width(_text))
, _st(st)
, a_textBgOverOpacity(0)
, a_textFg(st.textFg->c)
, _a_over(animation(this, &RoundButton::step_over)) {
	resizeToText();

	setCursor(style::cur_pointer);
}

void RoundButton::setText(const QString &text) {
	_text = text;
	_fullText = text;
	_textWidth = _st.font->width(_text);
	resizeToText();
}

void RoundButton::resizeToText() {
	if (_st.width <= 0) {
		resize(_textWidth - _st.width, _st.height);
	} else {
		if (_st.width < _textWidth + (_st.height - _st.font->height)) {
			_text = _st.font->elided(_fullText, qMax(_st.width - (_st.height - _st.font->height), 1));
			_textWidth = _st.font->width(_text);
		}
		resize(_st.width, _st.height);
	}
}

void RoundButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	App::roundRect(p, rect(), _st.textBg);

	float64 o = a_textBgOverOpacity.current();
	if (o > 0) {
		p.setOpacity(o);
		App::roundRect(p, rect(), _st.textBgOver);
		p.setOpacity(1);
		p.setPen(a_textFg.current());
	} else {
		p.setPen(_st.textFg);
	}
	p.setFont(_st.font);
	p.drawText((width() - _textWidth) / 2, _st.textTop + _st.font->ascent, _text);
}

void RoundButton::step_over(float64 ms, bool timer) {
	float64 dt = ms / _st.duration;
	if (dt >= 1) {
		_a_over.stop();
		a_textFg.finish();
		a_textBgOverOpacity.finish();
	} else {
		a_textFg.update(dt, anim::linear);
		a_textBgOverOpacity.update(dt, anim::linear);
	}
	if (timer) update();
}

void RoundButton::onStateChanged(int oldState, ButtonStateChangeSource source) {
	float64 textBgOverOpacity = (_state & StateOver) ? 1 : 0;
	style::color textFg = (_state & StateOver) ? (_st.textFgOver) : _st.textFg;

	a_textBgOverOpacity.start(textBgOverOpacity);
	a_textFg.start(textFg->c);
	if (source == ButtonByUser || source == ButtonByPress) {
		_a_over.stop();
		a_textBgOverOpacity.finish();
		a_textFg.finish();
		update();
	} else {
		_a_over.start();
	}
}

} // namespace Ui
