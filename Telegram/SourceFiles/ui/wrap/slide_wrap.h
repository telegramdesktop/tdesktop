/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/padding_wrap.h"

namespace Ui {

template <typename Widget = RpWidget>
class SlideWrap;

template <>
class SlideWrap<RpWidget> : public Wrap<PaddingWrap<RpWidget>> {
	using Parent = Wrap<PaddingWrap<RpWidget>>;

public:
	SlideWrap(
		QWidget *parent,
		object_ptr<RpWidget> &&child);
	SlideWrap(
		QWidget *parent,
		const style::margins &padding);
	SlideWrap(
		QWidget *parent,
		object_ptr<RpWidget> &&child,
		const style::margins &padding);

	SlideWrap *setDuration(int duration);
	SlideWrap *toggle(bool shown, anim::type animated);
	SlideWrap *show(anim::type animated) {
		return toggle(true, animated);
	}
	SlideWrap *hide(anim::type animated) {
		return toggle(false, animated);
	}
	SlideWrap *finishAnimating();
	SlideWrap *toggleOn(rpl::producer<bool> &&shown);

	bool animating() const {
		return _animation.animating();
	}
	bool toggled() const {
		return _toggled;
	}
	auto toggledValue() const {
		return _toggledChanged.events_starting_with_copy(_toggled);
	}

	QMargins getMargins() const override;

protected:
	int resizeGetHeight(int newWidth) override;
	void wrappedSizeUpdated(QSize size) override;

private:
	void animationStep();

	bool _toggled = true;
	rpl::event_stream<bool> _toggledChanged;
	Animation _animation;
	int _duration = 0;

};

template <typename Widget>
class SlideWrap : public Wrap<PaddingWrap<Widget>, SlideWrap<RpWidget>> {
	using Parent = Wrap<PaddingWrap<Widget>, SlideWrap<RpWidget>>;

public:
	SlideWrap(
		QWidget *parent,
		object_ptr<Widget> &&child)
	: Parent(parent, std::move(child)) {
	}
	SlideWrap(
		QWidget *parent,
		const style::margins &padding)
	: Parent(parent, padding) {
	}
	SlideWrap(
		QWidget *parent,
		object_ptr<Widget> &&child,
		const style::margins &padding)
	: Parent(parent, std::move(child), padding) {
	}

	SlideWrap *setDuration(int duration) {
		return chain(Parent::setDuration(duration));
	}
	SlideWrap *toggle(bool shown, anim::type animated) {
		return chain(Parent::toggle(shown, animated));
	}
	SlideWrap *show(anim::type animated) {
		return chain(Parent::show(animated));
	}
	SlideWrap *hide(anim::type animated) {
		return chain(Parent::hide(animated));
	}
	SlideWrap *finishAnimating() {
		return chain(Parent::finishAnimating());
	}
	SlideWrap *toggleOn(rpl::producer<bool> &&shown) {
		return chain(Parent::toggleOn(std::move(shown)));
	}

private:
	SlideWrap *chain(SlideWrap<RpWidget> *result) {
		return static_cast<SlideWrap*>(result);
	}

};

inline object_ptr<SlideWrap<>> CreateSlideSkipWidget(
		QWidget *parent,
		int skip) {
	return object_ptr<SlideWrap<>>(
		parent,
		QMargins(0, 0, 0, skip));
}

class MultiSlideTracker {
public:
	template <typename Widget>
	void track(const Ui::SlideWrap<Widget> *wrap) {
		_widgets.push_back(wrap);
	}

	rpl::producer<bool> atLeastOneShownValue() const;

private:
	std::vector<const Ui::SlideWrap<Ui::RpWidget>*> _widgets;

};

} // namespace Ui

