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
#include "style.h"
#include "lang.h"

#include "flatcheckbox.h"

FlatCheckbox::FlatCheckbox(QWidget *parent, const QString &text, bool checked, const style::flatCheckbox &st) : Button(parent),
	_st(st), a_over(0, 0), _text(text), _opacity(1), _checked(checked) {
	connect(this, SIGNAL(clicked()), this, SLOT(onClicked()));
	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));
	setCursor(_st.cursor);
	int32 w = _st.width, h = _st.height;
    if (w <= 0) w = _st.textLeft + _st.font->width(_text) + 2;
	if (h <= 0) h = qMax(_st.font->height, _st.imageRect.pxHeight());
	resize(QSize(w, h));
}

bool FlatCheckbox::checked() const {
	return _checked;
}

void FlatCheckbox::setChecked(bool checked) {
	if (_checked != checked) {
		_checked = checked;
		emit changed();
		update();
	}
}

void FlatCheckbox::setOpacity(float64 o) {
	_opacity = o;
	update();
}

void FlatCheckbox::onClicked() {
	if (_state & StateDisabled) return;
	setChecked(!checked());
}

void FlatCheckbox::onStateChange(int oldState, ButtonStateChangeSource source) {
	if ((_state & StateOver) && !(oldState & StateOver)) {
		a_over.start(1);
		anim::start(this);
	} else if (!(_state & StateOver) && (oldState & StateOver)) {
		a_over.start(0);
		anim::start(this);
	}
	if ((_state & StateDisabled) && !(oldState & StateDisabled)) {
		setCursor(_st.disabledCursor);
		anim::start(this);
	} else if (!(_state & StateDisabled) && (oldState & StateDisabled)) {
		setCursor(_st.cursor);
		anim::start(this);
	}
}

void FlatCheckbox::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	p.setOpacity(_opacity);
	if (_st.bgColor != st::transparent) {
		p.fillRect(rect(), _st.bgColor->b);
	}

	if (!_text.isEmpty()) {
		p.setFont(_st.font->f);
		p.setRenderHint(QPainter::TextAntialiasing);
		p.setPen((_state & StateDisabled ? _st.disColor : _st.textColor)->p);

		QRect tRect(rect());
		tRect.setTop(_st.textTop);
		tRect.setLeft(_st.textLeft);
//		p.drawText(_st.textLeft, _st.textTop + _st.font->ascent, _text);
		p.drawText(tRect, _text, QTextOption(style::al_topleft));
	}

	if (_state & StateDisabled) {
		QRect sRect(_checked ? _st.chkDisImageRect : _st.disImageRect);
		p.drawPixmap(_st.imagePos, App::sprite(), sRect);
	} else if ((_checked && _st.chkImageRect == _st.chkOverImageRect) || (!_checked && _st.imageRect == _st.overImageRect)) {
		p.setOpacity(_opacity);
		QRect sRect(_checked ? _st.chkImageRect : _st.imageRect);
		p.drawPixmap(_st.imagePos, App::sprite(), sRect);
	} else {
		if (a_over.current() < 1) {
			QRect sRect(_checked ? _st.chkImageRect : _st.imageRect);
			p.drawPixmap(_st.imagePos, App::sprite(), sRect);
		}
		if (a_over.current() > 0) {
			p.setOpacity(_opacity * a_over.current());
			QRect sRect(_checked ? _st.chkOverImageRect : _st.overImageRect);
			p.drawPixmap(_st.imagePos, App::sprite(), sRect);
		}
	}
}

bool FlatCheckbox::animStep(float64 ms) {
	float64 dt = ms / _st.duration;
	bool res = true;
	if (dt >= 1) {
		a_over.finish();
		res = false;
	} else {
		a_over.update(dt, _st.bgFunc);
	}
	update();
	return res;
}

