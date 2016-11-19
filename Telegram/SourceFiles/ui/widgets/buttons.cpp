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
#include "ui/widgets/buttons.h"

#include "ui/effects/ripple_animation.h"

namespace Ui {

LinkButton::LinkButton(QWidget *parent, const QString &text, const style::LinkButton &st) : AbstractButton(parent)
, _text(text)
, _textWidth(st.font->width(_text))
, _st(st) {
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

void LinkButton::onStateChanged(int oldState, StateChangeSource source) {
	update();
}

RippleButton::RippleButton(QWidget *parent, const style::RippleAnimation &st) : AbstractButton(parent)
, _st(st) {
}

void RippleButton::setForceRippled(bool rippled, SetForceRippledWay way) {
	if (_forceRippled != rippled) {
		_forceRippled = rippled;
		if (_forceRippled) {
			ensureRipple();
			if (_ripple->empty()) {
				_ripple->addFading();
			} else {
				_ripple->lastUnstop();
			}
		} else if (_ripple) {
			_ripple->lastStop();
		}
	}
	if (way == SetForceRippledWay::SkipAnimation && _ripple) {
		_ripple->lastFinish();
	}
	update();
}

void RippleButton::paintRipple(QPainter &p, int x, int y, uint64 ms) {
	if (_ripple) {
		_ripple->paint(p, x, y, width(), ms);
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}
}

void RippleButton::onStateChanged(int oldState, StateChangeSource source) {
	update();

	auto wasDown = (oldState & StateDown);
	auto down = (_state & StateDown);
	if (!_st.showDuration || down == wasDown || _forceRippled) {
		return;
	}

	if (down && (source == StateChangeSource::ByPress)) {
		// Start a ripple only from mouse press.
		ensureRipple();
		_ripple->add(prepareRippleStartPosition());
	} else if (!down && _ripple) {
		// Finish ripple anyway.
		_ripple->lastStop();
	}
}

void RippleButton::ensureRipple() {
	if (!_ripple) {
		_ripple = std_::make_unique<RippleAnimation>(_st, prepareRippleMask(), [this] { update(); });
	}
}

QImage RippleButton::prepareRippleMask() const {
	return RippleAnimation::rectMask(size());
}

QPoint RippleButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

RippleButton::~RippleButton() = default;

FlatButton::FlatButton(QWidget *parent, const QString &text, const style::FlatButton &st) : RippleButton(parent, st.ripple)
, _text(text)
, _st(st)
, a_over(0)
, _a_appearance(animation(this, &FlatButton::step_appearance)) {
	if (_st.width < 0) {
		_width = textWidth() - _st.width;
	} else if (!_st.width) {
		_width = textWidth() + _st.height - _st.font->height;
	} else {
		_width = _st.width;
	}
	resize(_width, _st.height);
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
	_width = w;
	if (_width < 0) {
		_width = textWidth() - _st.width;
	} else if (!_width) {
		_width = textWidth() + _st.height - _st.font->height;
	}
	resize(_width, height());
}

int32 FlatButton::textWidth() const {
	return _st.font->width(_text);
}

void FlatButton::step_appearance(float64 ms, bool timer) {
	float64 dt = ms / _st.duration;
	if (dt >= 1) {
		_a_appearance.stop();
		a_over.finish();
	} else {
		a_over.update(dt, anim::linear);
	}
	if (timer) update();
}

void FlatButton::onStateChanged(int oldState, StateChangeSource source) {
	RippleButton::onStateChanged(oldState, source);

	a_over.start((_state & StateOver) ? 1. : 0.);
	if (source == StateChangeSource::ByUser || source == StateChangeSource::ByPress) {
		_a_appearance.stop();
		a_over.finish();
		update();
	} else {
		_a_appearance.start();
	}
}

void FlatButton::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	QRect r(0, height() - _st.height, width(), _st.height);

	p.setOpacity(_opacity);
	p.fillRect(r, anim::brush(_st.bgColor, _st.overBgColor, a_over.current()));

	auto ms = getms();
	paintRipple(p, 0, 0, ms);

	p.setFont((_state & StateOver) ? _st.overFont : _st.font);
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setPen(anim::pen(_st.color, _st.overColor, a_over.current()));

	r.setTop(_st.textTop);
	p.drawText(r, _text, style::al_top);
}

RoundButton::RoundButton(QWidget *parent, const QString &text, const style::RoundButton &st) : RippleButton(parent, st.ripple)
, _fullText(text)
, _st(st) {
	setCursor(style::cur_pointer);
	updateText();
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

	auto innerWidth = contentWidth();
	auto rounded = rect().marginsRemoved(_st.padding);
	if (_fullWidthOverride < 0) {
		rounded = QRect(0, rounded.top(), innerWidth - _fullWidthOverride, rounded.height());
	}
	App::roundRect(p, myrtlrect(rounded), _st.textBg, ImageRoundRadius::Small);

	auto over = (_state & StateOver);
	auto down = (_state & StateDown);
	if (over || down) {
		App::roundRect(p, myrtlrect(rounded), _st.textBgOver, ImageRoundRadius::Small);
	}

	auto ms = getms();
	paintRipple(p, rounded.x(), rounded.y(), ms);

	p.setFont(_st.font);
	int textLeft = _st.padding.left() + ((width() - innerWidth - _st.padding.left() - _st.padding.right()) / 2);
	if (_fullWidthOverride < 0) {
		textLeft = -_fullWidthOverride / 2;
	}
	int textTop = _st.padding.top() + _st.textTop;
	if (!_text.isEmpty()) {
		p.setPen((over || down) ? _st.textFgOver : _st.textFg);
		p.drawTextLeft(textLeft, textTop, width(), _text);
	}
	if (!_secondaryText.isEmpty()) {
		textLeft += _textWidth + (_textWidth ? _st.secondarySkip : 0);
		p.setPen((over || down) ? _st.secondaryTextFgOver : _st.secondaryTextFg);
		p.drawTextLeft(textLeft, textTop, width(), _secondaryText);
	}
	_st.icon.paint(p, QPoint(_st.padding.left(), _st.padding.right()), width());
}

QImage RoundButton::prepareRippleMask() const {
	auto innerWidth = contentWidth();
	auto rounded = rtlrect(rect().marginsRemoved(_st.padding), width());
	if (_fullWidthOverride < 0) {
		rounded = QRect(0, rounded.top(), innerWidth - _fullWidthOverride, rounded.height());
	}
	return RippleAnimation::roundRectMask(rounded.size(), st::buttonRadius);
}

QPoint RoundButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - QPoint(_st.padding.left(), _st.padding.top());
}

