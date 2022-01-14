/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace Ui {

class ScrollArea;

class SelectScrollManager final {
public:
	SelectScrollManager();

	void checkDeltaScroll(const QPoint &point, int top, int bottom);
	void cancel();

	rpl::producer<int> scrolls();

private:
	void scrollByTimer();

	base::Timer _timer;
	int _delta = 0;
	rpl::event_stream<int> _scrolls;

};

} // namespace Ui