template <typename Type>
class TemplateRadiobuttonsGroup : public QMap<Type*, bool> {
	typedef QMap<Type*, bool> Parent;

public:
	TemplateRadiobuttonsGroup(const QString &name) : _name(name), _val(0) {
	}

	void remove(Type * const &radio);
	int32 val() const {
		return _val;
	}
	void setVal(int32 val) {
		_val = val;
	}

private:
	QString _name;
	int32 _val;

};

typedef TemplateRadiobuttonsGroup<FlatRadiobutton> FlatRadiobuttonGroup;
typedef TemplateRadiobuttonsGroup<Radiobutton> RadiobuttonGroup;

template <typename Type>
class Radiobuttons : public QMap<QString, TemplateRadiobuttonsGroup<Type> *> {
	typedef QMap<QString, TemplateRadiobuttonsGroup<Type> *> Parent;

public:

	TemplateRadiobuttonsGroup<Type> *reg(const QString &group) {
		typename Parent::const_iterator i = Parent::constFind(group);
		if (i == Parent::cend()) {
			i = Parent::insert(group, new TemplateRadiobuttonsGroup<Type>(group));
		}
		return i.value();
	}

	int remove(const QString &group) {
		typename Parent::iterator i = Parent::find(group);
		if (i != Parent::cend()) {
			delete i.value();
			Parent::erase(i);
			return 1;
		}
		return 0;
	}

	~Radiobuttons() {
		for (typename Parent::const_iterator i = Parent::cbegin(), e = Parent::cend(); i != e; ++i) {
			delete *i;
		}
	}
};

namespace {
	Radiobuttons<FlatRadiobutton> flatRadiobuttons;
	Radiobuttons<Radiobutton> radiobuttons;
}

template <>
void TemplateRadiobuttonsGroup<FlatRadiobutton>::remove(FlatRadiobutton * const &radio) {
	Parent::remove(radio);
	if (isEmpty()) {
		flatRadiobuttons.remove(_name);
	}
}

template <>
void TemplateRadiobuttonsGroup<Radiobutton>::remove(Radiobutton * const &radio) {
	Parent::remove(radio);
	if (isEmpty()) {
		radiobuttons.remove(_name);
	}
}

FlatRadiobutton::FlatRadiobutton(QWidget *parent, const QString &group, int32 value, const QString &text, bool checked, const style::flatCheckbox &st) :
	FlatCheckbox(parent, text, checked, st), _group(flatRadiobuttons.reg(group)), _value(value) {
	reinterpret_cast<FlatRadiobuttonGroup*>(_group)->insert(this, true);
	connect(this, SIGNAL(changed()), this, SLOT(onChanged()));
	if (this->checked()) onChanged();
}

void FlatRadiobutton::onChanged() {
	FlatRadiobuttonGroup *group = reinterpret_cast<FlatRadiobuttonGroup*>(_group);
	if (checked()) {
		int32 uncheck = group->val();
		if (uncheck != _value) {
			group->setVal(_value);
			for (FlatRadiobuttonGroup::const_iterator i = group->cbegin(), e = group->cend(); i != e; ++i) {
				if (i.key()->val() == uncheck) {
					i.key()->setChecked(false);
				}
			}
		}
	} else if (group->val() == _value) {
		setChecked(true);
	}
}

FlatRadiobutton::~FlatRadiobutton() {
	reinterpret_cast<FlatRadiobuttonGroup*>(_group)->remove(this);
}

