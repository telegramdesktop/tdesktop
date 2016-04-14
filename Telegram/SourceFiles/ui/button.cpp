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
#include "button.h"

Button::Button(QWidget *parent) : TWidget(parent), _state(StateNone), _acceptBoth(false) {
}

void Button::leaveEvent(QEvent *e) {
	if (_state & StateDown) return;

	setOver(false, ButtonByHover);
	setMouseTracking(false);
	return TWidget::leaveEvent(e);
}

void Button::enterEvent(QEvent *e) {
	setOver(true, ButtonByHover);
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
		setOver(true, ButtonByHover);
	} else {
		setOver(false, ButtonByHover);
	}
}

void Button::mouseReleaseEvent(QMouseEvent *e) {
	if (_state & StateDown) {
		int oldState = _state;
		_state &= ~StateDown;
		emit stateChanged(oldState, ButtonByPress);
		if (oldState & StateOver) {
			_modifiers = e->modifiers();
			emit clicked();
		} else {
			leaveEvent(e);
		}
	}
}

void Button::setOver(bool over, ButtonStateChangeSource source) {
	if (over && !(_state & StateOver)) {
		int oldState = _state;
		_state |= StateOver;
		emit stateChanged(oldState, source);
	} else if (!over && (_state & StateOver)) {
		int oldState = _state;
		_state &= ~StateOver;
		emit stateChanged(oldState, source);
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
