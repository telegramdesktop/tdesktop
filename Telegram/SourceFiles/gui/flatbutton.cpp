/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "gui/flatbutton.h"

FlatButton::FlatButton(QWidget *parent, const QString &text, const style::flatButton &st) : Button(parent),
	_text(text),
	_st(st),
	a_bg(st.bgColor->c), a_text(st.color->c), _opacity(1) {
	if (_st.width < 0) {
		_st.width = _st.font->m.width(text) - _st.width;
	} else if (!_st.width) {
		_st.width = _st.font->m.width(text) + _st.height - _st.font->height;
	}
	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));
	resize(_st.width, _st.height);
	setCursor(_st.cursor);
}

void FlatButton::setOpacity(float64 o) {
	_opacity = o;
	update();
}

void FlatButton::setText(const QString &text) {
	_text = text;
	update();
}

void FlatButton::setWidth(int32 w) {
	_st.width = w;
	resize(_st.width, height());
}

bool FlatButton::animStep(float64 ms) {
	float64 dt = ms / _st.duration;
	bool res = true;
	if (dt >= 1) {
		a_bg.finish();
		a_text.finish();
		res = false;
	} else {
		a_bg.update(dt, anim::linear);
		a_text.update(dt, anim::linear);
	}
	update();
	return res;
}

void FlatButton::onStateChange(int oldState, ButtonStateChangeSource source) {
	style::color bgColorTo = (_state & StateOver) ? ((_state & StateDown) ? _st.downBgColor : _st.overBgColor) : _st.bgColor;
	style::color colorTo = (_state & StateOver) ? ((_state & StateDown) ? _st.downColor : _st.overColor) : _st.color;

	a_bg.start(bgColorTo->c);
	a_text.start(colorTo->c);
	if (source == ButtonByUser || source == ButtonByPress) {
		anim::stop(this);
		a_bg.finish();
		a_text.finish();
		update();
	} else {
		anim::start(this);
	}
}

void FlatButton::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	QRect r(0, height() - _st.height, width(), _st.height);

	p.setOpacity(_opacity);
	p.fillRect(r, a_bg.current());

	p.setFont(((_state & StateOver) ? _st.overFont : _st.font)->f);
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setPen(a_text.current());

	r.setTop((_state & StateOver) ? ((_state & StateDown) ? _st.downTextTop : _st.overTextTop) : _st.textTop);
	p.drawText(r, _text, QTextOption(Qt::AlignHCenter));
}

BottomButton::BottomButton(QWidget *w, const QString &t, const style::flatButton &s) : FlatButton(w, t, s) {
	resize(width(), height() + 1);
}

void BottomButton::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	p.fillRect(0, 0, width(), st::lineWidth, st::scrollDef.shColor->b);

	FlatButton::paintEvent(e);
}

LinkButton::LinkButton(QWidget *parent, const QString &text, const style::linkButton &st) : Button(parent), _text(text), _st(st) {
	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));
	resize(_st.font->m.width(_text), _st.font->height);
	setCursor(style::cur_pointer);
}

void LinkButton::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.setFont(((_state & StateOver) ? _st.overFont : _st.font)->f);
	p.setPen(((_state & StateDown) ? _st.downColor : ((_state & StateOver) ? _st.overColor : _st.color))->p);
	p.drawText(0, ((_state & StateOver) ? _st.overFont : _st.font)->ascent, _text);
}

void LinkButton::setText(const QString &text) {
	_text = text;
	resize(_st.font->m.width(_text), _st.font->height);
	update();
}

void LinkButton::onStateChange(int oldState, ButtonStateChangeSource source) {
	update();
}

LinkButton::~LinkButton() {
}

IconedButton::IconedButton(QWidget *parent, const style::iconedButton &st, const QString &text) : Button(parent),
	_text(text), _st(st), a_opacity(_st.opacity), a_bg(_st.bgColor->c), _opacity(1) {

	if (_st.width < 0) {
		_st.width = _st.font->m.width(text) - _st.width;
	} else if (!_st.width) {
		_st.width = _st.font->m.width(text) + _st.height - _st.font->height;
	}
	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));
	resize(_st.width, _st.height);
	setCursor(_st.cursor);
}

void IconedButton::setOpacity(float64 opacity) {
	_opacity = opacity;
	update();
}

bool IconedButton::animStep(float64 ms) {
	float64 dt = ms / _st.duration;
	bool res = true;
	if (dt >= 1) {
		a_opacity.finish();
		a_bg.finish();
		res = false;
	} else {
		a_opacity.update(dt, anim::linear);
		a_bg.update(dt, anim::linear);
	}
	update();
	return res;
}

void IconedButton::onStateChange(int oldState, ButtonStateChangeSource source) {
	a_opacity.start((_state & (StateOver | StateDown)) ? _st.overOpacity : _st.opacity);
	a_bg.start(((_state & (StateOver | StateDown)) ? _st.overBgColor : _st.bgColor)->c);

	if (source == ButtonByUser || source == ButtonByPress) {
		anim::stop(this);
		a_opacity.finish();
		a_bg.finish();
		update();
	} else {
		anim::start(this);
	}
}

void IconedButton::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	p.setOpacity(_opacity);

	p.fillRect(e->rect(), a_bg.current());

	p.setOpacity(a_opacity.current() * _opacity);

	if (!_text.isEmpty()) {
		p.setFont(_st.font->f);
		p.setRenderHint(QPainter::TextAntialiasing);
		p.setPen(_st.color->p);
		const QPoint &t((_state & StateDown) ? _st.downTextPos : _st.textPos);
		p.drawText(t.x(), t.y() + _st.font->ascent, _text);
	}
	const QRect &i((_state & StateDown) ? _st.downIcon : _st.icon);
	if (i.width()) {
		const QPoint &t((_state & StateDown) ? _st.downIconPos : _st.iconPos);
		p.drawPixmap(t, App::sprite(), i);
	}
}
