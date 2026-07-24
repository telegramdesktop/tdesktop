/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

#include <crl/crl_time.h>

class DocumentData;

namespace Media::Player {

struct TrackState;

class MusicListenTracker final {
public:
	MusicListenTracker();

	void update(const TrackState &state);
	void finalize();

private:
	void report();
	void pauseTimedOut();

	DocumentData *_document = nullptr;
	FullMsgId _contextId;
	crl::time _listenedMs = 0;
	crl::time _playStartedAt = 0;
	bool _playing = false;
	base::Timer _pauseTimer;

};

} // namespace Media::Player
