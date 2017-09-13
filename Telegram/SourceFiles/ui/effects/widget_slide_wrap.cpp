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
#include "ui/effects/widget_slide_wrap.h"

namespace Ui {

PaddingWrap<RpWidget>::PaddingWrap(
	QWidget *parent,
	object_ptr<RpWidget> child,
	const style::margins &padding)
: Parent(parent, std::move(child))
, _padding(padding) {
	if (auto weak = wrapped()) {
		auto margins = weak->getMargins();
		weak->sizeValue()
			| rpl::on_next([this](QSize&&) { updateSize(); })
			| rpl::start(lifetime());
		weak->moveToLeft(_padding.left() + margins.left(), _padding.top() + margins.top());
	}
}

void PaddingWrap<RpWidget>::updateSize() {
	auto inner = [this] {
		if (auto weak = wrapped()) {
			return weak->rect();
		}
		return QRect(0, 0, _innerWidth, 0);
	}();
	resize(inner.marginsAdded(_padding).size());
}

int PaddingWrap<RpWidget>::naturalWidth() const {
	auto inner = [this] {
		if (auto weak = wrapped()) {
			return weak->naturalWidth();
		}
		return RpWidget::naturalWidth();
	}();
	return (inner < 0)
		? inner
		: (_padding.left() + inner + _padding.right());
}

int PaddingWrap<RpWidget>::resizeGetHeight(int newWidth) {
	_innerWidth = newWidth;
	if (auto weak = wrapped()) {
		weak->resizeToWidth(newWidth
			- _padding.left()
			- _padding.right());
	} else {
		updateSize();
	}
	return height();
}

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
	object_ptr<RpWidget> child,
	const style::margins &padding,
	int duration)
: Parent(parent, object_ptr<PaddingWrap<RpWidget>>(parent, std::move(child), padding))
, _duration(duration * 10) {
	if (auto weak = wrapped()) {
		weak->heightValue()
			| rpl::on_next([this](int newHeight) {
			if (_slideAnimation.animating()) {
				animationStep();
			} else if (_visible) {
				resize(width(), newHeight);
			}
		}) | rpl::start(lifetime());
	}
}

void SlideWrap<RpWidget>::animationStep() {
	if (wrapped()) {
		auto margins = getMargins();
		wrapped()->moveToLeft(margins.left(), margins.top());
	}
	auto current = _slideAnimation.current(_visible ? 1. : 0.);
	auto newHeight = wrapped()
		? (_slideAnimation.animating()
		? anim::interpolate(0, wrapped()->heightNoMargins(), current)
		: (_visible ? wrapped()->height() : 0))
		: 0;
	if (newHeight != height()) {
		resize(width(), newHeight);
	}
	auto shouldBeHidden = !_visible && !_slideAnimation.animating();
	if (shouldBeHidden != isHidden()) {
		setVisible(!shouldBeHidden);
	}
}

void SlideWrap<RpWidget>::toggleAnimated(bool visible) {
	if (_visible == visible) {
		animationStep();
		return;
	}
	_visible = visible;
	_slideAnimation.start(
		[this] { animationStep(); },
		_visible ? 0. : 1.,
		_visible ? 1. : 0.,
		_duration,
		anim::linear);
	animationStep();
}

void SlideWrap<RpWidget>::toggleFast(bool visible) {
	_visible = visible;
	finishAnimations();
}

void SlideWrap<RpWidget>::finishAnimations() {
	_slideAnimation.finish();
	animationStep();
}

QMargins SlideWrap<RpWidget>::getMargins() const {
	auto result = wrapped()->getMargins();
	return (animating() || !_visible)
		? QMargins(result.left(), 0, result.right(), 0)
		: result;
}

int SlideWrap<RpWidget>::resizeGetHeight(int newWidth) {
	if (wrapped()) {
		wrapped()->resizeToWidth(newWidth);
	}
	return height();
}

} // namespace Ui
