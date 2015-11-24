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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "gui/flatbutton.h"

FlatButton::FlatButton(QWidget *parent, const QString &text, const style::flatButton &st) : Button(parent),
	_text(text),
	_st(st), _autoFontPadding(0),
	a_bg(st.bgColor->c), a_text(st.color->c), _opacity(1) {
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

void FlatButton::setAutoFontSize(int32 padding, const QString &txt) {
	_autoFontPadding = padding;
	if (_autoFontPadding) {
		_textForAutoSize = txt;
		resizeEvent(0);
	} else {
		_textForAutoSize = QString();
		_autoFont = style::font();
	}
	update();
}

int32 FlatButton::textWidth() const {
	return _st.font->width(_text);
}

void FlatButton::resizeEvent(QResizeEvent *e) {
	if (_autoFontPadding) {
		_autoFont = _st.font;
		for (int32 s = _st.font->f.pixelSize(); s >= st::fsize; --s) {
			_autoFont = style::font(s, _st.font->flags(), _st.font->family());
			if (2 * _autoFontPadding + _autoFont->width(_textForAutoSize) <= width()) {
				break;
			}
		}
	}
	return Button::resizeEvent(e);
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

	p.setFont((_autoFont ? _autoFont : ((_state & StateOver) ? _st.overFont : _st.font))->f);
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setPen(a_text.current());

	int32 top = (_state & StateOver) ? ((_state & StateDown) ? _st.downTextTop : _st.overTextTop) : _st.textTop;
	if (_autoFont) top += (_st.font->height - _autoFont->height) / 2;
	r.setTop(top);

	p.drawText(r, _text, style::al_top);
}

LinkButton::LinkButton(QWidget *parent, const QString &text, const style::linkButton &st) : Button(parent), _text(text), _st(st) {
	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));
	resize(_st.font->width(_text), _st.font->height);
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
	resize(_st.font->width(_text), _st.font->height);
	update();
}

void LinkButton::onStateChange(int oldState, ButtonStateChangeSource source) {
	update();
}

LinkButton::~LinkButton() {
}

IconedButton::IconedButton(QWidget *parent, const style::iconedButton &st, const QString &text) : Button(parent),
	_text(text), _st(st), _width(_st.width), a_opacity(_st.opacity), a_bg(_st.bgColor->c), _opacity(1) {

	if (_width < 0) {
		_width = _st.font->width(text) - _width;
	} else if (!_width) {
		_width = _st.font->width(text) + _st.height - _st.font->height;
	}
	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));
	resize(_width, _st.height);
	setCursor(_st.cursor);
}

void IconedButton::setOpacity(float64 opacity) {
	_opacity = opacity;
	update();
}

void IconedButton::setText(const QString &text) {
	if (_text != text) {
		_text = text;
		if (_st.width < 0) {
			_width = _st.font->width(text) - _st.width;
		} else if (!_st.width) {
			_width = _st.font->width(text) + _st.height - _st.font->height;
		}
		resize(_width, _st.height);
		update();
	}
}

QString IconedButton::getText() const {
	return _text;
}

bool IconedButton::animStep(float64 ms) {
	bool res = true;
	if (_st.duration <= 1) {
		a_opacity.finish();
		a_bg.finish();
		res = false;
	} else {
		float64 dt = ms / _st.duration;
		if (dt >= 1) {
			a_opacity.finish();
			a_bg.finish();
			res = false;
		} else {
			a_opacity.update(dt, anim::linear);
			a_bg.update(dt, anim::linear);
		}
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

MaskedButton::MaskedButton(QWidget *parent, const style::iconedButton &st, const QString &text) : IconedButton(parent, st, text) {
}

void MaskedButton::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	p.setOpacity(_opacity);

	p.setOpacity(a_opacity.current() * _opacity);

	if (!_text.isEmpty()) {
		p.setFont(_st.font->f);
		p.setRenderHint(QPainter::TextAntialiasing);
		p.setPen(a_bg.current());
		const QPoint &t((_state & StateDown) ? _st.downTextPos : _st.textPos);
		p.drawText(t.x(), t.y() + _st.font->ascent, _text);
	}

	const style::sprite &i((_state & StateDown) ? _st.downIcon : _st.icon);
	if (i.pxWidth()) {
		const QPoint &t((_state & StateDown) ? _st.downIconPos : _st.iconPos);
		p.fillRect(QRect(t, QSize(i.pxWidth(), i.pxHeight())), a_bg.current());
		p.drawPixmap(t, App::sprite(), i);
	}
}

BoxButton::BoxButton(QWidget *parent, const QString &text, const style::BoxButton &st) : Button(parent),
_text(text.toUpper()), _fullText(text.toUpper()), _textWidth(st.font->width(_text)),
_st(st),
a_textBgOverOpacity(0), a_textFg(st.textFg->c), _a_over(animFunc(this, &BoxButton::animStep_over)) {
	if (_st.width <= 0) {
		resize(_textWidth - _st.width, _st.height);
	} else {
		if (_st.width < _textWidth + (_st.height - _st.font->height)) {
			_text = _st.font->elided(_fullText, qMax(_st.width - (_st.height - _st.font->height), 1.));
			_textWidth = _st.font->width(_text);
		}
		resize(_st.width, _st.height);
	}

	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));

	setCursor(style::cur_pointer);

	setAttribute(Qt::WA_OpaquePaintEvent);
}

void BoxButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(rect(), _st.textBg->b);

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

bool BoxButton::animStep_over(float64 ms) {
	float64 dt = ms / _st.duration;
	bool res = true;
	if (dt >= 1) {
		a_textFg.finish();
		a_textBgOverOpacity.finish();
		res = false;
	} else {
		a_textFg.update(dt, anim::linear);
		a_textBgOverOpacity.update(dt, anim::linear);
	}
	update();
	return res;
}

void BoxButton::onStateChange(int oldState, ButtonStateChangeSource source) {
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
