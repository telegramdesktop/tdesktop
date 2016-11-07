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
#include "window/slide_animation.h"

namespace Window {

SlideAnimation::SlideAnimation()
	: _animation(animation(this, &SlideAnimation::step)) {
}

void SlideAnimation::paintContents(Painter &p, const QRect &update) const {
	int retina = cIntRetinaFactor();

	_animation.step(getms());
	if (a_coordOver.current() > 0) {
		p.drawPixmap(QRect(0, 0, a_coordOver.current(), _cacheUnder.height() / retina), _cacheUnder, QRect(-a_coordUnder.current() * retina, 0, a_coordOver.current() * retina, _cacheUnder.height()));
		p.setOpacity(a_progress.current());
		p.fillRect(0, 0, a_coordOver.current(), _cacheUnder.height() / retina, st::slideFadeOutBg);
		p.setOpacity(1);
	}
	p.drawPixmap(QRect(a_coordOver.current(), 0, _cacheOver.width() / retina, _cacheOver.height() / retina), _cacheOver, QRect(0, 0, _cacheOver.width(), _cacheOver.height()));
	p.setOpacity(a_progress.current());
	st::slideShadow.fill(p, QRect(a_coordOver.current() - st::slideShadow.width(), 0, st::slideShadow.width(), _cacheOver.height() / retina));

	if (_topBarShadowEnabled) {
		p.setOpacity(1);
		p.fillRect(0, st::topBarHeight, _cacheOver.width() / retina, st::lineWidth, st::shadowColor);
	}
}

void SlideAnimation::setDirection(SlideDirection direction) {
	_direction = direction;
}

void SlideAnimation::setPixmaps(const QPixmap &oldContentCache, const QPixmap &newContentCache) {
	_cacheUnder = oldContentCache;
	_cacheOver = newContentCache;
}

void SlideAnimation::setTopBarShadow(bool enabled) {
	_topBarShadowEnabled = enabled;
}

void SlideAnimation::setRepaintCallback(RepaintCallback &&callback) {
	_repaintCallback = std_::move(callback);
}

void SlideAnimation::setFinishedCallback(FinishedCallback &&callback) {
	_finishedCallback = std_::move(callback);
}

void SlideAnimation::start() {
	int delta = st::slideShift;
	if (_direction == SlideDirection::FromLeft) {
		a_progress = anim::fvalue(1, 0);
		std::swap(_cacheUnder, _cacheOver);
		a_coordUnder = anim::ivalue(-delta, 0);
		a_coordOver = anim::ivalue(0, _cacheOver.width() / cIntRetinaFactor());
	} else {
		a_progress = anim::fvalue(0, 1);
		a_coordUnder = anim::ivalue(0, -delta);
		a_coordOver = anim::ivalue(_cacheOver.width() / cIntRetinaFactor(), 0);
	}
	_animation.start();
}

void SlideAnimation::step(float64 ms, bool timer) {
	float64 dt = ms / st::slideDuration;
	if (dt >= 1) {
		dt = 1;
		if (timer) {
			_animation.stop();
			a_coordUnder.finish();
			a_coordOver.finish();

			if (_finishedCallback) {
				_finishedCallback();
			}
			return;
		}
	}

	a_coordUnder.update(dt, transition());
	a_coordOver.update(dt, transition());
	a_progress.update(dt, transition());
	if (timer && _repaintCallback) {
		_repaintCallback();
	}
}

} // namespace Window
