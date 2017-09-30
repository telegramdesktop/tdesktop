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
#include "ui/wrap/fade_wrap.h"

#include "ui/widgets/shadow.h"

namespace Ui {

FadeWrap<RpWidget>::FadeWrap(
	QWidget *parent,
	object_ptr<RpWidget> &&child,
	bool scaled)
: Parent(parent, std::move(child))
, _duration(st::fadeWrapDuration)
, _animation(this, scaled) {
}

FadeWrap<RpWidget> *FadeWrap<RpWidget>::setDuration(int duration) {
	_duration = duration;
	return this;
}

FadeWrap<RpWidget> *FadeWrap<RpWidget>::toggleAnimated(
		bool shown) {
	auto updated = (shown != _animation.visible());
	if (shown) {
		_animation.fadeIn(_duration);
	} else {
		_animation.fadeOut(_duration);
	}
	if (updated) {
		_shownUpdated.fire_copy(shown);
	}
	return this;
}

FadeWrap<RpWidget> *FadeWrap<RpWidget>::toggleFast(bool shown) {
	auto updated = (shown != _animation.visible());
	if (shown) {
		_animation.show();
	} else {
		_animation.hide();
	}
	if (updated) {
		_shownUpdated.fire_copy(shown);
	}
	return this;
}

FadeWrap<RpWidget> *FadeWrap<RpWidget>::finishAnimations() {
	_animation.finish();
	return this;
}

FadeWrap<RpWidget> *FadeWrap<RpWidget>::toggleOn(
		rpl::producer<bool> &&shown) {
	std::move(shown)
		| rpl::start_with_next([this](bool shown) {
			toggleAnimated(shown);
		}, lifetime());
	finishAnimations();
	return this;
}

void FadeWrap<RpWidget>::paintEvent(QPaintEvent *e) {
	Painter p(this);
	_animation.paint(p);
}

FadeShadow::FadeShadow(QWidget *parent)
: FadeShadow(parent, st::shadowFg) {
}

FadeShadow::FadeShadow(QWidget *parent, style::color color)
: Parent(parent, object_ptr<PlainShadow>(parent, color)) {
	hideFast();
}

} // namespace Ui

