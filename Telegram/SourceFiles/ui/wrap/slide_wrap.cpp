/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/wrap/slide_wrap.h"

#include <rpl/combine.h>
#include <range/v3/algorithm/find.hpp>

namespace Ui {

SlideWrap<RpWidget>::SlideWrap(
	QWidget *parent,
	object_ptr<RpWidget> &&child)
: SlideWrap(
	parent,
	std::move(child),
	style::margins()) {
}

SlideWrap<RpWidget>::SlideWrap(
	QWidget *parent,
	const style::margins &padding)
: SlideWrap(parent, nullptr, padding) {
}

SlideWrap<RpWidget>::SlideWrap(
	QWidget *parent,
	object_ptr<RpWidget> &&child,
	const style::margins &padding)
: Parent(
	parent,
	object_ptr<PaddingWrap<RpWidget>>(
		parent,
		std::move(child),
		padding))
, _duration(st::slideWrapDuration) {
}

SlideWrap<RpWidget> *SlideWrap<RpWidget>::setDuration(int duration) {
	_duration = duration;
	return this;
}

SlideWrap<RpWidget> *SlideWrap<RpWidget>::toggle(
		bool shown,
		anim::type animated) {
	auto animate = (animated == anim::type::normal) && _duration;
	auto changed = (_toggled != shown);
	if (changed) {
		_toggled = shown;
		if (animate) {
			_animation.start(
				[this] { animationStep(); },
				_toggled ? 0. : 1.,
				_toggled ? 1. : 0.,
				_duration,
				anim::linear);
		}
	}
	if (animate) {
		animationStep();
	} else {
		finishAnimating();
	}
	if (changed) {
		_toggledChanged.fire_copy(_toggled);
	}
	return this;
}

SlideWrap<RpWidget> *SlideWrap<RpWidget>::finishAnimating() {
	_animation.finish();
	animationStep();
	return this;
}

SlideWrap<RpWidget> *SlideWrap<RpWidget>::toggleOn(
		rpl::producer<bool> &&shown) {
	std::move(
		shown
	) | rpl::start_with_next([this](bool shown) {
		toggle(shown, anim::type::normal);
	}, lifetime());
	finishAnimating();
	return this;
}

void SlideWrap<RpWidget>::animationStep() {
	auto newWidth = width();
	if (auto weak = wrapped()) {
		auto margins = getMargins();
		weak->moveToLeft(margins.left(), margins.top());
		newWidth = weak->width();
	}
	auto current = _animation.current(_toggled ? 1. : 0.);
	auto newHeight = wrapped()
		? (_animation.animating()
		? anim::interpolate(0, wrapped()->heightNoMargins(), current)
		: (_toggled ? wrapped()->height() : 0))
		: 0;
	if (newWidth != width() || newHeight != height()) {
		resize(newWidth, newHeight);
	}
	auto shouldBeHidden = !_toggled && !_animation.animating();
	if (shouldBeHidden != isHidden()) {
		const auto guard = make_weak(this);
		setVisible(!shouldBeHidden);
		if (shouldBeHidden && guard) {
			SendPendingMoveResizeEvents(this);
		}
	}
}

QMargins SlideWrap<RpWidget>::getMargins() const {
	auto result = wrapped()->getMargins();
	return (animating() || !_toggled)
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
	if (_animation.animating()) {
		animationStep();
	} else if (_toggled) {
		resize(size);
	}
}

rpl::producer<bool> MultiSlideTracker::atLeastOneShownValue() const {
	auto shown = std::vector<rpl::producer<bool>>();
	shown.reserve(_widgets.size());
	for (auto &widget : _widgets) {
		shown.push_back(widget->toggledValue());
	}
	return rpl::combine(
		std::move(shown),
		[](const std::vector<bool> &values) {
			return ranges::find(values, true) != values.end();
		});
}

} // namespace Ui

