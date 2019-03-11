/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
struct InfiniteRadialAnimation;
} // namespace style

namespace Ui {

struct RadialState {
	float64 shown = 0.;
	int arcFrom = 0;
	int arcLength = FullArcLength;
};

class RadialAnimation {
public:
	RadialAnimation(AnimationCallbacks &&callbacks);

	float64 opacity() const {
		return _opacity;
	}
	bool animating() const {
		return _animation.animating();
	}

	void start(float64 prg);
	bool update(float64 prg, bool finished, crl::time ms);
	void stop();

	void step(crl::time ms);
	void step() {
		step(crl::now());
	}

	void draw(
		Painter &p,
		const QRect &inner,
		int32 thickness,
		style::color color) const;

	RadialState computeState() const;

private:
	crl::time _firstStart = 0;
	crl::time _lastStart = 0;
	crl::time _lastTime = 0;
	float64 _opacity = 0.;
	anim::value a_arcEnd;
	anim::value a_arcStart;
	BasicAnimation _animation;
	bool _finished = false;

};

class InfiniteRadialAnimation {
public:
	InfiniteRadialAnimation(
		AnimationCallbacks &&callbacks,
		const style::InfiniteRadialAnimation &st);

	bool animating() const {
		return _animation.animating();
	}

	void start(crl::time skip = 0);
	void stop(anim::type animated = anim::type::normal);

	void step(crl::time ms);
	void step() {
		step(crl::now());
	}

	void draw(
		Painter &p,
		QPoint position,
		int outerWidth);
	void draw(
		Painter &p,
		QPoint position,
		QSize size,
		int outerWidth);

	RadialState computeState();

private:
	const style::InfiniteRadialAnimation &_st;
	crl::time _workStarted = 0;
	crl::time _workFinished = 0;
	BasicAnimation _animation;

};

} // namespace Ui
