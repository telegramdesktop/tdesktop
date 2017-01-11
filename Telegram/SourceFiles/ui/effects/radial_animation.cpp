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
#include "stdafx.h"
#include "ui/effects/radial_animation.h"

namespace Ui {

RadialAnimation::RadialAnimation(AnimationCallbacks &&callbacks)
	: a_arcStart(0, FullArcLength)
	, _animation(std_::move(callbacks)) {
}

void RadialAnimation::start(float64 prg) {
	_firstStart = _lastStart = _lastTime = getms();
	int32 iprg = qRound(qMax(prg, 0.0001) * AlmostFullArcLength), iprgstrict = qRound(prg * AlmostFullArcLength);
	a_arcEnd = anim::value(iprgstrict, iprg);
	_animation.start();
}

void RadialAnimation::update(float64 prg, bool finished, TimeMs ms) {
	auto iprg = qRound(qMax(prg, 0.0001) * AlmostFullArcLength);
	if (iprg != qRound(a_arcEnd.to())) {
		a_arcEnd.start(iprg);
		_lastStart = _lastTime;
	}
	_lastTime = ms;

	auto dt = float64(ms - _lastStart);
	auto fulldt = float64(ms - _firstStart);
	_opacity = qMin(fulldt / st::radialDuration, 1.);
	if (!finished) {
		a_arcEnd.update(1. - (st::radialDuration / (st::radialDuration + dt)), anim::linear);
	} else if (dt >= st::radialDuration) {
		a_arcEnd.update(1., anim::linear);
		stop();
	} else {
		auto r = dt / st::radialDuration;
		a_arcEnd.update(r, anim::linear);
		_opacity *= 1 - r;
	}
	auto fromstart = fulldt / st::radialPeriod;
	a_arcStart.update(fromstart - std::floor(fromstart), anim::linear);
}

void RadialAnimation::stop() {
	_firstStart = _lastStart = _lastTime = 0;
	a_arcEnd = anim::value();
	_animation.stop();
}

void RadialAnimation::step(TimeMs ms) {
	_animation.step(ms);
}

void RadialAnimation::draw(Painter &p, const QRect &inner, int32 thickness, style::color color) {
	float64 o = p.opacity();
	p.setOpacity(o * _opacity);

	QPen pen(color->p), was(p.pen());
	pen.setWidth(thickness);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);

	auto len = MinArcLength + qRound(a_arcEnd.current());
	auto from = QuarterArcLength - qRound(a_arcStart.current()) - len;
	if (rtl()) {
		from = QuarterArcLength - (from - QuarterArcLength) - len;
		if (from < 0) from += FullArcLength;
	}

	{
		PainterHighQualityEnabler hq(p);
		p.drawArc(inner, from, len);
	}

	p.setPen(was);
	p.setOpacity(o);
}

} // namespace Ui
