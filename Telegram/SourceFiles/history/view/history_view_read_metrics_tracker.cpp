/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_read_metrics_tracker.h"

#include "api/api_read_metrics.h"
#include "apiwrap.h"
#include "base/random.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace HistoryView {
namespace {

constexpr auto kGracePeriod = crl::time(300);
constexpr auto kActivityTimeout = 15 * crl::time(1000);
constexpr auto kMaxTrackingDuration = 5 * 60 * crl::time(1000);
constexpr auto kMinReportThreshold = crl::time(300);

} // namespace

ReadMetricsTracker::ReadMetricsTracker(not_null<PeerData*> peer)
: _peer(peer)
, _timer([=] { onTimeout(); }) {
	Core::App().appDeactivatedValue(
	) | rpl::on_next([=](bool deactivated) {
		const auto appActive = !deactivated;
		if (_appActive == appActive) {
			return;
		}
		_appActive = appActive;
		refreshPaused(crl::now());
	}, _lifetime);
}

ReadMetricsTracker::~ReadMetricsTracker() {
	finalizeAll();
}

void ReadMetricsTracker::startBatch(int visibleTop, int visibleBottom) {
	_batchNow = crl::now();
	sync(_batchNow);
	_batchViewportHeight = visibleBottom - visibleTop;
	_batchVisibleTop = visibleTop;
	_batchVisibleBottom = visibleBottom;
	_batchVisible.clear();
}

void ReadMetricsTracker::push(
		not_null<HistoryItem*> item,
		int itemTop,
		int itemHeight) {
	if (!ShouldTrack(item)) {
		return;
	}
	const auto msgId = item->id;
	_batchVisible.emplace(msgId);

	const auto clippedTop = std::max(itemTop, _batchVisibleTop) - itemTop;
	const auto clippedBottom = std::min(
		itemTop + itemHeight,
		_batchVisibleBottom) - itemTop;

	const auto addTracked = [&] {
		auto tracked = TrackedItem();
		tracked.entryGracePending = true;
		tracked.entryGraceStart = _batchNow;
		tracked.maxItemHeight = itemHeight;
		tracked.maxViewportHeight = _batchViewportHeight;
		tracked.seenTop = clippedTop;
		tracked.seenBottom = clippedBottom;
		_tracked.emplace(msgId, tracked);
	};

	auto it = _tracked.find(msgId);
	if (it == end(_tracked)) {
		addTracked();
		return;
	}
	auto &tracked = it->second;
	if (tracked.exitGracePending) {
		if (_batchNow - tracked.exitGraceStart >= kGracePeriod) {
			finalize(msgId, tracked);
			_tracked.erase(it);
			addTracked();
			return;
		}
		tracked.exitGracePending = false;
		tracked.exitGraceStart = 0;
		tracked.lastUpdate = _batchNow;
	}
	tracked.seenTop = std::min(tracked.seenTop, clippedTop);
	tracked.seenBottom = std::max(tracked.seenBottom, clippedBottom);
	accumulate_max(tracked.maxItemHeight, itemHeight);
	accumulate_max(tracked.maxViewportHeight, _batchViewportHeight);
}

void ReadMetricsTracker::endBatch() {
	for (auto it = _tracked.begin(); it != _tracked.end();) {
		if (_batchVisible.contains(it->first)) {
			++it;
			continue;
		}
		auto &tracked = it->second;
		if (tracked.entryGracePending) {
			it = _tracked.erase(it);
			continue;
		}
		if (!tracked.exitGracePending) {
			tracked.exitGracePending = true;
			tracked.exitGraceStart = _batchNow;
		}
		++it;
	}

	_currentlyVisible = std::move(_batchVisible);
	restartTimer();
}

void ReadMetricsTracker::registerActivity() {
	if (!_appActive || !_screenActive) {
		return;
	}
	const auto now = crl::now();
	const auto activityDeadline = activeUntil();
	if (activityDeadline && activityDeadline < now) {
		sync(now);
	}
	_lastActivity = now;
	restartTimer();
}

void ReadMetricsTracker::setScreenActive(bool active) {
	if (_screenActive == active) {
		return;
	}
	_screenActive = active;
	refreshPaused(crl::now());
}

void ReadMetricsTracker::pauseTracking() {
	setScreenActive(false);
}

void ReadMetricsTracker::resumeTracking() {
	setScreenActive(true);
}

void ReadMetricsTracker::onTimeout() {
	sync(crl::now());
}

void ReadMetricsTracker::sync(crl::time now) {
	const auto activeUntil = this->activeUntil();
	processTransitions(now, activeUntil);
	accumulate(now, activeUntil);
	restartTimer();
}

void ReadMetricsTracker::processTransitions(
		crl::time now,
		crl::time activeUntil) {
	for (auto it = _tracked.begin(); it != _tracked.end();) {
		auto &tracked = it->second;
		if (tracked.entryGracePending
			&& !_paused
			&& now - tracked.entryGraceStart >= kGracePeriod) {
			tracked.entryGracePending = false;
			do {
				tracked.viewId = base::RandomValue<uint64>();
			} while (!tracked.viewId);
			tracked.trackingStarted = tracked.entryGraceStart;
			tracked.lastUpdate = tracked.trackingStarted;
		}
		if (tracked.exitGracePending
			&& !_paused
			&& now - tracked.exitGraceStart >= kGracePeriod) {
			finalize(it->first, tracked);
			it = _tracked.erase(it);
			continue;
		}
		if (tracked.viewId && !tracked.entryGracePending && !tracked.exitGracePending) {
			const auto deadline = tracked.trackingStarted + kMaxTrackingDuration;
			if (now >= deadline) {
				if (!_paused
					&& _currentlyVisible.contains(it->first)
					&& tracked.lastUpdate < deadline) {
					addElapsed(tracked, tracked.lastUpdate, deadline, activeUntil);
					tracked.lastUpdate = deadline;
				}
				finalize(it->first, tracked);
				it = _tracked.erase(it);
				continue;
			}
		}
		++it;
	}
}

