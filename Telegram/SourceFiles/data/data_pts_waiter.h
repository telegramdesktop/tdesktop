/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

enum PtsSkippedQueue {
	SkippedUpdate,
	SkippedUpdates,
};

class PtsWaiter {
public:
	PtsWaiter() = default;
	void init(int32 pts) {
		_good = _last = _count = pts;
		clearSkippedUpdates();
	}
	bool inited() const {
		return _good > 0;
	}
	void setRequesting(bool isRequesting) {
		_requesting = isRequesting;
		if (_requesting) {
			clearSkippedUpdates();
		}
	}
	bool requesting() const {
		return _requesting;
	}
	bool waitingForSkipped() const {
		return _waitingForSkipped;
	}
	bool waitingForShortPoll() const {
		return _waitingForShortPoll;
	}
	void setWaitingForSkipped(ChannelData *channel, int32 ms); // < 0 - not waiting
	void setWaitingForShortPoll(ChannelData *channel, int32 ms); // < 0 - not waiting
	int32 current() const{
		return _good;
	}
	bool updated(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdates &updates);
	bool updated(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdate &update);
	bool updated(
		ChannelData *channel,
		int32 pts,
		int32 count);
	bool updateAndApply(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdates &updates);
	bool updateAndApply(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdate &update);
	bool updateAndApply(
		ChannelData *channel,
		int32 pts,
		int32 count);
	void applySkippedUpdates(ChannelData *channel);
	void clearSkippedUpdates();

private:
	// Return false if need to save that update and apply later.
	bool check(ChannelData *channel, int32 pts, int32 count);

	uint64 ptsKey(PtsSkippedQueue queue, int32 pts);
	void checkForWaiting(ChannelData *channel);

	QMap<uint64, PtsSkippedQueue> _queue;
	QMap<uint64, MTPUpdate> _updateQueue;
	QMap<uint64, MTPUpdates> _updatesQueue;
	int32 _good = 0;
	int32 _last = 0;
	int32 _count = 0;
	int32 _applySkippedLevel = 0;
	bool _requesting = false;
	bool _waitingForSkipped = false;
	bool _waitingForShortPoll = false;
	uint32 _skippedKey = 0;

};
