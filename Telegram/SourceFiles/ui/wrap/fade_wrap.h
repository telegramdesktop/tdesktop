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

