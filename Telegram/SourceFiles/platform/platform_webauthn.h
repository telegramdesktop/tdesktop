/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#pragma once

namespace Data::Passkey {
struct RegisterData;
struct LoginData;
} // namespace Data::Passkey

namespace Platform::WebAuthn {

enum class Error {
	None,
	Cancelled,
	UnsignedBuild,
	Other,
};

struct LoginResult {
	QByteArray clientDataJSON;
	QByteArray credentialId;
	QByteArray authenticatorData;
	QByteArray signature;
	QByteArray userHandle;
	Error error = Error::None;
};

struct RegisterResult {
	QByteArray credentialId;
	QByteArray attestationObject;
	QByteArray clientDataJSON;
	bool success = false;
	Error error = Error::None;
};

[[nodiscard]] bool IsSupported();
void RegisterKey(
	const Data::Passkey::RegisterData &data,
	Fn<void(RegisterResult result)> callback);
void Login(
	const Data::Passkey::LoginData &data,
	Fn<void(LoginResult result)> callback);

} // namespace Platform::WebAuthn
