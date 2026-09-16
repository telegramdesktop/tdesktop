/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace Editor {

class SegmentPlayer;
class TrimTimeline;

class TimelineSeeker final {
public:
	TimelineSeeker(
		not_null<TrimTimeline*> timeline,
		not_null<SegmentPlayer*> player);
	~TimelineSeeker();

private:
	void seek(crl::time position);
	void finishDragging();

	const not_null<TrimTimeline*> _timeline;
	const not_null<SegmentPlayer*> _player;

	base::Timer _timer;
	crl::time _pending = -1;
	bool _dragging = false;

	rpl::lifetime _lifetime;

};

} // namespace Editor
