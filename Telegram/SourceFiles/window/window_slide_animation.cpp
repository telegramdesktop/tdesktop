/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_slide_animation.h"

#include "styles/style_window.h"
#include "styles/style_boxes.h"

namespace Window {

void SlideAnimation::paintContents(Painter &p, const QRect &update) const {
	int retina = cIntRetinaFactor();

	auto progress = _animation.value((_direction == SlideDirection::FromLeft) ? 0. : 1.);
	if (_withFade) {
		p.fillRect(update, st::boxBg);

		auto slideLeft = (_direction == SlideDirection::FromLeft);
		auto dt = slideLeft
			? (1. - progress)
			: progress;
		auto easeOut = anim::easeOutCirc(1., dt);
		auto easeIn = anim::easeInCirc(1., dt);
		auto arrivingAlpha = easeIn;
		auto departingAlpha = 1. - easeOut;
		auto leftWidthFull = _cacheUnder.width() / cIntRetinaFactor();
		auto rightWidthFull = _cacheOver.width() / cIntRetinaFactor();
		auto leftCoord = (slideLeft ? anim::interpolate(-leftWidthFull, 0, easeOut) : anim::interpolate(0, -leftWidthFull, easeIn));
		auto leftAlpha = (slideLeft ? arrivingAlpha : departingAlpha);
		auto rightCoord = (slideLeft ? anim::interpolate(0, rightWidthFull, easeIn) : anim::interpolate(rightWidthFull, 0, easeOut));
		auto rightAlpha = (slideLeft ? departingAlpha : arrivingAlpha);

		auto leftWidth = (leftWidthFull + leftCoord);
		if (leftWidth > 0) {
			p.setOpacity(leftAlpha);
			p.drawPixmap(0, 0, leftWidth, _cacheUnder.height() / retina, _cacheUnder, (_cacheUnder.width() - leftWidth * cIntRetinaFactor()), 0, leftWidth * cIntRetinaFactor(), _cacheUnder.height());
		}
		auto rightWidth = rightWidthFull - rightCoord;
		if (rightWidth > 0) {
			p.setOpacity(rightAlpha);
			p.drawPixmap(rightCoord, 0, _cacheOver, 0, 0, rightWidth * cIntRetinaFactor(), _cacheOver.height());
		}
	} else {
		auto coordUnder = anim::interpolate(0, -st::slideShift, progress);
		auto coordOver = anim::interpolate(_cacheOver.width() / cIntRetinaFactor(), 0, progress);
		if (coordOver) {
			p.drawPixmap(QRect(0, 0, coordOver, _cacheUnder.height() / retina), _cacheUnder, QRect(-coordUnder * retina, 0, coordOver * retina, _cacheUnder.height()));
			p.setOpacity(progress);
			p.fillRect(0, 0, coordOver, _cacheUnder.height() / retina, st::slideFadeOutBg);
			p.setOpacity(1);
		}
		p.drawPixmap(QRect(coordOver, 0, _cacheOver.width() / retina, _cacheOver.height() / retina), _cacheOver, QRect(0, 0, _cacheOver.width(), _cacheOver.height()));
		p.setOpacity(progress);
		st::slideShadow.fill(p, QRect(coordOver - st::slideShadow.width(), 0, st::slideShadow.width(), _cacheOver.height() / retina));
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

void SlideAnimation::setWithFade(bool withFade) {
	_withFade = withFade;
}

void SlideAnimation::setRepaintCallback(RepaintCallback &&callback) {
	_repaintCallback = std::move(callback);
}

void SlideAnimation::setFinishedCallback(FinishedCallback &&callback) {
	_finishedCallback = std::move(callback);
}

void SlideAnimation::start() {
	auto fromLeft = (_direction == SlideDirection::FromLeft);
	if (fromLeft) std::swap(_cacheUnder, _cacheOver);
	_animation.start(
		[this] { animationCallback(); },
		fromLeft ? 1. : 0.,
		fromLeft ? 0. : 1.,
		st::slideDuration,
		transition());
	_repaintCallback();
}

void SlideAnimation::animationCallback() {
	_repaintCallback();
	if (!_animation.animating()) {
		if (_finishedCallback) {
			_finishedCallback();
		}
	}
}

} // namespace Window
