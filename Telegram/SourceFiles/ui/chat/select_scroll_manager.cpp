/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/select_scroll_manager.h"

#include "ui/widgets/scroll_area.h"

namespace Ui {

SelectScrollManager::SelectScrollManager()
: _timer([=] { scrollByTimer(); }) {
}

void SelectScrollManager::scrollByTimer() {
	const auto d = (_delta > 0)
		? std::min(_delta * 3 / 20 + 1, kMaxScrollSpeed)
		: std::max(_delta * 3 / 20 - 1, -kMaxScrollSpeed);
	_scrolls.fire_copy(d);
}

void SelectScrollManager::checkDeltaScroll(
		const QPoint &point,
		int top,
		int bottom) {
	const auto diff = point.y() - top;
	_delta = (diff < 0)
		? diff
		: (point.y() >= bottom)
		? (point.y() - bottom + 1)
		: 0;
	if (_delta) {
		_timer.callEach(15);
	} else {
		_timer.cancel();
	}
}

void SelectScrollManager::cancel() {
	_timer.cancel();
}

rpl::producer<int> SelectScrollManager::scrolls() {
	return _scrolls.events();
}

} // namespace Ui
