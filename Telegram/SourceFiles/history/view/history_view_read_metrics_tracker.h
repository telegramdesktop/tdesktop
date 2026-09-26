/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class PeerData;
class HistoryItem;

namespace HistoryView {

class ReadMetricsTracker final {
public:
	explicit ReadMetricsTracker(not_null<PeerData*> peer);
	~ReadMetricsTracker();

	void startBatch(int visibleTop, int visibleBottom);
	void push(not_null<HistoryItem*> item, int itemTop, int itemHeight);
	void endBatch();
	void registerActivity();
	void setScreenActive(bool active);
	void pauseTracking();
	void resumeTracking();

private:
	struct TrackedItem {
		uint64 viewId = 0;
		crl::time entryGraceStart = 0;
		crl::time trackingStarted = 0;
		crl::time lastUpdate = 0;
		crl::time totalInView = 0;
		crl::time activeInView = 0;
		int seenTop = std::numeric_limits<int>::max();
		int seenBottom = 0;
		int maxItemHeight = 0;
		int maxViewportHeight = 0;
		bool entryGracePending = false;
		bool exitGracePending = false;
		crl::time exitGraceStart = 0;
	};

	void onTimeout();
	void sync(crl::time now);
	void processTransitions(crl::time now, crl::time activeUntil);
	void accumulate(crl::time now, crl::time activeUntil);
	void refreshPaused(crl::time now);
	void restartTimer();
	[[nodiscard]] crl::time activeUntil() const;
	void addElapsed(
		TrackedItem &tracked,
		crl::time from,
		crl::time till,
		crl::time activeUntil) const;
	void finalize(MsgId msgId, const TrackedItem &tracked);
	void finalizeAll();
	[[nodiscard]] static bool ShouldTrack(not_null<HistoryItem*> item);

	const not_null<PeerData*> _peer;
	base::flat_map<MsgId, TrackedItem> _tracked;
	base::flat_set<MsgId> _currentlyVisible;
	crl::time _batchNow = 0;
	int _batchViewportHeight = 0;
	int _batchVisibleTop = 0;
	int _batchVisibleBottom = 0;
	base::flat_set<MsgId> _batchVisible;
	crl::time _lastActivity = 0;
	bool _appActive = true;
	bool _screenActive = true;
	bool _paused = false;
	crl::time _pausedSince = 0;
	base::Timer _timer;
	rpl::lifetime _lifetime;

};

} // namespace HistoryView
