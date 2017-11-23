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
#include "ui/wrap/padding_wrap.h"

namespace Ui {

PaddingWrap<RpWidget>::PaddingWrap(
	QWidget *parent,
	object_ptr<RpWidget> &&child,
	const style::margins &padding)
: Parent(parent, std::move(child)) {
	setPadding(padding);
}

void PaddingWrap<RpWidget>::setPadding(const style::margins &padding) {
	if (_padding != padding) {
		auto oldWidth = width() - _padding.left() - _padding.top();
		_padding = padding;

		if (auto weak = wrapped()) {
			wrappedSizeUpdated(weak->size());

			auto margins = weak->getMargins();
			weak->moveToLeft(
				_padding.left() + margins.left(),
				_padding.top() + margins.top());
		} else {
			resize(QSize(
				_padding.left() + oldWidth + _padding.right(),
				_padding.top() + _padding.bottom()));
		}
	}
}

void PaddingWrap<RpWidget>::wrappedSizeUpdated(QSize size) {
	resize(QRect(QPoint(), size).marginsAdded(_padding).size());
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
	if (auto weak = wrapped()) {
		weak->resizeToWidth(newWidth
			- _padding.left()
			- _padding.right());
	} else {
		resize(QSize(
			_padding.left() + newWidth + _padding.right(),
			_padding.top() + _padding.bottom()));
	}
	return heightNoMargins();
}

} // namespace Ui
