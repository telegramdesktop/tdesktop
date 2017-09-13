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

#include "ui/wrap/padding_wrap.h"

namespace Ui {

template <typename Widget = RpWidget>
class SlideWrap;

template <>
class SlideWrap<RpWidget> : public Wrap<PaddingWrap<RpWidget>> {
	using Parent = Wrap<PaddingWrap<RpWidget>>;

public:
	SlideWrap(QWidget *parent, object_ptr<RpWidget> child);
	SlideWrap(
		QWidget *parent,
		object_ptr<RpWidget> child,
		const style::margins &padding);
	SlideWrap(
		QWidget *parent,
		object_ptr<RpWidget> child,
		int duration);
	SlideWrap(
		QWidget *parent,
		object_ptr<RpWidget> child,
		const style::margins &padding,
		int duration);

	void toggleAnimated(bool visible);
	void toggleFast(bool visible);

	void showAnimated() {
		toggleAnimated(true);
	}
	void hideAnimated() {
		toggleAnimated(false);
	}

	void showFast() {
		toggleFast(true);
	}
	void hideFast() {
		toggleFast(false);
	}

	bool animating() const {
		return _slideAnimation.animating();
	}
	void finishAnimations();

	QMargins getMargins() const override;

	bool isHiddenOrHiding() const {
		return !_visible;
	}

protected:
	int resizeGetHeight(int newWidth) override;
	void wrappedSizeUpdated(QSize size) override;

private:
	void animationStep();

	bool _visible = true;
	Animation _slideAnimation;
	int _duration = 0;

};

template <typename Widget>
class SlideWrap : public Wrap<PaddingWrap<Widget>, SlideWrap<RpWidget>> {
	using Parent = Wrap<PaddingWrap<Widget>, SlideWrap<RpWidget>>;

public:
	SlideWrap(QWidget *parent, object_ptr<Widget> child)
	: Parent(parent, std::move(child)) {
	}
	SlideWrap(
		QWidget *parent,
		object_ptr<Widget> child,
		const style::margins &padding)
	: Parent(parent, std::move(child), padding) {
	}
	SlideWrap(
		QWidget *parent,
		object_ptr<Widget> child,
		int duration)
	: Parent(parent, std::move(child), duration) {
	}
	SlideWrap(
		QWidget *parent,
		object_ptr<Widget> child,
		const style::margins &padding,
		int duration)
	: Parent(parent, std::move(child), padding, duration) {
	}

};

} // namespace Ui