Checkbox::Checkbox(QWidget *parent, const QString &text, bool checked, const style::Checkbox &st) : Button(parent),
_st(st),
a_over(0), a_checked(checked ? 1 : 0),
_a_over(animFunc(this, &Checkbox::animStep_over)), _a_checked(animFunc(this, &Checkbox::animStep_checked)),
_text(text), _fullText(text), _textWidth(st.font->width(text)),
_checked(checked) {
	if (_st.width <= 0) {
		resize(_textWidth - _st.width, _st.height);
	} else {
		if (_st.width < _st.textPosition.x() + _textWidth + (_st.textPosition.x() - _st.diameter)) {
			_text = _st.font->elided(_fullText, qMax(_st.width - (_st.textPosition.x() + (_st.textPosition.x() - _st.diameter)), 1.));
			_textWidth = _st.font->width(_text);
		}
		resize(_st.width, _st.height);
	}
	_checkRect = myrtlrect(0, 0, _st.diameter, _st.diameter);

	connect(this, SIGNAL(clicked()), this, SLOT(onClicked()));
	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));

	setCursor(style::cur_pointer);

	setAttribute(Qt::WA_OpaquePaintEvent);
}

bool Checkbox::checked() const {
	return _checked;
}

void Checkbox::setChecked(bool checked) {
	if (_checked != checked) {
		_checked = checked;
		if (_checked) {
			a_checked.start(1);
		} else {
			a_checked.start(0);
		}
		_a_checked.start();

		emit changed();
	}
}

bool Checkbox::animStep_over(float64 ms) {
	float64 dt = ms / _st.duration;
	bool res = true;
	if (dt >= 1) {
		a_over.finish();
		res = false;
	} else {
		a_over.update(dt, anim::linear);
	}
	update(_checkRect);
	return res;
}

bool Checkbox::animStep_checked(float64 ms) {
	float64 dt = ms / _st.duration;
	bool res = true;
	if (dt >= 1) {
		a_checked.finish();
		res = false;
	} else {
		a_checked.update(dt, anim::linear);
	}
	update(_checkRect);
	return res;
}

void Checkbox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	float64 over = a_over.current(), checked = a_checked.current();
	bool cnone = (over == 0. && checked == 0.), cover = (over == 1. && checked == 0.), cchecked = (checked == 1.);
	bool cbad = !cnone && !cover && !cchecked;
	QColor color;
	if (cbad) {
		float64 onone = (1. - over) * (1. - checked), oover = over * (1. - checked), ochecked = checked;
		color.setRedF(_st.checkFg->c.redF() * onone + _st.checkFgOver->c.redF() * oover + _st.checkFgActive->c.redF() * ochecked);
		color.setGreenF(_st.checkFg->c.greenF() * onone + _st.checkFgOver->c.greenF() * oover + _st.checkFgActive->c.greenF() * ochecked);
		color.setBlueF(_st.checkFg->c.blueF() * onone + _st.checkFgOver->c.blueF() * oover + _st.checkFgActive->c.blueF() * ochecked);
	}

	QRect r(e->rect());
	p.setClipRect(r);
	p.fillRect(r, _st.textBg->b);
	if (_checkRect.intersects(r)) {
		p.setRenderHint(QPainter::HighQualityAntialiasing);

		QPen pen;
		if (cbad) {
			pen = QPen(color);
		} else {
			pen = (cnone ? _st.checkFg : (cover ? _st.checkFgOver : _st.checkFgActive))->p;
			color = (cnone ? _st.checkFg : (cover ? _st.checkFgOver : _st.checkFgActive))->c;
		}
		pen.setWidth(_st.thickness);
		p.setPen(pen);
		if (checked > 0) {
			color.setAlphaF(checked);
			p.setBrush(color);
		} else {
			p.setBrush(Qt::NoBrush);
		}
		p.drawRoundedRect(QRectF(_checkRect).marginsRemoved(QMarginsF(_st.thickness / 2, _st.thickness / 2, _st.thickness / 2, _st.thickness / 2)), st::msgRadius, st::msgRadius);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);

		if (checked > 0) {
			p.drawSpriteCenter(_checkRect, _st.checkIcon);
		}
	}
	if (_checkRect.contains(r)) return;

	p.setPen(_st.textFg);
	p.setFont(_st.font);
	p.drawTextLeft(_st.textPosition.x(), _st.textPosition.y(), width(), _text, _textWidth);
}

