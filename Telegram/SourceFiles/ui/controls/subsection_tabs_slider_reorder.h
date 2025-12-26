/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

namespace Ui {

class RpWidget;
class ScrollArea;
class SubsectionButton;
class SubsectionSlider;

class SubsectionSliderReorder final {
public:
	using ProxyCallback = Fn<not_null<Ui::RpWidget*>(int)>;
	enum class State : uchar {
		Started,
		Applied,
		Cancelled,
	};
	struct Single {
		not_null<SubsectionButton*> widget;
		int oldPosition = 0;
		int newPosition = 0;
		State state = State::Started;
	};

	SubsectionSliderReorder(
		not_null<SubsectionSlider*> slider,
		not_null<ScrollArea*> scroll);
	SubsectionSliderReorder(not_null<SubsectionSlider*> slider);
	~SubsectionSliderReorder();

	void start();
	void cancel();
	void finishReordering();
	void addPinnedInterval(int from, int length);
	void clearPinnedIntervals();
	void setMouseEventProxy(ProxyCallback callback);
	[[nodiscard]] rpl::producer<Single> updates() const;

private:
	struct Entry {
		not_null<SubsectionButton*> button;
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

	void mouseMove(not_null<SubsectionButton*> button, QPoint position);
	void mousePress(
		not_null<SubsectionButton*> button,
		Qt::MouseButton mouseButton,
		QPoint position);
	void mouseRelease(Qt::MouseButton button);

	void checkForStart(QPoint position);
	void updateOrder(int index, QPoint position);
	void cancelCurrent();
	void finishCurrent();
	void cancelCurrent(int index);

	[[nodiscard]] int indexOf(not_null<SubsectionButton*> button) const;
	void moveToShift(int index, int shift);
	void updateShift(not_null<SubsectionButton*> button, int indexHint);

	void updateScrollCallback();
	void checkForScrollAnimation();
	[[nodiscard]] int deltaFromEdge();

	[[nodiscard]] bool isIndexPinned(int index) const;

	const not_null<Ui::SubsectionSlider*> _slider;
	Ui::ScrollArea *_scroll = nullptr;

	Ui::Animations::Basic _scrollAnimation;

	std::vector<Interval> _pinnedIntervals;

	ProxyCallback _proxyButtonCallback = nullptr;

	SubsectionButton *_currentButton = nullptr;
	int _currentStart = 0;
	int _currentDesiredIndex = 0;
	State _currentState = State::Cancelled;
	std::vector<Entry> _entries;
	rpl::event_stream<Single> _updates;
	rpl::lifetime _lifetime;
};

} // namespace Ui
