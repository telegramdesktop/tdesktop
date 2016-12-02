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
#include "ui/effects/widget_slide_wrap.h"

namespace Ui {

WidgetSlideWrap<TWidget>::WidgetSlideWrap(QWidget *parent
, TWidget *entity
, style::margins entityPadding
, base::lambda<void()> &&updateCallback
, int duration) : TWidget(parent)
, _entity(entity)
, _padding(entityPadding)
, _duration(duration)
, _updateCallback(std_::move(updateCallback))
, _a_height(animation(this, &WidgetSlideWrap<TWidget>::step_height)) {
	_entity->setParent(this);
	auto margins = getMargins();
	_entity->moveToLeft(margins.left() + _padding.left(), margins.top() + _padding.top());
	_realSize = _entity->rectNoMargins().marginsAdded(_padding).size();
	_entity->installEventFilter(this);
	resizeToWidth(_realSize.width());
}

void WidgetSlideWrap<TWidget>::slideUp() {
	if (isHidden()) {
		_forceHeight = 0;
		resizeToWidth(_realSize.width());
		if (_updateCallback) _updateCallback();
		return;
	}
	if (_a_height.animating()) {
		if (_hiding) return;
	} else {
		a_height = anim::ivalue(_realSize.height());
	}
	a_height.start(0);
	_hiding = true;
	_a_height.start();
}

void WidgetSlideWrap<TWidget>::slideDown() {
	if (isHidden()) {
		show();
	}
	if (_forceHeight < 0) {
		return;
	}

	if (_a_height.animating()) {
		if (!_hiding) return;
	}
	a_height.start(_realSize.height());
	_forceHeight = a_height.current();
	_hiding = false;
	_a_height.start();
}

void WidgetSlideWrap<TWidget>::showFast() {
	show();
	_a_height.stop();
	_forceHeight = -1;
	resizeToWidth(_realSize.width());
	if (_updateCallback) {
		_updateCallback();
	}
}

void WidgetSlideWrap<TWidget>::hideFast() {
	_a_height.stop();
	a_height = anim::ivalue(0);
	_forceHeight = 0;
	resizeToWidth(_realSize.width());
	hide();
	if (_updateCallback) {
		_updateCallback();
	}
}

QMargins WidgetSlideWrap<TWidget>::getMargins() const {
	auto entityMargins = _entity->getMargins();
	if (_forceHeight < 0) {
		return entityMargins;
	}
	return QMargins(entityMargins.left(), 0, entityMargins.right(), 0);
}

int WidgetSlideWrap<TWidget>::naturalWidth() const {
	auto inner = _entity->naturalWidth();
	return (inner < 0) ? inner : (_padding.left() + inner + _padding.right());
}

bool WidgetSlideWrap<TWidget>::eventFilter(QObject *object, QEvent *event) {
	if (object == _entity && event->type() == QEvent::Resize) {
		_realSize = _entity->rectNoMargins().marginsAdded(_padding).size();
		if (!_inResizeToWidth) {
			resizeToWidth(_realSize.width());
			if (_updateCallback) {
				_updateCallback();
			}
		}
	}
	return TWidget::eventFilter(object, event);
}

int WidgetSlideWrap<TWidget>::resizeGetHeight(int newWidth) {
	_inResizeToWidth = true;
	auto resized = (_forceHeight >= 0);
	_entity->resizeToWidth(newWidth - _padding.left() - _padding.right());
	auto margins = getMargins();
	_entity->moveToLeft(margins.left() + _padding.left(), margins.top() + _padding.top());
	_inResizeToWidth = false;
	if (resized) {
		return _forceHeight;
	}
	return _realSize.height();
}

void WidgetSlideWrap<TWidget>::step_height(float64 ms, bool timer) {
	auto dt = ms / _duration;
	if (dt >= 1) {
		a_height.finish();
		_a_height.stop();
		_forceHeight = _hiding ? 0 : -1;
		if (_hiding) hide();
	} else {
		a_height.update(dt, anim::linear);
		_forceHeight = a_height.current();
	}
	resizeToWidth(_realSize.width());
	if (_updateCallback) {
		_updateCallback();
	}
}

} // namespace Ui
