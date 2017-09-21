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
#include "ui/wrap/slide_wrap.h"

namespace Ui {

SlideWrap<RpWidget>::SlideWrap(
	QWidget *parent,
	object_ptr<RpWidget> child)
: SlideWrap(
	parent,
	std::move(child),
	style::margins(),
	st::slideWrapDuration) {
}

SlideWrap<RpWidget>::SlideWrap(
	QWidget *parent,
	object_ptr<RpWidget> child,
	const style::margins &padding)
: SlideWrap(
	parent,
	std::move(child),
	padding,
	st::slideWrapDuration) {
}

SlideWrap<RpWidget>::SlideWrap(
	QWidget *parent,
	object_ptr<RpWidget> child,
	int duration)
: SlideWrap(parent, std::move(child), style::margins(), duration) {
}

SlideWrap<RpWidget>::SlideWrap(
	QWidget *parent,
	const style::margins &padding)
: SlideWrap(parent, nullptr, padding, st::slideWrapDuration) {
}

SlideWrap<RpWidget>::SlideWrap(
	QWidget *parent,
	const style::margins &padding,
	int duration)
: SlideWrap(parent, nullptr, padding, duration) {
}

SlideWrap<RpWidget>::SlideWrap(
	QWidget *parent,
	object_ptr<RpWidget> child,
	const style::margins &padding,
	int duration)
: Parent(
	parent,
	object_ptr<PaddingWrap<RpWidget>>(
		parent,
		std::move(child),
		padding))
, _duration(duration) {
}

void SlideWrap<RpWidget>::animationStep() {
	auto newWidth = width();
	if (auto weak = wrapped()) {
		auto margins = getMargins();
		weak->moveToLeft(margins.left(), margins.top());
		newWidth = weak->width();
	}
	auto current = _slideAnimation.current(_shown ? 1. : 0.);
	auto newHeight = wrapped()
		? (_slideAnimation.animating()
		? anim::interpolate(0, wrapped()->heightNoMargins(), current)
		: (_shown ? wrapped()->height() : 0))
		: 0;
	if (newWidth != width() || newHeight != height()) {
		resize(newWidth, newHeight);
	}
	auto shouldBeHidden = !_shown && !_slideAnimation.animating();
	if (shouldBeHidden != isHidden()) {
		setVisible(!shouldBeHidden);
		if (shouldBeHidden) {
			myEnsureResized(this);
		}
	}
}

void SlideWrap<RpWidget>::setShown(bool shown) {
	_shown = shown;
	_shownUpdated.fire_copy(_shown);
}

void SlideWrap<RpWidget>::toggleAnimated(bool shown) {
	if (_shown == shown) {
		animationStep();
		return;
	}
	setShown(shown);
	_slideAnimation.start(
		[this] { animationStep(); },
		_shown ? 0. : 1.,
		_shown ? 1. : 0.,
		_duration,
		anim::linear);
	animationStep();
}

void SlideWrap<RpWidget>::toggleFast(bool shown) {
	setShown(shown);
	finishAnimations();
}

void SlideWrap<RpWidget>::finishAnimations() {
	_slideAnimation.finish();
	animationStep();
}

QMargins SlideWrap<RpWidget>::getMargins() const {
	auto result = wrapped()->getMargins();
	return (animating() || !_shown)
		? QMargins(result.left(), 0, result.right(), 0)
		: result;
}

int SlideWrap<RpWidget>::resizeGetHeight(int newWidth) {
	if (wrapped()) {
		wrapped()->resizeToWidth(newWidth);
	}
	return heightNoMargins();
}

void SlideWrap<RpWidget>::wrappedSizeUpdated(QSize size) {
	if (_slideAnimation.animating()) {
		animationStep();
	} else if (_shown) {
		resize(size);
	}
}

} // namespace Ui

