/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "lottie/lottie_player.h"
#include "lottie/lottie_animation.h"
#include "base/timer.h"

namespace Lottie {

class SinglePlayer final : public Player {
public:
	SinglePlayer(
		const QByteArray &content,
		const FrameRequest &request);
	SinglePlayer(
		FnMut<void(FnMut<void(QByteArray &&cached)>)> get, // Main thread.
		FnMut<void(QByteArray &&cached)> put, // Unknown thread.
		const QByteArray &content,
		const FrameRequest &request);
	~SinglePlayer();

	void start(
		not_null<Animation*> animation,
		std::unique_ptr<SharedState> state) override;
	void failed(not_null<Animation*> animation, Error error) override;

	rpl::producer<Update, Error> updates() override;

	[[nodiscard]] bool ready() const;
	[[nodiscard]] QImage frame(const FrameRequest &request) const;

	void updateFrameRequest(
		not_null<const Animation*> animation,
		const FrameRequest &request) override;

	// Returns frame position, if any frame was marked as displayed.
	crl::time markFrameDisplayed(crl::time now) override;
	crl::time markFrameShown() override;

	void checkStep() override;

private:
	void checkNextFrameAvailability();
	void checkNextFrameRender();

	Animation _animation;
	base::Timer _timer;
	std::shared_ptr<FrameRenderer> _renderer;
	SharedState *_state = nullptr;
	crl::time _nextFrameTime = kTimeUnknown;
	rpl::event_stream<Update, Error> _updates;
	rpl::lifetime _lifetime;

};

} // namespace Lottie
