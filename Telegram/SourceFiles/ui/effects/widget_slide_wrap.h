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
#pragma once

#include "styles/style_widgets.h"

namespace Ui {

template <typename Widget>
class WidgetSlideWrap;

template <>
class WidgetSlideWrap<TWidget> : public TWidget {
public:
	WidgetSlideWrap(QWidget *parent
		, object_ptr<TWidget> entity
		, style::margins entityPadding
		, base::lambda<void()> updateCallback
		, int duration = st::widgetSlideDuration);

	void showAnimated();
	void hideAnimated();
	void toggleAnimated(bool visible) {
		if (visible) {
			showAnimated();
		} else {
			hideAnimated();
		}
	}
	void showFast() {
		toggleFast(true);
	}
	void hideFast() {
		toggleFast(false);
	}
	void toggleFast(bool visible);

	bool isHiddenOrHiding() const {
		return isHidden() || (_a_height.animating() && _hiding);
	}

	void finishAnimation() {
		_a_height.finish();
		myEnsureResized(_entity);
		animationCallback();
	}
	bool animating() const {
		return _a_height.animating();
	}

	TWidget *entity() {
		return _entity;
	}

	const TWidget *entity() const {
		return _entity;
	}

	QMargins getMargins() const override;
	int naturalWidth() const override;

protected:
	bool eventFilter(QObject *object, QEvent *event) override;
	int resizeGetHeight(int newWidth) override;

private:
	void animationCallback();

	object_ptr<TWidget> _entity;
	bool _inResizeToWidth = false;
	style::margins _padding;
	int _duration;
	base::lambda<void()> _updateCallback;

	style::size _realSize;
	int _forceHeight = -1;
	Animation _a_height;
	bool _hiding = false;

};

template <typename Widget>
class WidgetSlideWrap : public WidgetSlideWrap<TWidget> {
public:
	WidgetSlideWrap(QWidget *parent
		, object_ptr<Widget> entity
		, style::margins entityPadding
		, base::lambda<void()> updateCallback
		, int duration = st::widgetSlideDuration) : WidgetSlideWrap<TWidget>(parent
			, std::move(entity)
			, entityPadding
			, std::move(updateCallback)
			, duration) {
	}
	Widget *entity() {
		return static_cast<Widget*>(WidgetSlideWrap<TWidget>::entity());
	}
	const Widget *entity() const {
		return static_cast<const Widget*>(WidgetSlideWrap<TWidget>::entity());
	}

};

} // namespace Ui
