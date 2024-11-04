/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/widgets/chat_filters_tabs_slider.h"

namespace Ui {

class ScrollArea;

class ChatsFiltersTabsReorder final {
public:
	using Section = ChatsFiltersTabs::Section;
	enum class State : uchar {
		Started,
		Applied,
		Cancelled,
	};

	struct Single {
		not_null<Section*> widget;
		int oldPosition = 0;
		int newPosition = 0;
		State state = State::Started;
	};

	ChatsFiltersTabsReorder(
		not_null<ChatsFiltersTabs*> layout,
		not_null<ScrollArea*> scroll);
	ChatsFiltersTabsReorder(not_null<ChatsFiltersTabs*> layout);

	void start();
	void cancel();
	void finishReordering();
	void addPinnedInterval(int from, int length);
	void clearPinnedIntervals();
	[[nodiscard]] rpl::producer<Single> updates() const;

private:
	struct Entry {
		not_null<Section*> widget;
		Ui::Animations::Simple shiftAnimation;
		int shift = 0;
		int finalShift = 0;
		int deltaShift = 0;
	};
	struct Interval {
		[[nodiscard]] bool isIn(int index) const;

		int from = 0;
		int length = 0;
	};

	void mousePress(Qt::MouseButton button, QPoint position, QPoint global);
	void mouseMove(QPoint position);
	void mouseRelease(Qt::MouseButton button);

	void checkForStart(QPoint position);
	void updateOrder(int index, QPoint position);
	void cancelCurrent();
	void finishCurrent();
	void cancelCurrent(int index);

	[[nodiscard]] int indexOf(not_null<Section*> widget) const;
	void moveToShift(int index, int shift);
	void updateShift(not_null<Section*> widget, int indexHint);

	void updateScrollCallback();
	void checkForScrollAnimation();
	[[nodiscard]] int deltaFromEdge();

	[[nodiscard]] bool isIndexPinned(int index) const;

	const not_null<ChatsFiltersTabs*> _layout;
	Ui::ScrollArea *_scroll = nullptr;

	Ui::Animations::Basic _scrollAnimation;

	std::vector<Interval> _pinnedIntervals;

	Section *_currentWidget = nullptr;
	ChatsFiltersTabs::ShiftedSection *_currentShiftedWidget = nullptr;
	int _currentStart = 0;
	int _currentDesiredIndex = 0;
	State _currentState = State::Cancelled;
	std::vector<Entry> _entries;
	rpl::event_stream<Single> _updates;
	rpl::lifetime _lifetime;

};

} // namespace Ui
