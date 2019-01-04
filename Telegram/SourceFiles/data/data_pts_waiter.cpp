/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_pts_waiter.h"

#include "mainwidget.h"
#include "auth_session.h"
#include "apiwrap.h"

uint64 PtsWaiter::ptsKey(PtsSkippedQueue queue, int32 pts) {
	return _queue.insert(uint64(uint32(pts)) << 32 | (++_skippedKey), queue).key();
}

void PtsWaiter::setWaitingForSkipped(ChannelData *channel, int32 ms) {
	if (ms >= 0) {
		if (App::main()) {
			App::main()->ptsWaiterStartTimerFor(channel, ms);
		}
		_waitingForSkipped = true;
	} else {
		_waitingForSkipped = false;
		checkForWaiting(channel);
	}
}

void PtsWaiter::setWaitingForShortPoll(ChannelData *channel, int32 ms) {
	if (ms >= 0) {
		if (App::main()) {
			App::main()->ptsWaiterStartTimerFor(channel, ms);
		}
		_waitingForShortPoll = true;
	} else {
		_waitingForShortPoll = false;
		checkForWaiting(channel);
	}
}

void PtsWaiter::checkForWaiting(ChannelData *channel) {
	if (!_waitingForSkipped && !_waitingForShortPoll && App::main()) {
		App::main()->ptsWaiterStartTimerFor(channel, -1);
	}
}

void PtsWaiter::applySkippedUpdates(ChannelData *channel) {
	if (!_waitingForSkipped) return;

	setWaitingForSkipped(channel, -1);

	if (_queue.isEmpty()) return;

	++_applySkippedLevel;
	for (auto i = _queue.cbegin(), e = _queue.cend(); i != e; ++i) {
		switch (i.value()) {
		case SkippedUpdate: Auth().api().applyUpdateNoPtsCheck(_updateQueue.value(i.key())); break;
		case SkippedUpdates: Auth().api().applyUpdatesNoPtsCheck(_updatesQueue.value(i.key())); break;
		}
	}
	--_applySkippedLevel;
	clearSkippedUpdates();
}

void PtsWaiter::clearSkippedUpdates() {
	_queue.clear();
	_updateQueue.clear();
	_updatesQueue.clear();
	_applySkippedLevel = 0;
}

bool PtsWaiter::updated(ChannelData *channel, int32 pts, int32 count, const MTPUpdates &updates) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	} else if (check(channel, pts, count)) {
		return true;
	}
	_updatesQueue.insert(ptsKey(SkippedUpdates, pts), updates);
	return false;
}

bool PtsWaiter::updated(ChannelData *channel, int32 pts, int32 count, const MTPUpdate &update) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	} else if (check(channel, pts, count)) {
		return true;
	}
	_updateQueue.insert(ptsKey(SkippedUpdate, pts), update);
	return false;
}

bool PtsWaiter::updated(ChannelData *channel, int32 pts, int32 count) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	}
	return check(channel, pts, count);
}

bool PtsWaiter::updateAndApply(ChannelData *channel, int32 pts, int32 count, const MTPUpdates &updates) {
	if (!updated(channel, pts, count, updates)) {
		return false;
	}
	if (!_waitingForSkipped || _queue.isEmpty()) {
		// Optimization - no need to put in queue and back.
		Auth().api().applyUpdatesNoPtsCheck(updates);
	} else {
		_updatesQueue.insert(ptsKey(SkippedUpdates, pts), updates);
		applySkippedUpdates(channel);
	}
	return true;
}

bool PtsWaiter::updateAndApply(ChannelData *channel, int32 pts, int32 count, const MTPUpdate &update) {
	if (!updated(channel, pts, count, update)) {
		return false;
	}
	if (!_waitingForSkipped || _queue.isEmpty()) {
		// Optimization - no need to put in queue and back.
		Auth().api().applyUpdateNoPtsCheck(update);
	} else {
		_updateQueue.insert(ptsKey(SkippedUpdate, pts), update);
		applySkippedUpdates(channel);
	}
	return true;
}

bool PtsWaiter::updateAndApply(ChannelData *channel, int32 pts, int32 count) {
	if (!updated(channel, pts, count)) {
		return false;
	}
	applySkippedUpdates(channel);
	return true;
}

bool PtsWaiter::check(ChannelData *channel, int32 pts, int32 count) { // return false if need to save that update and apply later
	if (!inited()) {
		init(pts);
		return true;
	}

	_last = qMax(_last, pts);
	_count += count;
	if (_last == _count) {
		_good = _last;
		return true;
	} else if (_last < _count) {
		setWaitingForSkipped(channel, 1);
	} else {
		setWaitingForSkipped(channel, WaitForSkippedTimeout);
	}
	return !count;
}
