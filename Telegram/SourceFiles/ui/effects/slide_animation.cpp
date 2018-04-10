/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/slide_animation.h"

namespace Ui {

void SlideAnimation::setSnapshots(QPixmap leftSnapshot, QPixmap rightSnapshot) {
	_leftSnapshot = std::move(leftSnapshot);
	_rightSnapshot = std::move(rightSnapshot);
	Assert(!_leftSnapshot.isNull());
	Assert(!_rightSnapshot.isNull());
	_leftSnapshot.setDevicePixelRatio(cRetinaFactor());
	_rightSnapshot.setDevicePixelRatio(cRetinaFactor());
}

void SlideAnimation::paintFrame(Painter &p, int x, int y, int outerWidth, TimeMs ms) {
	auto dt = _animation.current(ms, 1.);
	if (!animating()) return;

	auto easeOut = anim::easeOutCirc(1., dt);
	auto easeIn = anim::easeInCirc(1., dt);
	auto arrivingAlpha = easeIn;
	auto departingAlpha = 1. - easeOut;
	auto leftCoord = (_slideLeft ? anim::interpolate(-_leftSnapshotWidth, 0, easeOut) : anim::interpolate(0, -_leftSnapshotWidth, easeIn));
	auto leftAlpha = (_slideLeft ? arrivingAlpha : departingAlpha);
	auto rightCoord = (_slideLeft ? anim::interpolate(0, _rightSnapshotWidth, easeIn) : anim::interpolate(_rightSnapshotWidth, 0, easeOut));
	auto rightAlpha = (_slideLeft ? departingAlpha : arrivingAlpha);

	if (_overflowHidden) {
		auto leftWidth = (_leftSnapshotWidth + leftCoord);
		if (leftWidth > 0) {
			p.setOpacity(leftAlpha);
			p.drawPixmap(x, y, leftWidth, _leftSnapshotHeight, _leftSnapshot, (_leftSnapshot.width() - leftWidth * cIntRetinaFactor()), 0, leftWidth * cIntRetinaFactor(), _leftSnapshot.height());
		}
		auto rightWidth = _rightSnapshotWidth - rightCoord;
		if (rightWidth > 0) {
			p.setOpacity(rightAlpha);
			p.drawPixmap(x + rightCoord, y, _rightSnapshot, 0, 0, rightWidth * cIntRetinaFactor(), _rightSnapshot.height());
		}
	} else {
		p.setOpacity(leftAlpha);
		p.drawPixmap(x + leftCoord, y, _leftSnapshot);
		p.setOpacity(rightAlpha);
		p.drawPixmap(x + rightCoord, y, _rightSnapshot);
	}
}

} // namespace Ui
