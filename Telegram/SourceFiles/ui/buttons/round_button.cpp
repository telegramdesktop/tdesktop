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

RoundButton::RoundButton(QWidget *parent, const QString &text, const style::RoundButton &st) : Button(parent)
, _text(text)
, _fullText(text)
, _textWidth(st.font->width(_text))
, _st(st)
, a_textBgOverOpacity(0)
, a_textFg(st.textFg->c)
, a_secondaryTextFg(st.secondaryTextFg->c)
, _a_over(animation(this, &RoundButton::step_over)) {
	resizeToText();

	setCursor(style::cur_pointer);
}

void RoundButton::setTextTransform(TextTransform transform) {
	_transform = transform;
	updateText();
}

void RoundButton::setText(const QString &text) {
	_fullText = text;
	updateText();
}

void RoundButton::setSecondaryText(const QString &secondaryText) {
	_fullSecondaryText = secondaryText;
	updateText();
}

void RoundButton::setFullWidth(int newFullWidth) {
	_fullWidthOverride = newFullWidth;
	resizeToText();
}

void RoundButton::updateText() {
	if (_transform == TextTransform::ToUpper) {
		_text = _fullText.toUpper();
		_secondaryText = _fullSecondaryText.toUpper();
	} else {
		_text = _fullText;
		_secondaryText = _fullSecondaryText;
	}
	_textWidth = _text.isEmpty() ? 0 : _st.font->width(_text);
	_secondaryTextWidth = _secondaryText.isEmpty() ? 0 : _st.font->width(_secondaryText);

	resizeToText();
}

void RoundButton::resizeToText() {
	int innerWidth = contentWidth();
	if (_fullWidthOverride < 0) {
		resize(innerWidth - _fullWidthOverride, _st.height + _st.padding.top() + _st.padding.bottom());
	} else if (_st.width <= 0) {
		resize(innerWidth - _st.width + _st.padding.left() + _st.padding.right(), _st.height + _st.padding.top() + _st.padding.bottom());
	} else {
		if (_st.width < innerWidth + (_st.height - _st.font->height)) {
			_text = _st.font->elided(_fullText, qMax(_st.width - (_st.height - _st.font->height), 1));
			innerWidth = _st.font->width(_text);
		}
		resize(_st.width + _st.padding.left() + _st.padding.right(), _st.height + _st.padding.top() + _st.padding.bottom());
	}
}

int RoundButton::contentWidth() const {
	int result = _textWidth + _secondaryTextWidth;
	if (_textWidth > 0 && _secondaryTextWidth > 0) {
		result += _st.secondarySkip;
	}
	return result;
}

void RoundButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	int innerWidth = contentWidth();
	auto rounded = rtlrect(rect().marginsRemoved(_st.padding), width());
	if (_fullWidthOverride < 0) {
		rounded = QRect(0, rounded.top(), innerWidth - _fullWidthOverride, rounded.height());
	}
	App::roundRect(p, rounded, _st.textBg, ImageRoundRadius::Small);

	auto o = a_textBgOverOpacity.current();
	if (o > 0) {
		p.setOpacity(o);
		App::roundRect(p, rounded, _st.textBgOver, ImageRoundRadius::Small);
		p.setOpacity(1);
	}

	p.setFont(_st.font);
	int textLeft = _st.padding.left() + ((width() - innerWidth - _st.padding.left() - _st.padding.right()) / 2);
	if (_fullWidthOverride < 0) {
		textLeft = -_fullWidthOverride / 2;
	}
	int textTopDelta = (_state & StateDown) ? (_st.downTextTop - _st.textTop) : 0;
	int textTop = _st.padding.top() + _st.textTop + textTopDelta;
	if (!_text.isEmpty()) {
		if (o > 0) {
			p.setPen(a_textFg.current());
		} else {
			p.setPen(_st.textFg);
		}
		p.drawTextLeft(textLeft, textTop, width(), _text);
	}
	if (!_secondaryText.isEmpty()) {
		textLeft += _textWidth + (_textWidth ? _st.secondarySkip : 0);
		if (o > 0) {
			p.setPen(a_secondaryTextFg.current());
		} else {
			p.setPen(_st.secondaryTextFg);
		}
		p.drawTextLeft(textLeft, textTop, width(), _secondaryText);
	}
	_st.icon.paint(p, QPoint(_st.padding.left(), _st.padding.right() + textTopDelta), width());
}

void RoundButton::step_over(float64 ms, bool timer) {
	float64 dt = ms / _st.duration;
	if (dt >= 1) {
		_a_over.stop();
		a_textFg.finish();
		a_secondaryTextFg.finish();
		a_textBgOverOpacity.finish();
	} else {
		a_textFg.update(dt, anim::linear);
		a_secondaryTextFg.update(dt, anim::linear);
		a_textBgOverOpacity.update(dt, anim::linear);
	}
	if (timer) update();
}

void RoundButton::onStateChanged(int oldState, ButtonStateChangeSource source) {
	auto textBgOverOpacity = (_state & StateOver) ? 1. : 0.;
	auto textFg = (_state & StateOver) ? (_st.textFgOver) : _st.textFg;
	auto secondaryTextFg = (_state & StateOver) ? (_st.secondaryTextFgOver) : _st.secondaryTextFg;

	a_textBgOverOpacity.start(textBgOverOpacity);
	a_textFg.start(textFg->c);
	a_secondaryTextFg.start(secondaryTextFg->c);
	if (source == ButtonByUser || source == ButtonByPress || true) {
		_a_over.stop();
		a_textFg.finish();
		a_secondaryTextFg.finish();
		a_textBgOverOpacity.finish();
		update();
	} else {
		_a_over.start();
	}
}

} // namespace Ui
