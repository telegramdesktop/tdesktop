/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "lottie/lottie_common.h"
#include "base/weak_ptr.h"

#include <rpl/producer.h>

namespace Lottie {

class SharedState;

class Player : public base::has_weak_ptr {
public:
	virtual void start(
		not_null<Animation*> animation,
		std::unique_ptr<SharedState> state) = 0;
	virtual void failed(not_null<Animation*> animation, Error error) = 0;

	[[nodiscard]] virtual rpl::producer<Update, Error> updates() = 0;

	virtual void updateFrameRequest(
		not_null<const Animation*> animation,
		const FrameRequest &request) = 0;

	// Returns frame position, if any frame was marked as displayed.
	virtual crl::time markFrameDisplayed(crl::time now) = 0;
	virtual crl::time markFrameShown() = 0;

	virtual void checkStep() = 0;

	virtual ~Player() = default;

};

} // namespace Lottie
