/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#pragma once

#include "base/basic_types.h"
#include "webauthn/cable_core.h"

#include <string>
#include <vector>

namespace Platform::WebAuthn::Cable {

enum class Outcome {
	Success,
	Cancelled,
	SecurityKey,
	NoBluetooth,
	Failed,
};

struct RegisterRequest {
	std::string rpId;
	std::string rpName;
	Bytes userId;
	std::string userName;
	std::string userDisplayName;
	Bytes clientDataHash;
	std::vector<int> algorithms;
	int timeoutMs = 90000;
};

struct RegisterResult {
	Bytes credentialId;
	Bytes authData;
	Outcome outcome = Outcome::Failed;
};

struct LoginRequest {
	std::string rpId;
	Bytes clientDataHash;
	std::vector<Bytes> allowCredentialIds;
	int timeoutMs = 90000;
};

struct LoginResult {
	Bytes credentialId;
	Bytes authData;
	Bytes signature;
	Bytes userHandle;
	Outcome outcome = Outcome::Failed;
};

void Register(RegisterRequest request, Fn<void(RegisterResult)> done);
void Login(LoginRequest request, Fn<void(LoginResult)> done);

} // namespace Platform::WebAuthn::Cable
