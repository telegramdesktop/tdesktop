/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/subsection_tabs_slider_reorder.h"

#include "ui/controls/subsection_tabs_slider.h"
#include "ui/widgets/scroll_area.h"
#include "ui/controls/subsection_tabs_slider.h"
#include "styles/style_basic.h"

#include <QtWidgets/QApplication>

namespace Ui {

namespace {

constexpr auto kScrollFactor = 0.05;

} // namespace

SubsectionSliderReorder::SubsectionSliderReorder(
	not_null<SubsectionSlider*> slider,
	not_null<ScrollArea*> scroll)
: _slider(slider)
, _scroll(scroll)
, _scrollAnimation([=] { updateScrollCallback(); }) {
}

SubsectionSliderReorder::SubsectionSliderReorder(
	not_null<SubsectionSlider*> slider)
: _slider(slider) {
}

SubsectionSliderReorder::~SubsectionSliderReorder() {
	cancel();
}

void SubsectionSliderReorder::cancel() {
	if (_currentButton) {
		cancelCurrent(indexOf(_currentButton));
	}
	_lifetime.destroy();
	_entries.clear();
}

void SubsectionSliderReorder::start() {
	const auto count = _slider->sectionsCount();
	if (count < 2) {
		return;
	}
	for (auto i = 0; i != count; ++i) {
		const auto button = _slider->buttonAt(i);
		const auto eventsProducer = _proxyButtonCallback
			? _proxyButtonCallback(i)
			: button;
		eventsProducer->events(
		) | rpl::start_with_next_done([=](not_null<QEvent*> e) {
			switch (e->type()) {
			case QEvent::MouseMove:
				mouseMove(
					button,
					static_cast<QMouseEvent*>(e.get())->globalPos());
				break;
			case QEvent::MouseButtonPress: {
				const auto event = static_cast<QMouseEvent*>(e.get());
				mousePress(button, event->button(), event->globalPos());
				break;
			}
			case QEvent::MouseButtonRelease:
				mouseRelease(static_cast<QMouseEvent*>(e.get())->button());
				break;
			}
		}, [=] {
			cancel();
		}, _lifetime);
		_entries.push_back({ button });
	}
}

void SubsectionSliderReorder::addPinnedInterval(int from, int length) {
	_pinnedIntervals.push_back({ from, length });
}

void SubsectionSliderReorder::clearPinnedIntervals() {
	_pinnedIntervals.clear();
}

void SubsectionSliderReorder::setMouseEventProxy(ProxyCallback callback) {
	_proxyButtonCallback = std::move(callback);
}

bool SubsectionSliderReorder::Interval::isIn(int index) const {
	return (index >= from) && (index < (from + length));
}

bool SubsectionSliderReorder::isIndexPinned(int index) const {
	return ranges::any_of(_pinnedIntervals, [&](const Interval &i) {
		return i.isIn(index);
	});
}

void SubsectionSliderReorder::mouseMove(
		not_null<SubsectionButton*> button,
		QPoint position) {
	if (_currentButton != button) {
		return;
	} else if (_currentState != State::Started) {
		checkForStart(position);
	} else {
		updateOrder(indexOf(_currentButton), position);

		checkForScrollAnimation();
	}
}

void SubsectionSliderReorder::checkForStart(QPoint position) {
	const auto shift = _slider->isVertical()
		? (position.y() - _currentStart)
		: (position.x() - _currentStart);
	const auto delta = QApplication::startDragDistance();
	if (std::abs(shift) <= delta) {
		return;
	}
	_currentButton->raise();
	_currentState = State::Started;
	_currentStart += (shift > 0) ? delta : -delta;

	const auto index = indexOf(_currentButton);
	_currentDesiredIndex = index;
	_updates.fire({ _currentButton, index, index, _currentState });

	updateOrder(index, position);
}

void SubsectionSliderReorder::updateOrder(int index, QPoint position) {
	if (isIndexPinned(index)) {
		return;
	}
	const auto shift = _slider->isVertical()
		? (position.y() - _currentStart)
		: (position.x() - _currentStart);
	auto &current = _entries[index];
	current.shiftAnimation.stop();
	current.shift = current.finalShift = shift;
	_slider->setButtonShift(index, shift);

	const auto count = _entries.size();
	const auto currentSize = _slider->isVertical()
		? _currentButton->height()
		: _currentButton->width();
	const auto currentMiddle = _slider->isVertical()
		? (_currentButton->y() + currentSize / 2)
		: (_currentButton->x() + currentSize / 2);
	_currentDesiredIndex = index;
	if (shift > 0) {
		auto top = _slider->isVertical()
			? (_currentButton->y() - shift)
			: (_currentButton->x() - shift);
		for (auto next = index + 1; next != count; ++next) {
			if (isIndexPinned(next)) {
				return;
			}
			const auto &entry = _entries[next];
			top += _slider->isVertical()
				? entry.button->height()
				: entry.button->width();
			if (currentMiddle < top) {
				moveToShift(next, 0);
			} else {
				_currentDesiredIndex = next;
				moveToShift(next, -currentSize);
			}
		}
		for (auto prev = index - 1; prev >= 0; --prev) {
			moveToShift(prev, 0);
		}
	} else {
		for (auto next = index + 1; next != count; ++next) {
			moveToShift(next, 0);
		}
		for (auto prev = index - 1; prev >= 0; --prev) {
			if (isIndexPinned(prev)) {
				return;
			}
			const auto &entry = _entries[prev];
			const auto entryPos = _slider->isVertical()
				? (entry.button->y() - entry.shift)
				: (entry.button->x() - entry.shift);
			if (currentMiddle >= entryPos + currentSize) {
				moveToShift(prev, 0);
			} else {
				_currentDesiredIndex = prev;
				moveToShift(prev, currentSize);
			}
		}
	}
}

void SubsectionSliderReorder::mousePress(
		not_null<SubsectionButton*> button,
		Qt::MouseButton btn,
		QPoint position) {
	if (btn != Qt::LeftButton) {
		return;
	}
	cancelCurrent();
	_currentButton = button;
	_currentStart = _slider->isVertical() ? position.y() : position.x();
}

void SubsectionSliderReorder::mouseRelease(Qt::MouseButton button) {
	if (button != Qt::LeftButton) {
		return;
	}
	finishReordering();
}

void SubsectionSliderReorder::cancelCurrent() {
	if (_currentButton) {
		cancelCurrent(indexOf(_currentButton));
	}
}

void SubsectionSliderReorder::cancelCurrent(int index) {
	Expects(_currentButton != nullptr);

	if (_currentState == State::Started) {
		_currentState = State::Cancelled;
		_updates.fire({ _currentButton, index, index, _currentState });
	}
	_currentButton = nullptr;
	for (auto i = 0, count = int(_entries.size()); i != count; ++i) {
		moveToShift(i, 0);
	}
}

void SubsectionSliderReorder::finishReordering() {
	if (_scroll) {
		_scrollAnimation.stop();
	}
	finishCurrent();
}

void SubsectionSliderReorder::finishCurrent() {
	if (!_currentButton) {
		return;
	}
	const auto index = indexOf(_currentButton);
	if (_currentDesiredIndex == index || _currentState != State::Started) {
		cancelCurrent(index);
		return;
	}
	const auto result = _currentDesiredIndex;
	const auto button = _currentButton;
	_currentState = State::Cancelled;
	_currentButton = nullptr;

	auto &current = _entries[index];
	const auto size = _slider->isVertical()
		? button->height()
		: button->width();
	if (index < result) {
		auto sum = 0;
		for (auto i = index; i != result; ++i) {
			auto &entry = _entries[i + 1];
			const auto btn = entry.button;
			entry.deltaShift += size;
			updateShift(btn, i + 1);
			sum += _slider->isVertical() ? btn->height() : btn->width();
		}
		current.finalShift -= sum;
	} else if (index > result) {
		auto sum = 0;
		for (auto i = result; i != index; ++i) {
			auto &entry = _entries[i];
			const auto btn = entry.button;
			entry.deltaShift -= size;
			updateShift(btn, i);
			sum += _slider->isVertical() ? btn->height() : btn->width();
		}
		current.finalShift += sum;
	}
	if (!(current.finalShift + current.deltaShift)) {
		current.shift = 0;
		_slider->setButtonShift(index, 0);
	}
	base::reorder(_entries, index, result);
	_slider->reorderButtons(index, result);
	for (auto i = 0, count = int(_entries.size()); i != count; ++i) {
		moveToShift(i, 0);
	}

	_updates.fire({ button, index, result, State::Applied });
}

void SubsectionSliderReorder::moveToShift(int index, int shift) {
	auto &entry = _entries[index];
	if (entry.finalShift + entry.deltaShift == shift) {
		return;
	}
	const auto button = entry.button;
	entry.shiftAnimation.start(
		[=] { updateShift(button, index); },
		entry.finalShift,
		shift - entry.deltaShift,
		st::slideWrapDuration);
	entry.finalShift = shift - entry.deltaShift;
}

void SubsectionSliderReorder::updateShift(
		not_null<SubsectionButton*> button,
		int indexHint) {
	Expects(indexHint >= 0 && indexHint < _entries.size());

	const auto index = (_entries[indexHint].button == button)
		? indexHint
		: indexOf(button);
	auto &entry = _entries[index];
	entry.shift = base::SafeRound(
		entry.shiftAnimation.value(entry.finalShift)
	) + entry.deltaShift;
	if (entry.deltaShift && !entry.shiftAnimation.animating()) {
		entry.finalShift += entry.deltaShift;
		entry.deltaShift = 0;
	}
	_slider->setButtonShift(index, entry.shift);
}

auto SubsectionSliderReorder::updates() const
-> rpl::producer<SubsectionSliderReorder::Single> {
	return _updates.events();
}

int SubsectionSliderReorder::indexOf(
		not_null<SubsectionButton*> button) const {
	const auto i = ranges::find(_entries, button, &Entry::button);
	Assert(i != end(_entries));
	return i - begin(_entries);
}

void SubsectionSliderReorder::updateScrollCallback() {
	if (!_scroll) {
		return;
	}
	const auto delta = deltaFromEdge();
	if (_slider->isVertical()) {
		const auto oldTop = _scroll->scrollTop();
		_scroll->scrollToY(oldTop + delta);
		const auto newTop = _scroll->scrollTop();
		_currentStart += oldTop - newTop;
		if (newTop == 0 || newTop == _scroll->scrollTopMax()) {
			_scrollAnimation.stop();
		}
	} else {
		const auto oldLeft = _scroll->scrollLeft();
		_scroll->scrollToX(oldLeft + delta);
		const auto newLeft = _scroll->scrollLeft();
		_currentStart += oldLeft - newLeft;
		if (newLeft == 0 || newLeft == _scroll->scrollLeftMax()) {
			_scrollAnimation.stop();
		}
	}
}

void SubsectionSliderReorder::checkForScrollAnimation() {
	if (!_scroll || !deltaFromEdge() || _scrollAnimation.animating()) {
		return;
	}
	_scrollAnimation.start();
}

int SubsectionSliderReorder::deltaFromEdge() {
	Expects(_currentButton != nullptr);
	Expects(_scroll);

	const auto globalPosition = _currentButton->mapToGlobal(QPoint(0, 0));
	const auto localPos = _scroll->mapFromGlobal(globalPosition);
	const auto localTop = _slider->isVertical() ? localPos.y() : localPos.x();
	const auto buttonSize = _slider->isVertical()
		? _currentButton->height()
		: _currentButton->width();
	const auto scrollSize = _slider->isVertical()
		? _scroll->height()
		: _scroll->width();
	const auto localBottom = localTop + buttonSize - scrollSize;

	const auto isTopEdge = (localTop < 0);
	const auto isBottomEdge = (localBottom > 0);
	if (!isTopEdge && !isBottomEdge) {
		_scrollAnimation.stop();
		return 0;
	}
	return int((isBottomEdge ? localBottom : localTop) * kScrollFactor);
}

} // namespace Ui