void Checkbox::onClicked() {
	if (_state & StateDisabled) return;
	setChecked(!checked());
}

void Checkbox::onStateChange(int oldState, ButtonStateChangeSource source) {
	if ((_state & StateOver) && !(oldState & StateOver)) {
		a_over.start(1);
		_a_over.start();
	} else if (!(_state & StateOver) && (oldState & StateOver)) {
		a_over.start(0);
		_a_over.start();
	}
	if ((_state & StateDisabled) && !(oldState & StateDisabled)) {
		setCursor(style::cur_default);
	} else if (!(_state & StateDisabled) && (oldState & StateDisabled)) {
		setCursor(style::cur_pointer);
	}
}

Radiobutton::Radiobutton(QWidget *parent, const QString &group, int32 value, const QString &text, bool checked, const style::Radiobutton &st) : Button(parent),
_st(st),
a_over(0), a_checked(checked ? 1 : 0),
_a_over(animFunc(this, &Radiobutton::animStep_over)), _a_checked(animFunc(this, &Radiobutton::animStep_checked)),
_text(text), _fullText(text), _textWidth(st.font->width(text)),
_checked(checked), _group(radiobuttons.reg(group)), _value(value) {
	if (_st.width <= 0) {
		resize(_textWidth - _st.width, _st.height);
	} else {
		if (_st.width < _st.textPosition.x() + _textWidth + (_st.textPosition.x() - _st.diameter)) {
			_text = _st.font->elided(_fullText, qMax(_st.width - (_st.textPosition.x() + (_st.textPosition.x() - _st.diameter)), 1.));
			_textWidth = _st.font->width(_text);
		}
		resize(_st.width, _st.height);
	}
	_checkRect = myrtlrect(0, 0, _st.diameter, _st.diameter);

	connect(this, SIGNAL(clicked()), this, SLOT(onClicked()));
	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));

	setCursor(style::cur_pointer);

	setAttribute(Qt::WA_OpaquePaintEvent);

	reinterpret_cast<RadiobuttonGroup*>(_group)->insert(this, true);
	if (_checked) onChanged();
}

bool Radiobutton::checked() const {
	return _checked;
}

void Radiobutton::setChecked(bool checked) {
	if (_checked != checked) {
		_checked = checked;
		if (_checked) {
			a_checked.start(1);
		} else {
			a_checked.start(0);
		}
		_a_checked.start();

		onChanged();
		emit changed();
	}
}

bool Radiobutton::animStep_over(float64 ms) {
	float64 dt = ms / _st.duration;
	bool res = true;
	if (dt >= 1) {
		a_over.finish();
		res = false;
	} else {
		a_over.update(dt, anim::linear);
	}
	update(_checkRect);
	return res;
}

bool Radiobutton::animStep_checked(float64 ms) {
	float64 dt = ms / _st.duration;
	bool res = true;
	if (dt >= 1) {
		a_checked.finish();
		res = false;
	} else {
		a_checked.update(dt, anim::linear);
	}
	update(_checkRect);
	return res;
}

