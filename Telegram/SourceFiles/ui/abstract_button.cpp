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
#include "ui/abstract_button.h"

namespace Ui {

void AbstractButton::leaveEvent(QEvent *e) {
	if (_state & StateDown) return;

	setOver(false, StateChangeSource::ByHover);
	setMouseTracking(false);
	return TWidget::leaveEvent(e);
}

void AbstractButton::enterEvent(QEvent *e) {
	setOver(true, StateChangeSource::ByHover);
	setMouseTracking(true);
	return TWidget::enterEvent(e);
}

void AbstractButton::setAcceptBoth(bool acceptBoth) {
	_acceptBoth = acceptBoth;
}

void AbstractButton::mousePressEvent(QMouseEvent *e) {
	if (_acceptBoth || e->buttons() & Qt::LeftButton) {
		if (!(_state & StateOver)) {
			enterEvent(0);
		}
		if (!(_state & StateDown)) {
			int oldState = _state;
			_state |= StateDown;
			onStateChanged(oldState, StateChangeSource::ByPress);

			e->accept();
		}
	}
}

void AbstractButton::mouseMoveEvent(QMouseEvent *e) {
	if (rect().contains(e->pos())) {
		setOver(true, StateChangeSource::ByHover);
	} else {
		setOver(false, StateChangeSource::ByHover);
	}
}

void AbstractButton::mouseReleaseEvent(QMouseEvent *e) {
	if (_state & StateDown) {
		int oldState = _state;
		_state &= ~StateDown;
		onStateChanged(oldState, StateChangeSource::ByPress);
		if (oldState & StateOver) {
			_modifiers = e->modifiers();
			if (_clickedCallback) {
				_clickedCallback();
			} else {
				emit clicked();
			}
		} else {
			leaveEvent(e);
		}
	}
}

void AbstractButton::setOver(bool over, StateChangeSource source) {
	if (over && !(_state & StateOver)) {
		int oldState = _state;
		_state |= StateOver;
		onStateChanged(oldState, source);
	} else if (!over && (_state & StateOver)) {
		int oldState = _state;
		_state &= ~StateOver;
		onStateChanged(oldState, source);
	}
}

void AbstractButton::setDisabled(bool disabled) {
	int oldState = _state;
	if (disabled && !(_state & StateDisabled)) {
		_state |= StateDisabled;
		onStateChanged(oldState, StateChangeSource::ByUser);
	} else if (!disabled && (_state & StateDisabled)) {
		_state &= ~StateDisabled;
		onStateChanged(oldState, StateChangeSource::ByUser);
	}
}

void AbstractButton::clearState() {
	int oldState = _state;
	_state = StateNone;
	onStateChanged(oldState, StateChangeSource::ByUser);
}

int AbstractButton::getState() const {
	return _state;
}

} // namespace Ui
