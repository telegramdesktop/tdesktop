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

namespace Ui {

template <typename Widget = RpWidget>
class PaddingWrap;

template <>
class PaddingWrap<RpWidget> : public Wrap<RpWidget> {
	using Parent = Wrap<RpWidget>;

public:
	PaddingWrap(
		QWidget *parent,
		object_ptr<RpWidget> &&child,
		const style::margins &padding);

	style::margins padding() const {
		return _padding;
	}
	void setPadding(const style::margins &padding);

	int naturalWidth() const override;

protected:
	int resizeGetHeight(int newWidth) override;
	void wrappedSizeUpdated(QSize size) override;

private:
	style::margins _padding;

};

template <typename Widget>
class PaddingWrap : public Wrap<Widget, PaddingWrap<RpWidget>> {
	using Parent = Wrap<Widget, PaddingWrap<RpWidget>>;

public:
	PaddingWrap(
		QWidget *parent,
		object_ptr<Widget> &&child,
		const style::margins &padding)
	: Parent(parent, std::move(child), padding) {
	}

};

class FixedHeightWidget : public RpWidget {
public:
	FixedHeightWidget(QWidget *parent, int height)
	: RpWidget(parent) {
		resize(width(), height);
	}

};

inline object_ptr<FixedHeightWidget> CreateSkipWidget(
		QWidget *parent,
		int skip) {
	return object_ptr<FixedHeightWidget>(
		parent,
		skip);
}

} // namespace Ui
