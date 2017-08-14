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
#include "ui/effects/widget_slide_wrap.h"

namespace Ui {

WidgetSlideWrap<TWidget>::WidgetSlideWrap(QWidget *parent
, object_ptr<TWidget> entity
, style::margins entityPadding
, base::lambda<void()> updateCallback
, int duration) : TWidget(parent)
, _entity(std::move(entity))
, _padding(entityPadding)
, _duration(duration)
, _updateCallback(std::move(updateCallback)) {
	_entity->setParent(this);
	auto margins = getMargins();
	_entity->moveToLeft(margins.left() + _padding.left(), margins.top() + _padding.top());
	_realSize = _entity->rectNoMargins().marginsAdded(_padding).size();
	_entity->installEventFilter(this);
	resizeToWidth(_realSize.width());
}

void WidgetSlideWrap<TWidget>::hideAnimated() {
	if (isHidden()) {
		_forceHeight = 0;
		resizeToWidth(_realSize.width());
		if (_updateCallback) _updateCallback();
		return;
	}
	if (_a_height.animating()) {
		if (_hiding) return;
	}
	_hiding = true;
	_a_height.start([this] { animationCallback(); }, _realSize.height(), 0., _duration);
}

void WidgetSlideWrap<TWidget>::showAnimated() {
	if (isHidden()) {
		show();
	}
	if (_forceHeight < 0) {
		return;
	}

	if (_a_height.animating()) {
		if (!_hiding) return;
	}
	_hiding = false;
	_forceHeight = qRound(_a_height.current(0.));
	_a_height.start([this] { animationCallback(); }, 0., _realSize.height(), _duration);
}

void WidgetSlideWrap<TWidget>::toggleFast(bool visible) {
	_hiding = !visible;
	if (!_hiding) show();
	_a_height.finish();
	_forceHeight = _hiding ? 0 : -1;
	resizeToWidth(_realSize.width());
	if (_hiding) hide();
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
	_realSize = _entity->rectNoMargins().marginsAdded(_padding).size();
	return _realSize.height();
}

void WidgetSlideWrap<TWidget>::animationCallback() {
	_forceHeight = qRound(_a_height.current(_hiding ? 0 : -1));
	resizeToWidth(_realSize.width());
	if (!_a_height.animating()) {
		_forceHeight = _hiding ? 0 : -1;
		if (_hiding) hide();
	}
	if (_updateCallback) {
		_updateCallback();
	}
}

} // namespace Ui
