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
#include "ui/widgets/continuous_slider.h"

namespace Ui {
namespace {

constexpr auto kByWheelFinishedTimeout = 1000;

} // namespace

ContinuousSlider::ContinuousSlider(QWidget *parent) : TWidget(parent)
, _a_value(animation(this, &ContinuousSlider::step_value)) {
	setCursor(style::cur_pointer);
}

float64 ContinuousSlider::value() const {
	return a_value.current();
}

void ContinuousSlider::setDisabled(bool disabled) {
	if (_disabled != disabled) {
		_disabled = disabled;
		setCursor(_disabled ? style::cur_default : style::cur_pointer);
		update();
	}
}

void ContinuousSlider::setMoveByWheel(bool moveByWheel) {
	if (_moveByWheel != moveByWheel) {
		_moveByWheel = moveByWheel;
		if (_moveByWheel) {
			_byWheelFinished = std_::make_unique<SingleTimer>();
			_byWheelFinished->setTimeoutHandler([this] {
				if (_changeFinishedCallback) {
					_changeFinishedCallback(getCurrentValue(getms()));
				}
			});
		} else {
			_byWheelFinished.reset();
		}
	}
}

void ContinuousSlider::setValue(float64 value, bool animated) {
	if (animated) {
		a_value.start(value);
		_a_value.start();
	} else {
		a_value = anim::fvalue(value, value);
		_a_value.stop();
	}
	update();
}

void ContinuousSlider::setFadeOpacity(float64 opacity) {
	_fadeOpacity = opacity;
	update();
}

void ContinuousSlider::step_value(float64 ms, bool timer) {
	float64 dt = ms / (2 * AudioVoiceMsgUpdateView);
	if (dt >= 1) {
		_a_value.stop();
		a_value.finish();
	} else {
		a_value.update(qMin(dt, 1.), anim::linear);
	}
	if (timer) update();
}

void ContinuousSlider::mouseMoveEvent(QMouseEvent *e) {
	if (_mouseDown) {
		updateDownValueFromPos(e->pos());
	}
}

float64 ContinuousSlider::computeValue(const QPoint &pos) const {
	auto seekRect = myrtlrect(getSeekRect());
	auto result = isHorizontal() ?
		(pos.x() - seekRect.x()) / float64(seekRect.width()) :
		(1. - (pos.y() - seekRect.y()) / float64(seekRect.height()));
	return snap(result, 0., 1.);
}

void ContinuousSlider::mousePressEvent(QMouseEvent *e) {
	_mouseDown = true;
	_downValue = computeValue(e->pos());
	update();
	if (_changeProgressCallback) {
		_changeProgressCallback(_downValue);
	}
}

void ContinuousSlider::mouseReleaseEvent(QMouseEvent *e) {
	if (_mouseDown) {
		_mouseDown = false;
		if (_changeFinishedCallback) {
			_changeFinishedCallback(_downValue);
		}
		a_value = anim::fvalue(_downValue, _downValue);
		_a_value.stop();
		update();
	}
}

void ContinuousSlider::wheelEvent(QWheelEvent *e) {
	if (_mouseDown) {
		return;
	}
#ifdef OS_MAC_OLD
	constexpr auto step = 120;
#else // OS_MAC_OLD
	constexpr auto step = static_cast<int>(QWheelEvent::DefaultDeltasPerStep);
#endif // OS_MAC_OLD
	constexpr auto coef = 1. / (step * 10.);

	auto deltaX = e->angleDelta().x(), deltaY = e->angleDelta().y();
	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		deltaY *= -1;
	}
	if (deltaX * deltaY < 0) {
		return;
	}

	auto delta = (deltaX >= 0 && deltaY >= 0) ? qMax(deltaX, deltaY) : qMin(deltaX, deltaY);
	auto finalValue = snap(a_value.to() + delta * coef, 0., 1.);
	setValue(finalValue, false);
	if (_changeProgressCallback) {
		_changeProgressCallback(finalValue);
	}
	_byWheelFinished->start(kByWheelFinishedTimeout);
}

void ContinuousSlider::updateDownValueFromPos(const QPoint &pos) {
	_downValue = computeValue(pos);
	update();
	if (_changeProgressCallback) {
		_changeProgressCallback(_downValue);
	}
}

void ContinuousSlider::enterEvent(QEvent *e) {
	setOver(true);
}

void ContinuousSlider::leaveEvent(QEvent *e) {
	setOver(false);
}

void ContinuousSlider::setOver(bool over) {
	if (_over == over) return;

	_over = over;
	auto from = _over ? 0. : 1., to = _over ? 1. : 0.;
	_a_over.start([this] { update(); }, from, to, getOverDuration());
}

} // namespace Ui
