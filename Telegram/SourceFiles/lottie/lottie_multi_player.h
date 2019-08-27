/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "lottie/lottie_player.h"
#include "base/timer.h"
#include "base/algorithm.h"
#include "base/flat_set.h"
#include "base/flat_map.h"

#include <rpl/event_stream.h>

namespace Lottie {

class Animation;
class FrameRenderer;

struct MultiUpdate {
	//base::variant<
	//	std::pair<Animation*, Information>,
	//	DisplayMultiFrameRequest,
	//	std::pair<Animation*, Error>> data;
};

class MultiPlayer final : public Player {
public:
	MultiPlayer(
		Quality quality = Quality::Default,
		std::shared_ptr<FrameRenderer> renderer = nullptr);
	~MultiPlayer();

	void start(
		not_null<Animation*> animation,
		std::unique_ptr<SharedState> state) override;
	void failed(not_null<Animation*> animation, Error error) override;
	void updateFrameRequest(
		not_null<const Animation*> animation,
		const FrameRequest &request) override;
	bool markFrameShown() override;
	void checkStep() override;

	not_null<Animation*> append(
		FnMut<void(FnMut<void(QByteArray &&cached)>)> get, // Main thread.
		FnMut<void(QByteArray &&cached)> put, // Unknown thread.
		const QByteArray &content,
		const FrameRequest &request);
	not_null<Animation*> append(
		const QByteArray &content,
		const FrameRequest &request);

	rpl::producer<MultiUpdate> updates() const;

	void remove(not_null<Animation*> animation);

	void pause(not_null<Animation*> animation);
	void unpause(not_null<Animation*> animation);

private:
	struct PausedInfo {
		not_null<SharedState*> state;
		crl::time pauseTime = kTimeUnknown;
		crl::time pauseDelay = kTimeUnknown;
	};
	struct StartingInfo {
		std::unique_ptr<SharedState> state;
		bool paused = false;
	};

	void addNewToActive(
		not_null<Animation*> animation,
		StartingInfo &&info);
	[[nodiscard]] int countFrameIndex(
		not_null<SharedState*> state,
		crl::time time,
		crl::time delay) const;
	void startAtRightTime(std::unique_ptr<SharedState> state);
	void processPending();
	void markFrameDisplayed(crl::time now);
	void addTimelineDelay(crl::time delayed);
	void checkNextFrameAvailability();
	void checkNextFrameRender();
	void unpauseFirst(
		not_null<Animation*> animation,
		not_null<SharedState*> state);
	void pauseAndSaveState(not_null<Animation*> animation);
	void unpauseAndKeepUp(not_null<Animation*> animation);
	void removeNow(not_null<Animation*> animation);

	Quality _quality = Quality::Default;
	base::Timer _timer;
	const std::shared_ptr<FrameRenderer> _renderer;
	std::vector<std::unique_ptr<Animation>> _animations;
	base::flat_map<not_null<Animation*>, not_null<SharedState*>> _active;
	base::flat_map<not_null<Animation*>, PausedInfo> _paused;
	base::flat_set<not_null<Animation*>> _pendingPause;
	base::flat_set<not_null<Animation*>> _pendingUnpause;
	base::flat_set<not_null<Animation*>> _pausedBeforeStart;
	base::flat_set<not_null<Animation*>> _pendingRemove;
	base::flat_map<not_null<Animation*>, StartingInfo> _pendingToStart;
	crl::time _started = kTimeUnknown;
	crl::time _lastSyncTime = kTimeUnknown;
	crl::time _delay = 0;
	crl::time _nextFrameTime = kTimeUnknown;
	rpl::event_stream<MultiUpdate> _updates;
	rpl::lifetime _lifetime;

};

} // namespace Lottie
