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
#include "switcher.h"

Switcher::Switcher(QWidget *parent, const style::switcher &st) : TWidget(parent)
, _selected(0)
, _over(-1)
, _wasOver(-1)
, _pressed(-1)
, _st(st)
, a_bgOver(_st.bgColor->c)
, a_bgWasOver(_st.bgHovered->c) {
	resize(width(), _st.height);
}

void Switcher::leaveEvent(QEvent *e) {
	setOver(-1);
	if (_pressed >= 0) return;

	setMouseTracking(false);
	return TWidget::leaveEvent(e);
}

void Switcher::enterEvent(QEvent *e) {
	setMouseTracking(true);
	return TWidget::enterEvent(e);
}

void Switcher::mousePressEvent(QMouseEvent *e) {
	if (e->buttons() & Qt::LeftButton) {
		mouseMoveEvent(e);
		if (_over != _pressed) {
			_pressed = _over;
			e->accept();
		}
	}
}

void Switcher::mouseMoveEvent(QMouseEvent *e) {
	if (rect().contains(e->pos())) {
		if (width()) {
			setOver((e->pos().x() * _buttons.size()) / width());
		}
	} else {
		setOver(-1);
	}
}

void Switcher::mouseReleaseEvent(QMouseEvent *e) {
	if (_pressed >= 0) {
		if (_pressed == _over && _pressed != _selected) {
			setSelected(_pressed);
		} else {
			setSelected(_selected);
		}
	} else {
		leaveEvent(e);
	}
}

void Switcher::addButton(const QString &btn) {
	_buttons.push_back(btn);
	update();
}

bool Switcher::animStep(float64 ms) {
	float64 dt = ms / _st.duration;
	bool res = true;
	if (dt >= 1) {
		res = false;
		a_bgOver.finish();
		a_bgWasOver.finish();
	} else {
		a_bgOver.update(dt, anim::linear);
		a_bgWasOver.update(dt, anim::linear);
	}
	update();
	return res;
}

void Switcher::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	p.fillRect(rect(), _st.bgColor->b);
	if (!_buttons.isEmpty()) {
		p.setFont(_st.font->f);
		float64 btnWidth = float64(width()) / _buttons.size();
		for (int i = 0; i < _buttons.size(); ++i) {
			QRect btnRect(qRound(i * btnWidth), 0, qRound((i + 1) * btnWidth) - qRound(i * btnWidth), height());
			if (i == _selected) {
				p.fillRect(btnRect, _st.bgActive->b);
			} else if (i == _over) {
				p.fillRect(btnRect, a_bgOver.current());
			} else if (i == _wasOver) {
				p.fillRect(btnRect, a_bgWasOver.current());
			}
			p.setPen((i == _selected ? _st.activeColor : _st.textColor)->p);
			p.drawText(btnRect, _buttons[i], style::al_center);
		}
	}
	if (_st.border) {
		p.fillRect(0, 0, width() - _st.border, _st.border, _st.borderColor->b);
		p.fillRect(width() - _st.border, 0, _st.border, height() - _st.border, _st.borderColor->b);
		p.fillRect(_st.border, height() - _st.border, width() - _st.border, _st.border, _st.borderColor->b);
		p.fillRect(0, _st.border, _st.border, height() - _st.border, _st.borderColor->b);
	}
}

int Switcher::selected() const {
	return _selected;
}

void Switcher::setSelected(int selected) {
	if (selected != _selected) {
		_selected = selected;
		emit changed();
	}
	_pressed = _over = _wasOver = -1;
	anim::stop(this);
	setCursor(style::cur_default);
	update();
}

void Switcher::setOver(int over) {
	if (over != _over) {
		QColor c(a_bgOver.current());
		if (_wasOver == over) {
			a_bgOver = anim::cvalue(a_bgWasOver.current(), _st.bgHovered->c);
		} else {
			a_bgOver = anim::cvalue(_st.bgColor->c, _st.bgHovered->c);
		}
		a_bgWasOver = anim::cvalue(c, _st.bgColor->c);

		_wasOver = _over;
		_over = over;

		anim::start(this);

		setCursor((_over >= 0 && _over != _selected) ? style::cur_pointer : style::cur_default);
	}
}
