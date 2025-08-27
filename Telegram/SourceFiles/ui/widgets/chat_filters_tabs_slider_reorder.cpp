/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/chat_filters_tabs_slider_reorder.h"

#include "ui/widgets/scroll_area.h"
#include "styles/style_basic.h"

#include <QScrollBar>
#include <QtGui/QtEvents>
#include <QtWidgets/QApplication>

namespace Ui {
namespace {

constexpr auto kScrollFactor = 0.05;

} // namespace

ChatsFiltersTabsReorder::ChatsFiltersTabsReorder(
	not_null<ChatsFiltersTabs*> layout,
	not_null<ScrollArea*> scroll)
: _layout(layout)
, _scroll(scroll)
, _scrollAnimation([this] { updateScrollCallback(); }) {
}

ChatsFiltersTabsReorder::ChatsFiltersTabsReorder(
	not_null<ChatsFiltersTabs*> layout)
: _layout(layout) {
}

void ChatsFiltersTabsReorder::cancel() {
	if (_currentWidget) {
		cancelCurrent(indexOf(_currentWidget));
	}
	_lifetime.destroy();
	for (auto i = 0, count = _layout->count(); i != count; ++i) {
		_layout->setHorizontalShift(i, 0);
	}
	_entries.clear();
}

void ChatsFiltersTabsReorder::start() {
	const auto count = _layout->count();
	if (count < 2) {
		return;
	}
	_layout->events()
	| rpl::start_with_next_done([this](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::MouseMove:
			mouseMove(static_cast<QMouseEvent*>(e.get())->globalPos());
			break;
		case QEvent::MouseButtonPress: {
			const auto m = static_cast<QMouseEvent*>(e.get());
			mousePress(m->button(), m->pos(), m->globalPos());
			break;
		}
		case QEvent::MouseButtonRelease:
			mouseRelease(static_cast<QMouseEvent*>(e.get())->button());
			break;
		}
	}, [this] {
		cancel();
	}, _lifetime);

	for (auto i = 0; i != count; ++i) {
		const auto widget = _layout->widgetAt(i);
		_entries.push_back({ widget });
	}
}

void ChatsFiltersTabsReorder::addPinnedInterval(int from, int length) {
	_pinnedIntervals.push_back({ from, length });
}

void ChatsFiltersTabsReorder::clearPinnedIntervals() {
	_pinnedIntervals.clear();
}

bool ChatsFiltersTabsReorder::Interval::isIn(int index) const {
	return (index >= from) && (index < (from + length));
}

bool ChatsFiltersTabsReorder::isIndexPinned(int index) const {
	return ranges::any_of(_pinnedIntervals, [&](const Interval &i) {
		return i.isIn(index);
	});
}

void ChatsFiltersTabsReorder::checkForStart(QPoint position) {
	const auto shift = position.x() - _currentStart;
	const auto delta = QApplication::startDragDistance();
	if (std::abs(shift) <= delta) {
		return;
	}
	_currentState = State::Started;
	_currentStart += (shift > 0) ? delta : -delta;

	const auto index = indexOf(_currentWidget);
	_layout->setRaised(index);
	_currentDesiredIndex = index;
	_updates.fire({ _currentWidget, index, index, _currentState });

	updateOrder(index, position);
}

