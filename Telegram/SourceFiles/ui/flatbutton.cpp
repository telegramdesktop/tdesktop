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
#include "ui/flatbutton.h"

#include "styles/style_history.h"

FlatButton::FlatButton(QWidget *parent, const QString &text, const style::flatButton &st) : Button(parent)
, _text(text)
, _st(st)
, a_bg(st.bgColor->c)
, a_text(st.color->c)
, _a_appearance(animation(this, &FlatButton::step_appearance))
, _opacity(1) {
	if (_st.width < 0) {
		_st.width = textWidth() - _st.width;
	} else if (!_st.width) {
		_st.width = textWidth() + _st.height - _st.font->height;
	}
	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));
	resize(_st.width, _st.height);
	setCursor(_st.cursor);
}

void FlatButton::setOpacity(float64 o) {
	_opacity = o;
	update();
}

float64 FlatButton::opacity() const {
	return _opacity;
}

void FlatButton::setText(const QString &text) {
	_text = text;
	update();
}

void FlatButton::setWidth(int32 w) {
	_st.width = w;
	if (_st.width < 0) {
		_st.width = textWidth() - _st.width;
	} else if (!_st.width) {
		_st.width = textWidth() + _st.height - _st.font->height;
	}
	resize(_st.width, height());
}

int32 FlatButton::textWidth() const {
	return _st.font->width(_text);
}

void FlatButton::step_appearance(float64 ms, bool timer) {
	float64 dt = ms / _st.duration;
	if (dt >= 1) {
		_a_appearance.stop();
		a_bg.finish();
		a_text.finish();
	} else {
		a_bg.update(dt, anim::linear);
		a_text.update(dt, anim::linear);
	}
	if (timer) update();
}

void FlatButton::onStateChange(int oldState, ButtonStateChangeSource source) {
	auto &bgColorTo = (_state & StateOver) ? ((_state & StateDown) ? _st.downBgColor : _st.overBgColor) : _st.bgColor;
	auto &colorTo = (_state & StateOver) ? ((_state & StateDown) ? _st.downColor : _st.overColor) : _st.color;

	a_bg.start(bgColorTo->c);
	a_text.start(colorTo->c);
	if (source == ButtonByUser || source == ButtonByPress) {
		_a_appearance.stop();
		a_bg.finish();
		a_text.finish();
		update();
	} else {
		_a_appearance.start();
	}
}

void FlatButton::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	QRect r(0, height() - _st.height, width(), _st.height);

	p.setOpacity(_opacity);
	if (_st.radius > 0) {
		p.setRenderHint(QPainter::HighQualityAntialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush(QBrush(a_bg.current()));
		p.drawRoundedRect(r, _st.radius, _st.radius);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);
	} else {
		p.fillRect(r, a_bg.current());
	}

	p.setFont((_state & StateOver) ? _st.overFont : _st.font);
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setPen(a_text.current());

	int32 top = (_state & StateOver) ? ((_state & StateDown) ? _st.downTextTop : _st.overTextTop) : _st.textTop;
	r.setTop(top);

	p.drawText(r, _text, style::al_top);
}

LinkButton::LinkButton(QWidget *parent, const QString &text, const style::linkButton &st) : Button(parent)
, _text(text)
, _textWidth(st.font->width(_text))
, _st(st) {
	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));
	resize(_textWidth, _st.font->height);
	setCursor(style::cur_pointer);
}

int LinkButton::naturalWidth() const {
	return _textWidth;
}

void LinkButton::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto &font = ((_state & StateOver) ? _st.overFont : _st.font);
	auto &pen = ((_state & StateDown) ? _st.downColor : ((_state & StateOver) ? _st.overColor : _st.color));
	p.setFont(font);
	p.setPen(pen);
	if (_textWidth > width()) {
		p.drawText(0, font->ascent, font->elided(_text, width()));
	} else {
		p.drawText(0, font->ascent, _text);
	}
}

void LinkButton::setText(const QString &text) {
	_text = text;
	_textWidth = _st.font->width(_text);
	resize(_textWidth, _st.font->height);
	update();
}

void LinkButton::onStateChange(int oldState, ButtonStateChangeSource source) {
	update();
}

LinkButton::~LinkButton() {
}

BoxButton::BoxButton(QWidget *parent, const QString &text, const style::RoundButton &st) : Button(parent)
, _text(text.toUpper())
, _fullText(text.toUpper())
, _textWidth(st.font->width(_text))
, _st(st)
, a_textBgOverOpacity(0)
, a_textFg(st.textFg->c)
, _a_over(animation(this, &BoxButton::step_over)) {
	resizeToText();

	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));

	setCursor(style::cur_pointer);

	setAttribute(Qt::WA_OpaquePaintEvent);
}

void BoxButton::setText(const QString &text) {
	_text = text;
	_fullText = text;
	_textWidth = _st.font->width(_text);
	resizeToText();
	update();
}

void BoxButton::resizeToText() {
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

void BoxButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(rect(), _st.textBg->b);

	float64 o = a_textBgOverOpacity.current();
	if (o > 0) {
		p.setOpacity(o);
		App::roundRect(p, rect(), _st.textBgOver, ImageRoundRadius::Small);
		p.setOpacity(1);
		p.setPen(a_textFg.current());
	} else {
		p.setPen(_st.textFg);
	}
	p.setFont(_st.font);

	auto textTop = (_state & StateDown) ? _st.downTextTop : _st.textTop;
	p.drawText((width() - _textWidth) / 2, textTop + _st.font->ascent, _text);
}

void BoxButton::step_over(float64 ms, bool timer) {
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

void BoxButton::onStateChange(int oldState, ButtonStateChangeSource source) {
	float64 textBgOverOpacity = (_state & StateOver) ? 1 : 0;
	style::color textFg = (_state & StateOver) ? (_st.textFgOver) : _st.textFg;

	a_textBgOverOpacity.start(textBgOverOpacity);
	a_textFg.start(textFg->c);
	if (source == ButtonByUser || source == ButtonByPress || true) {
		_a_over.stop();
		a_textBgOverOpacity.finish();
		a_textFg.finish();
		update();
	} else {
		_a_over.start();
	}
}
