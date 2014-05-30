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
#include "button.h"

Button::Button(QWidget *parent) : TWidget(parent), _state(StateNone), _acceptBoth(false) {
}

void Button::leaveEvent(QEvent *e) {
	if (_state & StateDown) return;

	if (_state & StateOver) {
		int oldState = _state;
		_state &= ~StateOver;
		emit stateChanged(oldState, ButtonByHover);
	}
	setMouseTracking(false);
	return TWidget::leaveEvent(e);
}

void Button::enterEvent(QEvent *e) {
	if (!(_state & StateOver)) {
		int oldState = _state;
		_state |= StateOver;
		emit stateChanged(oldState, ButtonByHover);
	}
	setMouseTracking(true);
	return TWidget::enterEvent(e);
}

void Button::setAcceptBoth(bool acceptBoth) {
	_acceptBoth = acceptBoth;
}

void Button::mousePressEvent(QMouseEvent *e) {
	if (_acceptBoth || e->buttons() & Qt::LeftButton) {
		if (!(_state & StateOver)) {
			enterEvent(0);
		}
		if (!(_state & StateDown)) {
			int oldState = _state;
			_state |= StateDown;
			emit stateChanged(oldState, ButtonByPress);

			e->accept();
		}
	}
}

void Button::mouseMoveEvent(QMouseEvent *e) {
	if (rect().contains(e->pos())) {
		if (!(_state & StateOver)) {
			int oldState = _state;
			_state |= StateOver;
			emit stateChanged(oldState, ButtonByHover);
		}
	} else {
		if (_state & StateOver) {
			int oldState = _state;
			_state &= ~StateOver;
			emit stateChanged(oldState, ButtonByHover);
		}
	}
}

void Button::mouseReleaseEvent(QMouseEvent *e) {
	if (_state & StateDown) {
		int oldState = _state;
		_state &= ~StateDown;
		emit stateChanged(oldState, ButtonByPress);
		if (oldState & StateOver) {
			emit clicked();
		} else {
			leaveEvent(e);
		}
	}
}

void Button::setDisabled(bool disabled) {
	int oldState = _state;
	if (disabled && !(_state & StateDisabled)) {
		_state |= StateDisabled;
		emit stateChanged(oldState, ButtonByUser);
	} else if (!disabled && (_state & StateDisabled)) {
		_state &= ~StateDisabled;
		emit stateChanged(oldState, ButtonByUser);
	}
}

void Button::clearState() {
	int oldState = _state;
	_state = StateNone;
	emit stateChanged(oldState, ButtonByUser);
}

int Button::getState() const {
	return _state;
}
