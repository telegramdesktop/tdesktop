/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "webauthn/cable_scanner.h"

namespace Platform::WebAuthn::Cable {

std::unique_ptr<BleScanner> MakeBleScanner() {
	return nullptr;
}

} // namespace Platform::WebAuthn::Cable
