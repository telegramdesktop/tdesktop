/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/vertical_drum_picker.h"

#include "ui/effects/animation_value_f.h"

namespace Ui {

PickerAnimation::PickerAnimation() {
}

void PickerAnimation::jumpToOffset(int offset) {
	_result.from = _result.current;
	_result.to += offset;
	_animation.stop();
	auto callback = [=](float64 value) {
		const auto was = _result.current;
		_result.current = anim::interpolateF(
			_result.from,
			_result.to,
			value);
		_updates.fire(_result.current - was);
	};
	_animation.start(
		std::move(callback),
		0.,
		1.,
		st::fadeWrapDuration);
}

void PickerAnimation::setResult(float64 from, float64 current, float64 to) {
	_result = { from, current, to };
}

rpl::producer<PickerAnimation::Shift> PickerAnimation::updates() const {
	return _updates.events();
}

VerticalDrumPicker::VerticalDrumPicker(
	not_null<Ui::RpWidget*> parent,
	PaintItemCallback &&paintCallback,
	int itemsCount,
	int itemHeight,
	int startIndex)
: RpWidget(parent)
, _itemsCount(itemsCount)
, _itemHeight(itemHeight)
, _paintCallback(std::move(paintCallback))
, _pendingStartIndex(startIndex) {
	Expects(_paintCallback != nullptr);

	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		_itemsVisibleCount = std::ceil(float64(s.height()) / _itemHeight);
		if (_pendingStartIndex && _itemsVisibleCount) {
			_index = normalizedIndex(base::take(_pendingStartIndex)
				- _itemsVisibleCount / 2);
		}
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);

		const auto outerWidth = width();
		const auto centerY = height() / 2.;
		const auto shiftedY = _itemHeight * _shift;
		for (auto i = -1; i < (_itemsVisibleCount + 1); i++) {
			const auto y = (_itemHeight * i + shiftedY);
			_paintCallback(
				p,
				normalizedIndex(i + _index),
				y,
				((y + _itemHeight / 2.) - centerY) / centerY,
				outerWidth);
		}
	}, lifetime());

	_animation.updates(
	) | rpl::start_with_next([=](PickerAnimation::Shift shift) {
		increaseShift(shift);
	}, lifetime());
}

void VerticalDrumPicker::increaseShift(float64 by) {
	_shift += by;
	if (_shift >= 1.) {
		_shift -= 1.;
		_index--;
		_index = normalizedIndex(_index);
	} else if (_shift <= -1.) {
		_shift += 1.;
		_index++;
		_index = normalizedIndex(_index);
	}
	update();
}

void VerticalDrumPicker::handleWheelEvent(not_null<QWheelEvent*> e) {
	const auto direction = Ui::WheelDirection(e);
	if (direction) {
		_animation.jumpToOffset(direction);
	} else {
		increaseShift(
			std::min(e->pixelDelta().y() / float64(_itemHeight), 0.99));
		if (e->phase() == Qt::ScrollEnd) {
			animationDataFromIndex();
			_animation.jumpToOffset(0);
		}
	}
}

void VerticalDrumPicker::handleKeyEvent(not_null<QKeyEvent*> e) {
	if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Up) {
		_animation.jumpToOffset(1);
	} else if (e->key() == Qt::Key_PageUp && !e->isAutoRepeat()) {
		_animation.jumpToOffset(_itemsVisibleCount);
	} else if (e->key() == Qt::Key_Right || e->key() == Qt::Key_Down) {
		_animation.jumpToOffset(-1);
	} else if (e->key() == Qt::Key_PageDown && !e->isAutoRepeat()) {
		_animation.jumpToOffset(-_itemsVisibleCount);
	}
}

void VerticalDrumPicker::handleMouseEvent(not_null<QMouseEvent*> e) {
	if (e->type() == QEvent::MouseButtonPress) {
		_mouse.pressed = true;
		_mouse.lastPositionY = e->pos().y();
	} else if (e->type() == QEvent::MouseMove) {
		if (_mouse.pressed) {
			const auto was = _mouse.lastPositionY;
			_mouse.lastPositionY = e->pos().y();
			const auto diff = _mouse.lastPositionY - was;
			increaseShift(float64(diff) / _itemHeight);
			_mouse.clickDisabled = true;
		}
	} else if (e->type() == QEvent::MouseButtonRelease) {
		if (_mouse.clickDisabled) {
			animationDataFromIndex();
			_animation.jumpToOffset(0);
		} else {
			_mouse.lastPositionY = e->pos().y();
			const auto toOffset = (_itemsVisibleCount / 2)
				- (_mouse.lastPositionY / _itemHeight);
			_animation.jumpToOffset(toOffset);
		}
		_mouse = {};
	}
}


void VerticalDrumPicker::wheelEvent(QWheelEvent *e) {
	handleWheelEvent(e);
}

void VerticalDrumPicker::mousePressEvent(QMouseEvent *e) {
	handleMouseEvent(e);
}

void VerticalDrumPicker::mouseMoveEvent(QMouseEvent *e) {
	handleMouseEvent(e);
}

void VerticalDrumPicker::mouseReleaseEvent(QMouseEvent *e) {
	handleMouseEvent(e);
}

void VerticalDrumPicker::keyPressEvent(QKeyEvent *e) {
	handleKeyEvent(e);
}

void VerticalDrumPicker::animationDataFromIndex() {
	_animation.setResult(
		_index,
		_index + _shift,
		std::round(_index + _shift));
}

int VerticalDrumPicker::normalizedIndex(int index) const {
	if (index < 0) {
		index += _itemsCount;
	} else if (index >= _itemsCount) {
		index -= _itemsCount;
	}
	return index;
}

int VerticalDrumPicker::index() const {
	return normalizedIndex(_index + _itemsVisibleCount / 2);
}

} // namespace Ui