void Radiobutton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	float64 over = a_over.current(), checked = a_checked.current();
	bool cnone = (over == 0. && checked == 0.), cover = (over == 1. && checked == 0.), cchecked = (checked == 1.);
	bool cbad = !cnone && !cover && !cchecked;
	QColor color;
	if (cbad) {
		float64 onone = (1. - over) * (1. - checked), oover = over * (1. - checked), ochecked = checked;
		color.setRedF(_st.checkFg->c.redF() * onone + _st.checkFgOver->c.redF() * oover + _st.checkFgActive->c.redF() * ochecked);
		color.setGreenF(_st.checkFg->c.greenF() * onone + _st.checkFgOver->c.greenF() * oover + _st.checkFgActive->c.greenF() * ochecked);
		color.setBlueF(_st.checkFg->c.blueF() * onone + _st.checkFgOver->c.blueF() * oover + _st.checkFgActive->c.blueF() * ochecked);
	}

	QRect r(e->rect());
	p.setClipRect(r);
	p.fillRect(r, _st.textBg->b);
	if (_checkRect.intersects(r)) {
		p.setRenderHint(QPainter::HighQualityAntialiasing);

		QPen pen;
		if (cbad) {
			pen = QPen(color);
		} else {
			pen = (cnone ? _st.checkFg : (cover ? _st.checkFgOver : _st.checkFgActive))->p;
		}
		pen.setWidth(_st.thickness);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		//int32 skip = qCeil(_st.thickness / 2);
		//p.drawEllipse(_checkRect.marginsRemoved(QMargins(skip, skip, skip, skip)));
		p.drawEllipse(QRectF(_checkRect).marginsRemoved(QMarginsF(_st.thickness / 2, _st.thickness / 2, _st.thickness / 2, _st.thickness / 2)));

		if (checked > 0) {
			p.setPen(Qt::NoPen);
			if (cbad) {
				p.setBrush(color);
			} else {
				p.setBrush(cnone ? _st.checkFg : (cover ? _st.checkFgOver : _st.checkFgActive));
			}
			float64 skip0 = _checkRect.width() / 2., skip1 = _st.checkSkip / 10., checkSkip = skip0 * (1. - checked) + skip1 * checked;
			p.drawEllipse(QRectF(_checkRect).marginsRemoved(QMarginsF(checkSkip, checkSkip, checkSkip, checkSkip)));
			//int32 fskip = qFloor(checkSkip), cskip = qCeil(checkSkip);
			//if (2 * fskip < _checkRect.width()) {
			//	if (fskip != cskip) {
			//		p.setOpacity(float64(cskip) - checkSkip);
			//		p.drawEllipse(_checkRect.marginsRemoved(QMargins(fskip, fskip, fskip, fskip)));
			//		p.setOpacity(1.);
			//	}
			//	if (2 * cskip < _checkRect.width()) {
			//		p.drawEllipse(_checkRect.marginsRemoved(QMargins(cskip, cskip, cskip, cskip)));
			//	}
			//}
		}

		p.setRenderHint(QPainter::HighQualityAntialiasing, false);
	}
	if (_checkRect.contains(r)) return;

	p.setPen(_st.textFg);
	p.setFont(_st.font);
	p.drawTextLeft(_st.textPosition.x(), _st.textPosition.y(), width(), _text, _textWidth);
}

void Radiobutton::onClicked() {
	if (_state & StateDisabled) return;
	setChecked(!checked());
}

void Radiobutton::onStateChange(int oldState, ButtonStateChangeSource source) {
	if ((_state & StateOver) && !(oldState & StateOver)) {
		a_over.start(1);
		_a_over.start();
	} else if (!(_state & StateOver) && (oldState & StateOver)) {
		a_over.start(0);
		_a_over.start();
	}
	if ((_state & StateDisabled) && !(oldState & StateDisabled)) {
		setCursor(style::cur_default);
	} else if (!(_state & StateDisabled) && (oldState & StateDisabled)) {
		setCursor(style::cur_pointer);
	}
}

void Radiobutton::onChanged() {
	RadiobuttonGroup *group = reinterpret_cast<RadiobuttonGroup*>(_group);
	if (checked()) {
		int32 uncheck = group->val();
		if (uncheck != _value) {
			group->setVal(_value);
			for (RadiobuttonGroup::const_iterator i = group->cbegin(), e = group->cend(); i != e; ++i) {
				if (i.key()->val() == uncheck) {
					i.key()->setChecked(false);
				}
			}
		}
	} else if (group->val() == _value) {
		setChecked(true);
	}
}

Radiobutton::~Radiobutton() {
	reinterpret_cast<RadiobuttonGroup*>(_group)->remove(this);
}
