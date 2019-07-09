/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/wrap.h"
#include "ui/effects/fade_animation.h"

namespace Ui {

template <typename Widget = RpWidget>
class FadeWrapScaled;

template <typename Widget = RpWidget>
class FadeWrap;

template <>
class FadeWrap<RpWidget> : public Wrap<RpWidget> {
	using Parent = Wrap<RpWidget>;

public:
	FadeWrap(
		QWidget *parent,
		object_ptr<RpWidget> &&child,
		float64 scale = 1.);

	FadeWrap *setDuration(int duration);
	FadeWrap *toggle(bool shown, anim::type animated);
	FadeWrap *show(anim::type animated) {
		return toggle(true, animated);
	}
	FadeWrap *hide(anim::type animated) {
		return toggle(false, animated);
	}
	FadeWrap *finishAnimating();
	FadeWrap *toggleOn(rpl::producer<bool> &&shown);

	bool animating() const {
		return _animation.animating();
	}
	bool toggled() const {
		return _animation.visible();
	}
	auto toggledValue() const {
		return _toggledChanged.events_starting_with(
			_animation.visible());
	}

protected:
	void paintEvent(QPaintEvent *e) final override;

private:
	rpl::event_stream<bool> _toggledChanged;
	FadeAnimation _animation;
	int _duration = 0;

};

template <typename Widget>
class FadeWrap : public Wrap<Widget, FadeWrap<RpWidget>> {
	using Parent = Wrap<Widget, FadeWrap<RpWidget>>;

public:
	FadeWrap(
		QWidget *parent,
		object_ptr<Widget> &&child,
		float64 scale = 1.)
	: Parent(parent, std::move(child), scale) {
	}

	FadeWrap *setDuration(int duration) {
		return chain(Parent::setDuration(duration));
	}
	FadeWrap *toggle(bool shown, anim::type animated) {
		return chain(Parent::toggle(shown, animated));
	}
	FadeWrap *show(anim::type animated) {
		return chain(Parent::show(animated));
	}
	FadeWrap *hide(anim::type animated) {
		return chain(Parent::hide(animated));
	}
	FadeWrap *finishAnimating() {
		return chain(Parent::finishAnimating());
	}
	FadeWrap *toggleOn(rpl::producer<bool> &&shown) {
		return chain(Parent::toggleOn(std::move(shown)));
	}

private:
	FadeWrap *chain(FadeWrap<RpWidget> *result) {
		return static_cast<FadeWrap*>(result);
	}

};

template <typename Widget>
class FadeWrapScaled : public FadeWrap<Widget> {
	using Parent = FadeWrap<Widget>;

public:
	FadeWrapScaled(QWidget *parent, object_ptr<Widget> &&child)
	: Parent(parent, std::move(child), 0.) {
	}

};

class PlainShadow;
class FadeShadow : public FadeWrap<PlainShadow> {
	using Parent = FadeWrap<PlainShadow>;

public:
	FadeShadow(QWidget *parent);
	FadeShadow(QWidget *parent, style::color color);

};

} // namespace Ui

