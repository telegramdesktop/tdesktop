/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_webauthn.h"

#include "webauthn/webauthn_common.h"

namespace Platform::WebAuthn {

bool IsSupported() {
	return true;
}

void RegisterKey(
		const Data::Passkey::RegisterData &data,
		Fn<void(RegisterResult result)> callback) {
	RegisterViaCable(data, std::move(callback));
}

void Login(
		const Data::Passkey::LoginData &data,
		Fn<void(LoginResult result)> callback) {
	LoginViaCable(data, std::move(callback));
}

bool SecurityKeyPresent() {
	return Libfido2DevicePresent();
}

void RegisterViaSecurityKey(
		const Data::Passkey::RegisterData &data,
		Fn<void(RegisterResult)> callback) {
	RegisterViaLibfido2(data, std::move(callback));
}

void LoginViaSecurityKey(
		const Data::Passkey::LoginData &data,
		Fn<void(LoginResult)> callback) {
	LoginViaLibfido2(data, std::move(callback));
}

} // namespace Platform::WebAuthn
