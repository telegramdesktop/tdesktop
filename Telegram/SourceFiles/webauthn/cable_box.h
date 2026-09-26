/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <rpl/event_stream.h>
#include <rpl/variable.h>

#include <memory>

namespace Platform::WebAuthn::Cable {

enum class Sheet {
	Qr,
	Connecting,
	Continue,
	Error,
};

struct BoxState {
	rpl::variable<Sheet> sheet = Sheet::Qr;
	rpl::variable<QString> error;
	rpl::event_stream<> closeRequests;
};

struct BoxContent {
	std::shared_ptr<BoxState> state;
	QString qrText;
	bool isRegister = false;
	bool bluetoothAvailable = false;
	Fn<void()> securityKeyChosen;
	Fn<void()> cancelled;
};

void ShowCableToast(const QString &text);
bool ShowCableBox(BoxContent &&content);

} // namespace Platform::WebAuthn::Cable
