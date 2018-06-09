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

InfiniteRadialAnimation::InfiniteRadialAnimation(
	AnimationCallbacks &&callbacks,
	const style::InfiniteRadialAnimation &st)
: _st(st)
, _animation(std::move(callbacks)) {
}

void InfiniteRadialAnimation::start() {
	const auto now = getms();
	if (_workFinished <= now && (_workFinished || !_workStarted)) {
		_workStarted = now + _st.sineDuration;
		_workFinished = 0;
	}
	if (!_animation.animating()) {
		_animation.start();
	}
}

void InfiniteRadialAnimation::stop() {
	const auto now = getms();
	if (!_workFinished) {
		const auto zero = _workStarted - _st.sineDuration;
		const auto index = (now - zero + _st.sinePeriod - _st.sineShift)
			/ _st.sinePeriod;
		_workFinished = zero
			+ _st.sineShift
			+ (index * _st.sinePeriod)
			+ _st.sineDuration;
	} else if (_workFinished <= now) {
		_animation.stop();
	}
}

void InfiniteRadialAnimation::step(TimeMs ms) {
	_animation.step(ms);
}

void InfiniteRadialAnimation::draw(
		Painter &p,
		QPoint position,
		int outerWidth) {
	const auto state = computeState();

	auto o = p.opacity();
	p.setOpacity(o * state.shown);

	auto pen = _st.color->p;
	auto was = p.pen();
	pen.setWidth(_st.thickness);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);

	{
		PainterHighQualityEnabler hq(p);
		p.drawArc(
			rtlrect(
				position.x(),
				position.y(),
				_st.size.width(),
				_st.size.height(),
				outerWidth),
			state.arcFrom,
			state.arcLength);
	}

	p.setPen(was);
	p.setOpacity(o);
}

auto InfiniteRadialAnimation::computeState() -> State {
	const auto now = getms();
	const auto linear = int(((now * FullArcLength) / _st.linearPeriod)
		% FullArcLength);
	if (!_workStarted || (_workFinished && _workFinished <= now)) {
		const auto shown = 0.;
		_animation.stop();
		return {
			shown,
			linear,
			FullArcLength };
	}
	const auto min = int(std::round(FullArcLength * _st.arcMin));
	const auto max = int(std::round(FullArcLength * _st.arcMax));
	if (now <= _workStarted) {
		// zero .. _workStarted
		const auto zero = _workStarted - _st.sineDuration;
		const auto shown = (now - zero) / float64(_st.sineDuration);
		const auto length = anim::interpolate(
			FullArcLength,
			min,
			anim::sineInOut(1., snap(shown, 0., 1.)));
		return {
			shown,
			linear + (FullArcLength - length),
			length };
	} else if (!_workFinished || now <= _workFinished - _st.sineDuration) {
		// _workStared .. _workFinished - _st.sineDuration
		const auto shown = 1.;
		const auto cycles = (now - _workStarted) / _st.sinePeriod;
		const auto relative = (now - _workStarted) % _st.sinePeriod;
		const auto smallDuration = _st.sineShift - _st.sineDuration;
		const auto largeDuration = _st.sinePeriod
			- _st.sineShift
			- _st.sineDuration;
		const auto basic = int((linear
			+ (FullArcLength - min)
			+ cycles * (max - min)) % FullArcLength);
		if (relative <= smallDuration) {
			// localZero .. growStart
			return {
				shown,
				basic,
				min };
		} else if (relative <= smallDuration + _st.sineDuration) {
			// growStart .. growEnd
			const auto growLinear = (relative - smallDuration) /
				float64(_st.sineDuration);
			const auto growProgress = anim::sineInOut(1., growLinear);
			return {
				shown,
				basic,
				anim::interpolate(min, max, growProgress) };
		} else if (relative <= _st.sinePeriod - _st.sineDuration) {
			// growEnd .. shrinkStart
			return {
				shown,
				basic,
				max };
		} else {
			// shrinkStart .. shrinkEnd
			const auto shrinkLinear = (relative
				- (_st.sinePeriod - _st.sineDuration))
					/ float64(_st.sineDuration);
			const auto shrinkProgress = anim::sineInOut(1., shrinkLinear);
			const auto shrink = anim::interpolate(
				0,
				max - min,
				shrinkProgress);
			return {
				shown,
				basic + shrink,
				max - shrink }; // interpolate(max, min, shrinkProgress)
		}
	} else {
		// _workFinished - _st.sineDuration .. _workFinished
		const auto hidden = (now - (_workFinished - _st.sineDuration))
			/ float64(_st.sineDuration);
		const auto cycles = (_workFinished - _workStarted) / _st.sinePeriod;
		const auto basic = int((linear
			+ (FullArcLength - min)
			+ cycles * (max - min)) % FullArcLength);
		const auto length = anim::interpolate(
			min,
			FullArcLength,
			anim::sineInOut(1., snap(hidden, 0., 1.)));
		return {
			1. - hidden,
			basic,
			length };
	}
	//const auto frontPeriods = time / st.sinePeriod;
	//const auto frontCurrent = time % st.sinePeriod;
	//const auto frontProgress = anim::sineInOut(
	//	st.arcMax - st.arcMin,
	//	std::min(frontCurrent, TimeMs(st.sineDuration))
	//	/ float64(st.sineDuration));
	//const auto backTime = std::max(time - st.sineShift, 0LL);
	//const auto backPeriods = backTime / st.sinePeriod;
	//const auto backCurrent = backTime % st.sinePeriod;
	//const auto backProgress = anim::sineInOut(
	//	st.arcMax - st.arcMin,
	//	std::min(backCurrent, TimeMs(st.sineDuration))
	//	/ float64(st.sineDuration));
	//const auto front = linear + std::round((st.arcMin + frontProgress + frontPeriods * (st.arcMax - st.arcMin)) * FullArcLength);
	//const auto from = linear + std::round((backProgress + backPeriods * (st.arcMax - st.arcMin)) * FullArcLength);
	//const auto length = (front - from);

	//return {
	//	_opacity,
	//	from,
	//	length
	//};
}

} // namespace Ui
