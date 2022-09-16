/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/vertical_drum_picker.h"

#include "ui/effects/animation_value_f.h"
#include "styles/style_basic.h"

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
	int startIndex,
	bool looped)
: RpWidget(parent)
, _itemsCount(itemsCount)
, _itemHeight(itemHeight)
, _paintCallback(std::move(paintCallback))
, _pendingStartIndex(startIndex)
, _loopData({ .looped = looped }) {
	Expects(_paintCallback != nullptr);

	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		_itemsVisible.count = std::ceil(float64(s.height()) / _itemHeight);
		_itemsVisible.centerOffset = _itemsVisible.count / 2;
		if (_pendingStartIndex && _itemsVisible.count) {
			_index = normalizedIndex(base::take(_pendingStartIndex)
				- _itemsVisible.centerOffset);
		}

		if (!_loopData.looped) {
			_loopData.minIndex = -_itemsVisible.centerOffset;
			_loopData.maxIndex = _itemsCount - 1 - _itemsVisible.centerOffset;
		}
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(this);

		const auto outerWidth = width();
		const auto centerY = height() / 2.;
		const auto shiftedY = _itemHeight * _shift;
		for (auto i = -1; i < (_itemsVisible.count + 1); i++) {
			const auto index = normalizedIndex(i + _index);
			if (!isIndexInRange(index)) {
				continue;
			}
			const auto y = (_itemHeight * i + shiftedY);
			_paintCallback(
				p,
				index,
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
	// Guard input.
	if (by >= 1.) {
		by = .99;
	}

	auto shift = _shift;
	auto index = _index;
	shift += by;
	if (shift >= 1.) {
		shift -= 1.;
		index--;
		index = normalizedIndex(index);
	} else if (shift <= -1.) {
		shift += 1.;
		index++;
		index = normalizedIndex(index);
	}
	if (!_loopData.looped && (index <= _loopData.minIndex)) {
		_shift = std::min(0., shift);
		_index = _loopData.minIndex;
	} else if (!_loopData.looped && (index >= _loopData.maxIndex)) {
		_shift = std::max(0., shift);
		_index = _loopData.maxIndex;
	} else {
		_shift = shift;
		_index = index;
	}
	update();
}

void VerticalDrumPicker::handleWheelEvent(not_null<QWheelEvent*> e) {
	const auto direction = Ui::WheelDirection(e);
	if (direction) {
		_animation.jumpToOffset(direction);
	} else {
		if (const auto delta = e->pixelDelta().y(); delta) {
			increaseShift(delta / float64(_itemHeight));
		} else if (e->phase() == Qt::ScrollEnd) {
			animationDataFromIndex();
			_animation.jumpToOffset(0);
		} else {
			constexpr auto step = int(QWheelEvent::DefaultDeltasPerStep);

			_touch.verticalDelta += e->angleDelta().y();
			while (std::abs(_touch.verticalDelta) >= step) {
				if (_touch.verticalDelta < 0) {
					_touch.verticalDelta += step;
					_animation.jumpToOffset(1);
				} else {
					_touch.verticalDelta -= step;
					_animation.jumpToOffset(-1);
				}
			}
		}
	}
}

void VerticalDrumPicker::handleKeyEvent(not_null<QKeyEvent*> e) {
	if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Up) {
		_animation.jumpToOffset(1);
	} else if (e->key() == Qt::Key_PageUp && !e->isAutoRepeat()) {
		_animation.jumpToOffset(_itemsVisible.count);
	} else if (e->key() == Qt::Key_Right || e->key() == Qt::Key_Down) {
		_animation.jumpToOffset(-1);
	} else if (e->key() == Qt::Key_PageDown && !e->isAutoRepeat()) {
		_animation.jumpToOffset(-_itemsVisible.count);
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
			const auto toOffset = _itemsVisible.centerOffset
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

bool VerticalDrumPicker::isIndexInRange(int index) const {
	return (index >= 0) && (index < _itemsCount);
}

int VerticalDrumPicker::normalizedIndex(int index) const {
	if (!_loopData.looped) {
		return index;
	}
	if (index < 0) {
		index += _itemsCount;
	} else if (index >= _itemsCount) {
		index -= _itemsCount;
	}
	return index;
}

int VerticalDrumPicker::index() const {
	return normalizedIndex(_index + _itemsVisible.centerOffset);
}

} // namespace Ui
