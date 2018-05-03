/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/radial_animation.h"

#include "styles/style_widgets.h"

namespace Ui {

RadialAnimation::RadialAnimation(AnimationCallbacks &&callbacks)
: a_arcStart(0, FullArcLength)
, _animation(std::move(callbacks)) {
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
	auto o = p.opacity();
	p.setOpacity(o * _opacity);

	auto pen = color->p;
	auto was = p.pen();
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

InfiniteRadialAnimation::InfiniteRadialAnimation(AnimationCallbacks &&callbacks)
: _animation(std::move(callbacks)) {
}

void InfiniteRadialAnimation::start() {
	_start = _changed = getms();
	_finished = false;
	_animation.start();
}

void InfiniteRadialAnimation::update(bool finished, TimeMs ms) {
	if (_finished != finished) {
		_finished = finished;
		_changed = ms;
	}

	auto dt = float64(ms - _changed);
	auto fulldt = float64(ms - _start);
	_opacity = qMin(fulldt / st::radialDuration, 1.);
	if (!finished) {
	} else if (dt >= st::radialDuration) {
		stop();
	} else {
		auto r = dt / st::radialDuration;
		_opacity *= 1 - r;
	}
}

void InfiniteRadialAnimation::stop() {
	_start = _changed = 0;
	_animation.stop();
}

void InfiniteRadialAnimation::step(TimeMs ms) {
	_animation.step(ms);
}

void InfiniteRadialAnimation::draw(
		Painter &p,
		QPoint position,
		int outerWidth,
		const style::InfiniteRadialAnimation &st) {
	auto o = p.opacity();
	p.setOpacity(o * _opacity);

	auto pen = st.color->p;
	auto was = p.pen();
	pen.setWidth(st.thickness);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);

	const auto time = (getms() - _start);
	const auto linear = (time * FullArcLength) / st.linearPeriod;
	const auto frontPeriods = time / st.sinePeriod;
	const auto frontCurrent = time % st.sinePeriod;
	const auto frontProgress = anim::sineInOut(
		st.arcMax - st.arcMin,
		std::min(frontCurrent, TimeMs(st.sineDuration))
			/ float64(st.sineDuration));
	const auto backTime = std::max(time - st.sineShift, 0LL);
	const auto backPeriods = backTime / st.sinePeriod;
	const auto backCurrent = backTime % st.sinePeriod;
	const auto backProgress = anim::sineInOut(
		st.arcMax - st.arcMin,
		std::min(backCurrent, TimeMs(st.sineDuration))
			/ float64(st.sineDuration));
	const auto front = linear + std::round((st.arcMin + frontProgress + frontPeriods * (st.arcMax - st.arcMin)) * FullArcLength);
	const auto from = linear + std::round((backProgress + backPeriods * (st.arcMax - st.arcMin)) * FullArcLength);
	const auto len = (front - from);

	//if (rtl()) {
	//	from = QuarterArcLength - (from - QuarterArcLength) - len;
	//	if (from < 0) from += FullArcLength;
	//}

	{
		PainterHighQualityEnabler hq(p);
		p.drawArc(
			rtlrect(
				position.x(),
				position.y(),
				st.size.width(),
				st.size.height(),
				outerWidth),
			from,
			len);
	}

	p.setPen(was);
	p.setOpacity(o);
}

} // namespace Ui
