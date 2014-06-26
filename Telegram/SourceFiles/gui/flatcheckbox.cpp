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
#include "style.h"
#include "lang.h"

#include "flatcheckbox.h"

FlatCheckbox::FlatCheckbox(QWidget *parent, const QString &text, bool checked, const style::flatCheckbox &st) : Button(parent),
	_st(st), a_over(0, 0), _text(text), _opacity(1), _checked(checked) {
	connect(this, SIGNAL(clicked()), this, SLOT(onClicked()));
	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));
	setCursor(_st.cursor);
	int32 w = _st.width, h = _st.height;
	if (w <= 0) w = _st.textLeft + _st.font->m.width(_text) + 1;
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

	p.setFont(_st.font->f);
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setPen((_state & StateDisabled ? _st.disColor : _st.textColor)->p);

	QRect tRect(rect());
	tRect.setTop(_st.textTop);
	tRect.setLeft(_st.textLeft);
//    p.drawText(_st.textLeft, _st.textTop + _st.font->ascent, _text);
	p.drawText(tRect, _text, QTextOption(style::al_topleft));

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

class RadiobuttonsGroup : public QSet<FlatRadiobutton*> {
	typedef QSet<FlatRadiobutton*> Parent;

public:
	RadiobuttonsGroup(const QString &name) : _name(name), _val(0) {
	}

	void remove(FlatRadiobutton * const &radio);
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

class Radiobuttons : public QMap<QString, RadiobuttonsGroup*> {
	typedef QMap<QString, RadiobuttonsGroup*> Parent;

public:

	RadiobuttonsGroup *reg(const QString &group) {
		Parent::const_iterator i = constFind(group);
		if (i == cend()) {
			i = insert(group, new RadiobuttonsGroup(group));
		}
		return i.value();
	}

	int remove(const QString &group) {
		Parent::iterator i = find(group);
		if (i != cend()) {
			delete i.value();
			erase(i);
			return 1;
		}
		return 0;
	}

	~Radiobuttons() {
		for (Parent::const_iterator i = cbegin(), e = cend(); i != e; ++i) {
			delete *i;
		}
	}
};

namespace {
	Radiobuttons radioButtons;
}

void RadiobuttonsGroup::remove(FlatRadiobutton * const &radio) {
	Parent::remove(radio);
	if (isEmpty()) {
		radioButtons.remove(_name);
	}
}

FlatRadiobutton::FlatRadiobutton(QWidget *parent, const QString &group, int32 value, const QString &text, bool checked, const style::flatCheckbox &st) :
	FlatCheckbox(parent, text, checked, st), _group(radioButtons.reg(group)), _value(value) {
	_group->insert(this);
	connect(this, SIGNAL(changed()), this, SLOT(onChanged()));
	if (this->checked()) onChanged();
}

void FlatRadiobutton::onChanged() {
	if (checked()) {
		int32 uncheck = _group->val();
		if (uncheck != _value) {
			_group->setVal(_value);
			for (RadiobuttonsGroup::const_iterator i = _group->cbegin(), e = _group->cend(); i != e; ++i) {
				if ((*i)->val() == uncheck) {
					(*i)->setChecked(false);
				}
			}
		}
	} else if (_group->val() == _value) {
		setChecked(true);
	}
}

FlatRadiobutton::~FlatRadiobutton() {
	_group->remove(this);
}
