/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class UserData;

namespace Window {
class SessionController;
} // namespace Window

class GiftPremiumValidator final {
public:
	GiftPremiumValidator(not_null<Window::SessionController*> controller);

	void showBox(not_null<UserData*> user);
	void cancel();

private:
	const not_null<Window::SessionController*> _controller;
	MTP::Sender _api;

	mtpRequestId _requestId = 0;

};
