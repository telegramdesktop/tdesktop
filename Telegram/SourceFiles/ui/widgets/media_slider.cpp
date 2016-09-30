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
#include "ui/widgets/media_slider.h"

#include "styles/style_widgets.h"

namespace Ui {

MediaSlider::MediaSlider(QWidget *parent, const style::MediaSlider &st) : TWidget(parent)
, _st(st)
, _a_value(animation(this, &MediaSlider::step_value)) {
	setCursor(style::cur_pointer);
}

float64 MediaSlider::value() const {
	return a_value.current();
}

void MediaSlider::setDisabled(bool disabled) {
	if (_disabled != disabled) {
		_disabled = disabled;
		setCursor(_disabled ? style::cur_default : style::cur_pointer);
		update();
	}
}

void MediaSlider::setValue(float64 value, bool animated) {
	if (animated) {
		a_value.start(value);
		_a_value.start();
	} else {
		a_value = anim::fvalue(value, value);
		_a_value.stop();
	}
	update();
}

void MediaSlider::setFadeOpacity(float64 opacity) {
	_fadeOpacity = opacity;
	update();
}

void MediaSlider::step_value(float64 ms, bool timer) {
	float64 dt = ms / (2 * AudioVoiceMsgUpdateView);
	if (dt >= 1) {
		_a_value.stop();
		a_value.finish();
	} else {
		a_value.update(qMin(dt, 1.), anim::linear);
	}
	if (timer) update();
}

int MediaSlider::lineLeft() const {
	return (_st.seekSize.width() / 2);
}

int MediaSlider::lineWidth() const {
	return (width() - _st.seekSize.width());
}

void MediaSlider::paintEvent(QPaintEvent *e) {
	Painter p(this);

	int radius = _st.width / 2;
	p.setOpacity(_fadeOpacity);
	p.setPen(Qt::NoPen);
	p.setRenderHint(QPainter::HighQualityAntialiasing);

	auto ms = getms();
	_a_value.step(ms);
	auto over = _a_over.current(ms, _over ? 1. : 0.);
	int skip = lineLeft();
	int length = lineWidth();
	float64 prg = _mouseDown ? _downValue : a_value.current();
	int32 from = skip, mid = _disabled ? 0 : qRound(from + prg * length), end = from + length;
	if (mid > from) {
		p.setClipRect(0, 0, mid, height());
		p.setOpacity(_fadeOpacity * (over * _st.activeOpacity + (1. - over) * _st.inactiveOpacity));
		p.setBrush(_st.activeFg);
		p.drawRoundedRect(from, (height() - _st.width) / 2, mid + radius - from, _st.width, radius, radius);
	}
	if (end > mid) {
		p.setClipRect(mid, 0, width() - mid, height());
		p.setOpacity(_fadeOpacity);
		p.setBrush(_st.inactiveFg);
		p.drawRoundedRect(mid - radius, (height() - _st.width) / 2, end - (mid - radius), _st.width, radius, radius);
	}
	if (!_disabled && over > 0) {
		int x = mid - skip;
		p.setClipRect(rect());
		p.setOpacity(_fadeOpacity * _st.activeOpacity);
		auto seekButton = QRect(x, (height() - _st.seekSize.height()) / 2, _st.seekSize.width(), _st.seekSize.height());
		int remove = ((1. - over) * _st.seekSize.width()) / 2.;
		if (remove * 2 < _st.seekSize.width()) {
			p.setBrush(_st.activeFg);
			p.drawEllipse(seekButton.marginsRemoved(QMargins(remove, remove, remove, remove)));
		}
	}
}

void MediaSlider::mouseMoveEvent(QMouseEvent *e) {
	if (_mouseDown) {
		updateDownValueFromPos(e->pos().x());
	}
}

void MediaSlider::mousePressEvent(QMouseEvent *e) {
	_mouseDown = true;
	_downValue = snap((e->pos().x() - lineLeft()) / float64(lineWidth()), 0., 1.);
	update();
	if (_changeProgressCallback) {
		_changeProgressCallback(_downValue);
	}
}

void MediaSlider::mouseReleaseEvent(QMouseEvent *e) {
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

void MediaSlider::updateDownValueFromPos(int pos) {
	_downValue = snap((pos - lineLeft()) / float64(lineWidth()), 0., 1.);
	update();
	if (_changeProgressCallback) {
		_changeProgressCallback(_downValue);
	}
}

void MediaSlider::enterEvent(QEvent *e) {
	setOver(true);
}

void MediaSlider::leaveEvent(QEvent *e) {
	setOver(false);
}

void MediaSlider::setOver(bool over) {
	if (_over == over) return;

	_over = over;
	auto from = _over ? 0. : 1., to = _over ? 1. : 0.;
	_a_over.start([this] { update(); }, from, to, _st.duration);
}

} // namespace Ui
