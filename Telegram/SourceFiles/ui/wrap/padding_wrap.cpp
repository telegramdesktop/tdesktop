/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
		SendPendingMoveResizeEvents(weak);
	} else {
		resize(QSize(
			_padding.left() + newWidth + _padding.right(),
			_padding.top() + _padding.bottom()));
	}
	return heightNoMargins();
}

} // namespace Ui
