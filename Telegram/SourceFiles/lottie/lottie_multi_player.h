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

std::shared_ptr<FrameRenderer> MakeFrameRenderer();

class MultiPlayer final : public Player {
public:
	explicit MultiPlayer(std::shared_ptr<FrameRenderer> renderer = nullptr);
	~MultiPlayer();

	void start(
		not_null<Animation*> animation,
		std::unique_ptr<SharedState> state) override;
	void failed(not_null<Animation*> animation, Error error) override;
	void updateFrameRequest(
		not_null<const Animation*> animation,
		const FrameRequest &request) override;
	void markFrameShown() override;
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

private:
	crl::time startAtRightTime(not_null<SharedState*> state);
	void markFrameDisplayed(crl::time now, crl::time delayed);
	void checkNextFrameAvailability();
	void checkNextFrameRender();

	base::Timer _timer;
	const std::shared_ptr<FrameRenderer> _renderer;
	std::vector<std::unique_ptr<Animation>> _animations;
	base::flat_map<not_null<Animation*>, not_null<SharedState*>> _active;
	//base::flat_map<not_null<Animation*>, not_null<SharedState*>> _paused;
	crl::time _started = kTimeUnknown;
	crl::time _accumulatedDelay = 0;
	crl::time _nextFrameTime = kTimeUnknown;
	rpl::event_stream<MultiUpdate> _updates;
	rpl::lifetime _lifetime;

};

} // namespace Lottie