void ChatsFiltersTabsReorder::updateOrder(int index, QPoint position) {
	if (isIndexPinned(index)) {
		return;
	}
	const auto shift = position.x() - _currentStart;
	auto &current = _entries[index];
	current.shiftAnimation.stop();
	current.shift = current.finalShift = shift;
	_layout->setHorizontalShift(index, shift);

	checkForScrollAnimation();

	const auto count = _entries.size();
	const auto currentWidth = current.widget->width;
	const auto currentMiddle = current.widget->left
		+ shift
		+ currentWidth / 2;
	_currentDesiredIndex = index;
	if (shift > 0) {
		for (auto next = index + 1; next != count; ++next) {
			if (isIndexPinned(next)) {
				return;
			}
			const auto &e = _entries[next];
			if (currentMiddle < e.widget->left + e.widget->width / 2) {
				moveToShift(next, 0);
			} else {
				_currentDesiredIndex = next;
				moveToShift(next, -currentWidth);
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
			const auto &e = _entries[prev];
			if (currentMiddle >= e.widget->left + e.widget->width / 2) {
				moveToShift(prev, 0);
			} else {
				_currentDesiredIndex = prev;
				moveToShift(prev, currentWidth);
			}
		}
	}
}

void ChatsFiltersTabsReorder::mousePress(
		Qt::MouseButton button,
		QPoint position,
		QPoint globalPosition) {
	if (button != Qt::LeftButton) {
		return;
	}
	auto widget = (ChatsFiltersTabs::ShiftedSection*)(nullptr);
	for (auto i = 0; i != _layout->_sections.size(); ++i) {
		auto &section = _layout->_sections[i];
		if ((position.x() >= section.section->left)
			&& (position.x() < (section.section->left + section.section->width))) {
			widget = &section;
			break;
		}
	}
	cancelCurrent();
	if (!widget) {
		return;
	}
	_currentWidget = widget->section;
	_currentShiftedWidget = widget;
	_currentStart = globalPosition.x();
}

void ChatsFiltersTabsReorder::mouseMove(QPoint position) {
	if (!_currentWidget) {
	// if (_currentWidget != widget) {
		return;
	} else if (_currentState != State::Started) {
		checkForStart(position);
	} else {
		updateOrder(indexOf(_currentWidget), position);
	}
}

void ChatsFiltersTabsReorder::mouseRelease(Qt::MouseButton button) {
	if (button != Qt::LeftButton) {
		return;
	}
	finishReordering();
}

void ChatsFiltersTabsReorder::cancelCurrent() {
	if (_currentWidget) {
		cancelCurrent(indexOf(_currentWidget));
	}
}

void ChatsFiltersTabsReorder::cancelCurrent(int index) {
	Expects(_currentWidget != nullptr);

	if (_currentState == State::Started) {
		_currentState = State::Cancelled;
		_updates.fire({ _currentWidget, index, index, _currentState });
	}
	_currentWidget = nullptr;
	_currentShiftedWidget = nullptr;
	for (auto i = 0, count = int(_entries.size()); i != count; ++i) {
		moveToShift(i, 0);
	}
}

void ChatsFiltersTabsReorder::finishReordering() {
	if (_scroll) {
		_scrollAnimation.stop();
	}
	finishCurrent();
}

void ChatsFiltersTabsReorder::finishCurrent() {
	if (!_currentWidget) {
		return;
	}
	const auto index = indexOf(_currentWidget);
	if (_currentDesiredIndex == index || _currentState != State::Started) {
		cancelCurrent(index);
		return;
	}
	const auto result = _currentDesiredIndex;
	const auto widget = _currentWidget;
	_currentState = State::Cancelled;
	_currentWidget = nullptr;
	_currentShiftedWidget = nullptr;

	auto &current = _entries[index];
	const auto width = current.widget->width;
	if (index < result) {
		auto sum = 0;
		for (auto i = index; i != result; ++i) {
			auto &entry = _entries[i + 1];
			const auto widget = entry.widget;
			entry.deltaShift += width;
			updateShift(widget, i + 1);
			sum += widget->width;
		}
		current.finalShift -= sum;
	} else if (index > result) {
		auto sum = 0;
		for (auto i = result; i != index; ++i) {
			auto &entry = _entries[i];
			const auto widget = entry.widget;
			entry.deltaShift -= width;
			updateShift(widget, i);
			sum += widget->width;
		}
		current.finalShift += sum;
	}
	if (!(current.finalShift + current.deltaShift)) {
		current.shift = 0;
		_layout->setHorizontalShift(index, 0);
	}
	base::reorder(_entries, index, result);
	_layout->reorderSections(index, _currentDesiredIndex);
	for (auto i = 0; i != _layout->sectionsRef().size(); ++i) {
		_entries[i].widget = &_layout->sectionsRef()[i];
		moveToShift(i, 0);
	}

	_updates.fire({ widget, index, result, State::Applied });
}

void ChatsFiltersTabsReorder::moveToShift(int index, int shift) {
	auto &entry = _entries[index];
	if (entry.finalShift + entry.deltaShift == shift) {
		return;
	}
	const auto widget = entry.widget;
	entry.shiftAnimation.start(
		[=, this] { updateShift(widget, index); },
		entry.finalShift,
		shift - entry.deltaShift,
		st::slideWrapDuration);
	entry.finalShift = shift - entry.deltaShift;
}

void ChatsFiltersTabsReorder::updateShift(
		not_null<Section*> widget,
		int indexHint) {
	Expects(indexHint >= 0 && indexHint < _entries.size());

	const auto index = (_entries[indexHint].widget == widget)
		? indexHint
		: indexOf(widget);
	auto &entry = _entries[index];
	entry.shift = base::SafeRound(
		entry.shiftAnimation.value(entry.finalShift)
	) + entry.deltaShift;
	if (entry.deltaShift && !entry.shiftAnimation.animating()) {
		entry.finalShift += entry.deltaShift;
		entry.deltaShift = 0;
	}
	_layout->setHorizontalShift(index, entry.shift);
}

int ChatsFiltersTabsReorder::indexOf(not_null<Section*> widget) const {
	const auto i = ranges::find(_entries, widget, &Entry::widget);
	Assert(i != end(_entries));
	return i - begin(_entries);
}

auto ChatsFiltersTabsReorder::updates() const -> rpl::producer<Single> {
	return _updates.events();
}

void ChatsFiltersTabsReorder::updateScrollCallback() {
	if (!_scroll) {
		return;
	}
	const auto delta = deltaFromEdge();
	const auto oldLeft = _scroll->scrollLeft();
	_scroll->horizontalScrollBar()->setValue(oldLeft + delta);
	const auto newLeft = _scroll->scrollLeft();

	_currentStart += oldLeft - newLeft;
	if (newLeft == 0 || newLeft == _scroll->scrollLeftMax()) {
		_scrollAnimation.stop();
	}
}

void ChatsFiltersTabsReorder::checkForScrollAnimation() {
	if (!_scroll || !deltaFromEdge() || _scrollAnimation.animating()) {
		return;
	}
	_scrollAnimation.start();
}

int ChatsFiltersTabsReorder::deltaFromEdge() {
	Expects(_currentWidget != nullptr);
	Expects(_currentShiftedWidget != nullptr);
	Expects(_scroll);

	const auto globalPosition = _layout->mapToGlobal(
		QPoint(
			_currentWidget->left + _currentShiftedWidget->horizontalShift,
			0));
	const auto localLeft = _scroll->mapFromGlobal(globalPosition).x();
	const auto localRight = localLeft
		+ _currentWidget->width
		- _scroll->width();

	const auto isLeftEdge = (localLeft < 0);
	const auto isRightEdge = (localRight > 0);
	if (!isLeftEdge && !isRightEdge) {
		_scrollAnimation.stop();
		return 0;
	}
	return int((isRightEdge ? localRight : localLeft) * kScrollFactor);
}

} // namespace Ui
