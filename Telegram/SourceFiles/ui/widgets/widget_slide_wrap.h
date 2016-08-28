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
#pragma once

#include "styles/style_widgets.h"

namespace Ui {

template <typename Widget>
class WidgetSlideWrap : public TWidget {
public:
	WidgetSlideWrap(QWidget *parent, Widget *entity
		, style::margins entityPadding
		, base::lambda_unique<void()> &&updateCallback
		, int duration = st::widgetSlideDuration) : TWidget(parent)
		, _entity(entity)
		, _padding(entityPadding)
		, _duration(duration)
		, _updateCallback(std_::move(updateCallback))
		, _a_height(animation(this, &WidgetSlideWrap<Widget>::step_height)) {
		entity->setParent(this);
		entity->moveToLeft(_padding.left(), _padding.top());
		_realSize = entity->rect().marginsAdded(_padding).size();
		entity->installEventFilter(this);
		resize(_realSize);
	}

	bool eventFilter(QObject *object, QEvent *event) override {
		if (object == _entity && event->type() == QEvent::Resize) {
			_realSize = _entity->rect().marginsAdded(_padding).size();
			if (!_inResizeToWidth) {
				resize(_realSize.width(), (_forceHeight >= 0) ? _forceHeight : _realSize.height());
				if (_updateCallback) {
					_updateCallback();
				}
			}
		}
		return TWidget::eventFilter(object, event);
	}

	void slideUp() {
		if (isHidden()) {
			_forceHeight = 0;
			resize(_realSize.width(), _forceHeight);
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

	void slideDown() {
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

	void showFast() {
		_a_height.stop();
		resize(_realSize);
		if (_updateCallback) {
			_updateCallback();
		}
	}

	void hideFast() {
		_a_height.stop();
		a_height = anim::ivalue(0);
		_forceHeight = 0;
		resize(_realSize.width(), 0);
		hide();
		if (_updateCallback) {
			_updateCallback();
		}
	}

	Widget *entity() {
		return _entity;
	}

	const Widget *entity() const {
		return _entity;
	}

	int naturalWidth() const override {
		auto inner = _entity->naturalWidth();
		return (inner < 0) ? inner : (_padding.left() + inner + _padding.right());
	}

protected:
	int resizeGetHeight(int newWidth) override {
		_inResizeToWidth = true;
		_entity->resizeToWidth(newWidth - _padding.left() - _padding.right());
		_inResizeToWidth = false;
		return (_forceHeight >= 0) ? _forceHeight : _realSize.height();
	}

private:
	void step_height(float64 ms, bool timer) {
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
		resize(_realSize.width(), (_forceHeight >= 0) ? _forceHeight : _realSize.height());
		if (_updateCallback) {
			_updateCallback();
		}
	}

	Widget *_entity;
	bool _inResizeToWidth = false;
	style::margins _padding;
	int _duration;
	base::lambda_unique<void()> _updateCallback;

	style::size _realSize;
	int _forceHeight = -1;
	anim::ivalue a_height;
	Animation _a_height;
	bool _hiding = false;

};

} // namespace Ui
