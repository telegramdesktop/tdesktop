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

class TWidget;

#include "styles/style_widgets.h"

namespace Ui {

class FadeAnimation {
public:
	FadeAnimation(TWidget *widget);

	bool paint(Painter &p);
	void refreshCache();

	using FinishedCallback = base::lambda<void()>;
	void setFinishedCallback(FinishedCallback &&callback);

	using UpdatedCallback = base::lambda<void(float64)>;
	void setUpdatedCallback(UpdatedCallback &&callback);

	void show();
	void hide();

	void fadeIn(int duration);
	void fadeOut(int duration);

	void finish() {
		_animation.finish();
	}

	bool animating() const {
		return _animation.animating();
	}

private:
	void startAnimation(int duration);
	void stopAnimation();

	void updateCallback();

	TWidget *_widget;
	Animation _animation;
	QPixmap _cache;
	bool _visible = false;

	FinishedCallback _finishedCallback;
	UpdatedCallback _updatedCallback;

};

template <typename Widget>
class WidgetFadeWrap;

template <>
class WidgetFadeWrap<TWidget> : public TWidget {
public:
	WidgetFadeWrap(QWidget *parent
		, object_ptr<TWidget> entity
		, int duration = st::widgetFadeDuration
		, base::lambda<void()> &&updateCallback = base::lambda<void()>());

	void showAnimated() {
		_animation.fadeIn(_duration);
	}
	void hideAnimated() {
		_animation.fadeOut(_duration);
	}
	void showFast() {
		_animation.show();
		if (_updateCallback) {
			_updateCallback();
		}
	}
	void hideFast() {
		_animation.hide();
		if (_updateCallback) {
			_updateCallback();
		}
	}
	void finishAnimation() {
		_animation.finish();
	}

	TWidget *entity() {
		return _entity;
	}

	const TWidget *entity() const {
		return _entity;
	}

	QMargins getMargins() const override {
		return _entity->getMargins();
	}
	int naturalWidth() const override {
		return _entity->naturalWidth();
	}

	bool animating() const {
		return _animation.animating();
	}

protected:
	bool eventFilter(QObject *object, QEvent *event) override;
	void paintEvent(QPaintEvent *e) override;

private:
	object_ptr<TWidget> _entity;
	int _duration;
	base::lambda<void()> _updateCallback;

	FadeAnimation _animation;

};

template <typename Widget>
class WidgetFadeWrap : public WidgetFadeWrap<TWidget> {
public:
	WidgetFadeWrap(QWidget *parent
		, object_ptr<Widget> entity
		, int duration = st::widgetFadeDuration
		, base::lambda<void()> &&updateCallback = base::lambda<void()>()) : WidgetFadeWrap<TWidget>(parent
			, std_::move(entity)
			, duration
			, std_::move(updateCallback)) {
	}
	Widget *entity() {
		return static_cast<Widget*>(WidgetFadeWrap<TWidget>::entity());
	}
	const Widget *entity() const {
		return static_cast<const Widget*>(WidgetFadeWrap<TWidget>::entity());
	}

};

} // namespace Ui
