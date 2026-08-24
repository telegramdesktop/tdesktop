/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_webauthn.h"

namespace Platform::WebAuthn {

void RegisterViaCable(
	const Data::Passkey::RegisterData &data,
	Fn<void(RegisterResult)> callback);
void LoginViaCable(
	const Data::Passkey::LoginData &data,
	Fn<void(LoginResult)> callback);

void RegisterViaLibfido2(
	const Data::Passkey::RegisterData &data,
	Fn<void(RegisterResult)> callback);
void LoginViaLibfido2(
	const Data::Passkey::LoginData &data,
	Fn<void(LoginResult)> callback);

[[nodiscard]] bool Libfido2DevicePresent();

// Security key branch, per platform:
// - Linux and macOS always go through libfido2;
// - Windows uses webauthn.dll where present, and falls back to libfido2
//   on older systems that lack it (Win7/8/8.1, Win10 < 1903).
[[nodiscard]] bool SecurityKeyPresent();
void RegisterViaSecurityKey(
	const Data::Passkey::RegisterData &data,
	Fn<void(RegisterResult)> callback);
void LoginViaSecurityKey(
	const Data::Passkey::LoginData &data,
	Fn<void(LoginResult)> callback);

} // namespace Platform::WebAuthn
