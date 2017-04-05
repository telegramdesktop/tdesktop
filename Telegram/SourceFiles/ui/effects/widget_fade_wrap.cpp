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
#include "ui/effects/widget_fade_wrap.h"

namespace Ui {
namespace {

constexpr int kWideScale = 5;

} // namespace

FadeAnimation::FadeAnimation(TWidget *widget, bool scaled)
: _widget(widget)
, _scaled(scaled) {
}

bool FadeAnimation::paint(Painter &p) {
	if (_cache.isNull()) return false;

	auto opacity = _animation.current(getms(), _visible ? 1. : 0.);
	p.setOpacity(opacity);
	if (_scaled) {
		PainterHighQualityEnabler hq(p);
		auto targetRect = QRect((1 - kWideScale) / 2 * _size.width(), (1 - kWideScale) / 2 * _size.height(), kWideScale * _size.width(), kWideScale * _size.height());
		auto scale = opacity;
		auto shownWidth = anim::interpolate((1 - kWideScale) / 2 * _size.width(), 0, scale);
		auto shownHeight = anim::interpolate((1 - kWideScale) / 2 * _size.height(), 0, scale);
		p.drawPixmap(targetRect.marginsAdded(QMargins(shownWidth, shownHeight, shownWidth, shownHeight)), _cache);
	} else {
		p.drawPixmap(0, 0, _cache);
	}
	return true;
}

void FadeAnimation::refreshCache() {
	if (!_cache.isNull()) {
		_cache = QPixmap();
		_cache = grabContent();
	}
}

QPixmap FadeAnimation::grabContent() {
	myEnsureResized(_widget);
	_size = _widget->size();
	auto widgetContent = myGrab(_widget);
	if (!_scaled) {
		return widgetContent;
	}

	auto result = QImage(kWideScale * _size * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		p.drawPixmap((kWideScale - 1) / 2 * _size.width(), (kWideScale - 1) / 2 * _size.height(), widgetContent);
	}
	return App::pixmapFromImageInPlace(std::move(result));
}

void FadeAnimation::setFinishedCallback(FinishedCallback &&callback) {
	_finishedCallback = std::move(callback);
}

void FadeAnimation::setUpdatedCallback(UpdatedCallback &&callback) {
	_updatedCallback = std::move(callback);
}

void FadeAnimation::show() {
	if (!_visible) {
		_visible = true;
		_widget->showChildren();
	}
	stopAnimation();
}

void FadeAnimation::hide() {
	_visible = false;
	stopAnimation();
}

void FadeAnimation::stopAnimation() {
	_animation.finish();
	if (!_cache.isNull()) {
		_cache = QPixmap();
		updateCallback();
		if (_visible) {
			_widget->showChildren();
		}
		if (_finishedCallback) {
			_finishedCallback();
		}
	}
	if (_visible == _widget->isHidden()) {
		_widget->setVisible(_visible);
	}
}

void FadeAnimation::fadeIn(int duration) {
	if (_visible) return;

	_visible = true;
	startAnimation(duration);
}

void FadeAnimation::fadeOut(int duration) {
	if (!_visible) return;

	_visible = false;
	startAnimation(duration);
}

void FadeAnimation::startAnimation(int duration) {
	if (_cache.isNull()) {
		_widget->showChildren();
		_cache = grabContent();
		_widget->hideChildren();
	}
	auto from = _visible ? 0. : 1.;
	auto to = _visible ? 1. : 0.;
	_animation.start([this]() { updateCallback(); }, from, to, duration);
	updateCallback();
	if (_widget->isHidden()) {
		_widget->show();
	}
}

void FadeAnimation::updateCallback() {
	if (_animation.animating()) {
		_widget->update();
		if (_updatedCallback) {
			_updatedCallback(_animation.current(_visible ? 1. : 0.));
		}
	} else {
		stopAnimation();
	}
}

WidgetFadeWrap<TWidget>::WidgetFadeWrap(QWidget *parent
, object_ptr<TWidget> entity
, int duration
, base::lambda<void()> updateCallback
, bool scaled) : TWidget(parent)
, _entity(std::move(entity))
, _duration(duration)
, _updateCallback(std::move(updateCallback))
, _animation(this, scaled) {
	_animation.show();
	installCallbacks();
	_entity->setParent(this);
	_entity->moveToLeft(0, 0);
	_entity->installEventFilter(this);
	resize(_entity->size());
}

void WidgetFadeWrap<TWidget>::installCallbacks() {
	if (_updateCallback) {
		_animation.setFinishedCallback([this] { _updateCallback(); });
		_animation.setUpdatedCallback([this](float64 opacity) { _updateCallback(); });
	} else {
		_animation.setFinishedCallback(base::lambda<void()>());
		_animation.setUpdatedCallback(base::lambda<void(float64)>());
	}
}

bool WidgetFadeWrap<TWidget>::eventFilter(QObject *object, QEvent *event) {
	if (object == _entity && event->type() == QEvent::Resize) {
		resize(_entity->rect().size());
	}
	return TWidget::eventFilter(object, event);
}

void WidgetFadeWrap<TWidget>::paintEvent(QPaintEvent *e) {
	Painter p(this);
	_animation.paint(p);
}

} // namespace Ui