IconButton::IconButton(QWidget *parent, const style::IconButton &st) : RippleButton(parent, st.ripple)
, _st(st) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);
}

void IconButton::setIcon(const style::icon *icon, const style::icon *iconOver) {
	_iconOverride = icon;
	_iconOverrideOver = iconOver;
	update();
}

void IconButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();

	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y(), ms);

	auto down = (_state & StateDown);
	auto overIconOpacity = (down || forceRippled()) ? 1. : _a_over.current(getms(), (_state & StateOver) ? 1. : 0.);
	auto overIcon = [this] {
		if (_iconOverrideOver) {
			return _iconOverrideOver;
		} else if (!_st.iconOver.empty()) {
			return &_st.iconOver;
		} else if (_iconOverride) {
			return _iconOverride;
		}
		return &_st.icon;
	};
	auto justIcon = [this] {
		if (_iconOverride) {
			return _iconOverride;
		}
		return &_st.icon;
	};
	auto icon = (overIconOpacity == 1.) ? overIcon() : justIcon();
	auto position = _st.iconPosition;
	if (position.x() < 0) {
		position.setX((width() - icon->width()) / 2);
	}
	if (position.y() < 0) {
		position.setY((height() - icon->height()) / 2);
	}
	icon->paint(p, position, width());
	if (overIconOpacity > 0. && overIconOpacity < 1.) {
		auto iconOver = overIcon();
		if (iconOver != icon) {
			p.setOpacity(overIconOpacity);
			iconOver->paint(p, position, width());
		}
	}
}

void IconButton::onStateChanged(int oldState, StateChangeSource source) {
	RippleButton::onStateChanged(oldState, source);

	auto over = (_state & StateOver);
	if (over != (oldState & StateOver)) {
		if (_st.duration) {
			auto from = over ? 0. : 1.;
			auto to = over ? 1. : 0.;
			_a_over.start([this] { update(); }, from, to, _st.duration);
		} else {
			update();
		}
	}
}

QPoint IconButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
}

QImage IconButton::prepareRippleMask() const {
	return RippleAnimation::ellipseMask(QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

LeftOutlineButton::LeftOutlineButton(QWidget *parent, const QString &text, const style::OutlineButton &st) : RippleButton(parent, st.ripple)
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

int LeftOutlineButton::resizeGetHeight(int newWidth) {
	int availableWidth = qMax(newWidth - _st.padding.left() - _st.padding.right(), 1);
	if ((availableWidth < _fullTextWidth) || (_textWidth < availableWidth)) {
		_text = _st.font->elided(_fullText, availableWidth);
		_textWidth = _st.font->width(_text);
	}
	return _st.padding.top() + _st.font->height + _st.padding.bottom();
}

void LeftOutlineButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	bool over = (_state & StateOver), down = (_state & StateDown);
	if (width() > _st.outlineWidth) {
		p.fillRect(rtlrect(_st.outlineWidth, 0, width() - _st.outlineWidth, height(), width()), (over || down) ? _st.textBgOver : _st.textBg);
		paintRipple(p, 0, 0, getms());
		p.fillRect(rtlrect(0, 0, _st.outlineWidth, height(), width()), (over || down) ? _st.outlineFgOver : _st.outlineFg);
	}
	p.setFont(_st.font);
	p.setPen((over || down) ? _st.textFgOver : _st.textFg);
	p.drawTextLeft(_st.padding.left(), _st.padding.top(), width(), _text, _textWidth);
}

} // namespace Ui
