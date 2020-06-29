/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_pts_waiter.h"

#include "api/api_updates.h"

PtsWaiter::PtsWaiter(not_null<Api::Updates*> owner) : _owner(owner) {
}

uint64 PtsWaiter::ptsKey(PtsSkippedQueue queue, int32 pts) {
	return _queue.emplace(
		uint64(uint32(pts)) << 32 | (++_skippedKey),
		queue
	).first->first;
}

void PtsWaiter::setWaitingForSkipped(ChannelData *channel, crl::time ms) {
	if (ms >= 0) {
		_owner->ptsWaiterStartTimerFor(channel, ms);
		_waitingForSkipped = true;
	} else {
		_waitingForSkipped = false;
		checkForWaiting(channel);
	}
}

void PtsWaiter::setWaitingForShortPoll(ChannelData *channel, crl::time ms) {
	if (ms >= 0) {
		_owner->ptsWaiterStartTimerFor(channel, ms);
		_waitingForShortPoll = true;
	} else {
		_waitingForShortPoll = false;
		checkForWaiting(channel);
	}
}

void PtsWaiter::checkForWaiting(ChannelData *channel) {
	if (!_waitingForSkipped && !_waitingForShortPoll) {
		_owner->ptsWaiterStartTimerFor(channel, -1);
	}
}

void PtsWaiter::applySkippedUpdates(ChannelData *channel) {
	if (!_waitingForSkipped) {
		return;
	}

	setWaitingForSkipped(channel, -1);

	if (_queue.empty()) {
		return;
	}

	++_applySkippedLevel;
	for (auto i = _queue.cbegin(), e = _queue.cend(); i != e; ++i) {
		switch (i->second) {
		case SkippedUpdate: {
			_owner->applyUpdateNoPtsCheck(_updateQueue[i->first]);
		} break;
		case SkippedUpdates: {
			_owner->applyUpdatesNoPtsCheck(_updatesQueue[i->first]);
		} break;
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

bool PtsWaiter::updated(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdates &updates) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	} else if (check(channel, pts, count)) {
		return true;
	}
	_updatesQueue.emplace(ptsKey(SkippedUpdates, pts), updates);
	return false;
}

bool PtsWaiter::updated(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdate &update) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	} else if (check(channel, pts, count)) {
		return true;
	}
	_updateQueue.emplace(ptsKey(SkippedUpdate, pts), update);
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

bool PtsWaiter::updateAndApply(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdates &updates) {
	if (!updated(channel, pts, count, updates)) {
		return false;
	}
	if (!_waitingForSkipped || _queue.empty()) {
		// Optimization - no need to put in queue and back.
		_owner->applyUpdatesNoPtsCheck(updates);
	} else {
		_updatesQueue.emplace(ptsKey(SkippedUpdates, pts), updates);
		applySkippedUpdates(channel);
	}
	return true;
}

bool PtsWaiter::updateAndApply(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdate &update) {
	if (!updated(channel, pts, count, update)) {
		return false;
	}
	if (!_waitingForSkipped || _queue.empty()) {
		// Optimization - no need to put in queue and back.
		_owner->applyUpdateNoPtsCheck(update);
	} else {
		_updateQueue.emplace(ptsKey(SkippedUpdate, pts), update);
		applySkippedUpdates(channel);
	}
	return true;
}

bool PtsWaiter::updateAndApply(
		ChannelData *channel,
		int32 pts,
		int32 count) {
	if (!updated(channel, pts, count)) {
		return false;
	}
	applySkippedUpdates(channel);
	return true;
}

// Return false if need to save that update and apply later.
bool PtsWaiter::check(ChannelData *channel, int32 pts, int32 count) {
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
		setWaitingForSkipped(channel, kWaitForSkippedTimeout);
	}
	return !count;
}
