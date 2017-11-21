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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "ui/abstract_button.h"

#include <rpl/filter.h>
#include <rpl/mappers.h>
#include "messenger.h"

namespace Ui {

AbstractButton::AbstractButton(QWidget *parent) : RpWidget(parent) {
	setMouseTracking(true);

	using namespace rpl::mappers;
	shownValue()
		| rpl::filter(_1 == false)
		| rpl::start_with_next([this] { clearState(); }, lifetime());
}

void AbstractButton::leaveEventHook(QEvent *e) {
	if (_state & StateFlag::Down) return;

	setOver(false, StateChangeSource::ByHover);
	return TWidget::leaveEventHook(e);
}

void AbstractButton::enterEventHook(QEvent *e) {
	checkIfOver(mapFromGlobal(QCursor::pos()));
	return TWidget::enterEventHook(e);
}

void AbstractButton::setAcceptBoth(bool acceptBoth) {
	_acceptBoth = acceptBoth;
}

void AbstractButton::checkIfOver(QPoint localPos) {
	auto over = rect().marginsRemoved(getMargins()).contains(localPos);
	setOver(over, StateChangeSource::ByHover);
}

void AbstractButton::mousePressEvent(QMouseEvent *e) {
	checkIfOver(e->pos());
	if (_acceptBoth || (e->buttons() & Qt::LeftButton)) {
		if ((_state & StateFlag::Over) && !(_state & StateFlag::Down)) {
			auto was = _state;
			_state |= StateFlag::Down;
			onStateChanged(was, StateChangeSource::ByPress);

			e->accept();
		}
	}
}

void AbstractButton::mouseMoveEvent(QMouseEvent *e) {
	if (rect().marginsRemoved(getMargins()).contains(e->pos())) {
		setOver(true, StateChangeSource::ByHover);
	} else {
		setOver(false, StateChangeSource::ByHover);
	}
}

void AbstractButton::mouseReleaseEvent(QMouseEvent *e) {
	if (_state & StateFlag::Down) {
		auto was = _state;
		_state &= ~State(StateFlag::Down);
		onStateChanged(was, StateChangeSource::ByPress);
		if (was & StateFlag::Over) {
			_modifiers = e->modifiers();
			auto test = weak(this);
			if (_clickedCallback) {
				_clickedCallback();
			} else {
				emit clicked();
			}
			if (test) {
				_clicks.fire({});
			}
		} else {
			setOver(false, StateChangeSource::ByHover);
		}
	}
}

void AbstractButton::setPointerCursor(bool enablePointerCursor) {
	if (_enablePointerCursor != enablePointerCursor) {
		_enablePointerCursor = enablePointerCursor;
		updateCursor();
	}
}

void AbstractButton::setOver(bool over, StateChangeSource source) {
	if (over && !(_state & StateFlag::Over)) {
		auto was = _state;
		_state |= StateFlag::Over;
		Messenger::Instance().registerLeaveSubscription(this);
		onStateChanged(was, source);
	} else if (!over && (_state & StateFlag::Over)) {
		auto was = _state;
		_state &= ~State(StateFlag::Over);
		Messenger::Instance().unregisterLeaveSubscription(this);
		onStateChanged(was, source);
	}
	updateCursor();
}

void AbstractButton::updateCursor() {
	auto pointerCursor = _enablePointerCursor && (_state & StateFlag::Over);
	setCursor(pointerCursor ? style::cur_pointer : style::cur_default);
}

void AbstractButton::setDisabled(bool disabled) {
	auto was = _state;
	if (disabled && !(_state & StateFlag::Disabled)) {
		_state |= StateFlag::Disabled;
		onStateChanged(was, StateChangeSource::ByUser);
	} else if (!disabled && (_state & StateFlag::Disabled)) {
		_state &= ~State(StateFlag::Disabled);
		onStateChanged(was, StateChangeSource::ByUser);
	}
}

void AbstractButton::clearState() {
	auto was = _state;
	_state = StateFlag::None;
	onStateChanged(was, StateChangeSource::ByUser);
}

} // namespace Ui
