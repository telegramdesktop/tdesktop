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

#include <rpl/event_stream.h>

namespace Lottie {

class FrameRenderer;

struct DisplayFrameRequest {
};

struct Update {
	base::variant<
		Information,
		DisplayFrameRequest> data;
};

class SinglePlayer final : public Player {
public:
	SinglePlayer(
		const QByteArray &content,
		const FrameRequest &request,
		Quality quality = Quality::Default,
		const ColorReplacements *replacements = nullptr,
		std::shared_ptr<FrameRenderer> renderer = nullptr);
	SinglePlayer(
		FnMut<void(FnMut<void(QByteArray &&cached)>)> get, // Main thread.
		FnMut<void(QByteArray &&cached)> put, // Unknown thread.
		const QByteArray &content,
		const FrameRequest &request,
		Quality quality = Quality::Default,
		const ColorReplacements *replacements = nullptr,
		std::shared_ptr<FrameRenderer> renderer = nullptr);
	~SinglePlayer();

	void start(
		not_null<Animation*> animation,
		std::unique_ptr<SharedState> state) override;
	void failed(not_null<Animation*> animation, Error error) override;
	void updateFrameRequest(
		not_null<const Animation*> animation,
		const FrameRequest &request) override;
	bool markFrameShown() override;
	void checkStep() override;

	rpl::producer<Update, Error> updates() const;

	[[nodiscard]] bool ready() const;
	[[nodiscard]] QImage frame() const;
	[[nodiscard]] QImage frame(const FrameRequest &request) const;
	[[nodiscard]] Animation::FrameInfo frameInfo(
		const FrameRequest &request) const;

private:
	void checkNextFrameAvailability();
	void checkNextFrameRender();

	Animation _animation;
	base::Timer _timer;
	const std::shared_ptr<FrameRenderer> _renderer;
	SharedState *_state = nullptr;
	crl::time _nextFrameTime = kTimeUnknown;
	rpl::event_stream<Update, Error> _updates;
	rpl::lifetime _lifetime;

};

} // namespace Lottie