void ReadMetricsTracker::accumulate(crl::time now, crl::time activeUntil) {
	for (auto &[msgId, tracked] : _tracked) {
		if (!tracked.viewId
			|| tracked.entryGracePending
			|| tracked.exitGracePending
			|| tracked.lastUpdate <= 0) {
			continue;
		}
		if (_paused || !_currentlyVisible.contains(msgId)) {
			tracked.lastUpdate = now;
			continue;
		}
		addElapsed(tracked, tracked.lastUpdate, now, activeUntil);
		tracked.lastUpdate = now;
	}
}

void ReadMetricsTracker::refreshPaused(crl::time now) {
	const auto paused = !_appActive || !_screenActive;
	if (_paused == paused) {
		return;
	} else if (paused) {
		const auto activeUntil = this->activeUntil();
		processTransitions(now, activeUntil);
		accumulate(now, activeUntil);
		_pausedSince = now;
		for (auto &[msgId, tracked] : _tracked) {
			if (tracked.entryGracePending || tracked.exitGracePending) {
				continue;
			}
			tracked.lastUpdate = now;
		}
	} else {
		const auto delta = now - _pausedSince;
		for (auto &[msgId, tracked] : _tracked) {
			if (tracked.entryGracePending) {
				tracked.entryGraceStart = (tracked.entryGraceStart < _pausedSince)
					? (tracked.entryGraceStart + delta)
					: now;
			} else if (tracked.exitGracePending) {
				tracked.exitGraceStart = (tracked.exitGraceStart < _pausedSince)
					? (tracked.exitGraceStart + delta)
					: now;
			} else if (tracked.viewId) {
				tracked.lastUpdate = now;
			}
		}
		_pausedSince = 0;
	}
	_paused = paused;
	restartTimer();
}

void ReadMetricsTracker::restartTimer() {
	if (_tracked.empty()) {
		_timer.cancel();
		return;
	}
	const auto now = crl::now();
	auto nearest = crl::time(0);
	const auto updateNearest = [&](crl::time deadline) {
		if (!deadline) {
			return;
		}
		if (!nearest || deadline < nearest) {
			nearest = deadline;
		}
	};

	const auto activityDeadline = activeUntil();
	for (const auto &[msgId, tracked] : _tracked) {
		if (tracked.entryGracePending && !_paused) {
			updateNearest(tracked.entryGraceStart + kGracePeriod);
		} else if (tracked.exitGracePending && !_paused) {
			updateNearest(tracked.exitGraceStart + kGracePeriod);
		} else if (tracked.viewId) {
			updateNearest(tracked.trackingStarted + kMaxTrackingDuration);
			if (!_paused && _currentlyVisible.contains(msgId) && activityDeadline > now) {
				updateNearest(activityDeadline);
			}
		}
	}
	if (!nearest) {
		_timer.cancel();
		return;
	}
	_timer.callOnce(std::max(nearest - now, crl::time(0)));
}

crl::time ReadMetricsTracker::activeUntil() const {
	return _lastActivity ? (_lastActivity + kActivityTimeout) : 0;
}

void ReadMetricsTracker::addElapsed(
		TrackedItem &tracked,
		crl::time from,
		crl::time till,
		crl::time activeUntil) const {
	if (till <= from) {
		return;
	}
	tracked.totalInView += (till - from);
	if (activeUntil > from) {
		tracked.activeInView += std::max(std::min(till, activeUntil) - from, crl::time(0));
		tracked.activeInView = std::min(tracked.activeInView, tracked.totalInView);
	}
}

void ReadMetricsTracker::finalize(
		MsgId msgId,
		const TrackedItem &tracked) {
	if (tracked.viewId == 0 || tracked.totalInView < kMinReportThreshold) {
		return;
	}
	const auto heightRatio = (tracked.maxViewportHeight > 0)
		? qRound((tracked.maxItemHeight * 1000.0) / tracked.maxViewportHeight)
		: 0;
	const auto seenRange = (tracked.maxItemHeight > 0)
		? std::clamp(
			qRound(((tracked.seenBottom - tracked.seenTop) * 1000.0)
				/ tracked.maxItemHeight),
			0,
			1000)
		: 0;
	_peer->session().api().readMetrics().add(_peer, {
		.msgId = msgId,
		.viewId = tracked.viewId,
		.timeInViewMs = int(tracked.totalInView),
		.activeTimeInViewMs = int(tracked.activeInView),
		.heightToViewportRatioPermille = heightRatio,
		.seenRangeRatioPermille = seenRange,
	});
}

void ReadMetricsTracker::finalizeAll() {
	sync(crl::now());
	for (const auto &[msgId, tracked] : _tracked) {
		finalize(msgId, tracked);
	}
	_tracked.clear();
	_timer.cancel();
}

bool ReadMetricsTracker::ShouldTrack(not_null<HistoryItem*> item) {
	return item->isRegular() && item->hasViews();
}

} // namespace HistoryView
