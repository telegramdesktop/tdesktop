/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "mtproto/sender.h"
#include "base/weak_unique_ptr.h"

class AuthSession;

namespace tgvoip {
class VoIPController;
} // namespace tgvoip

namespace Calls {

class Instance : public base::enable_weak_from_this, private MTP::Sender {
public:
	Instance();

	void startOutgoingCall(gsl::not_null<UserData*> user);

	void handleUpdate(const MTPDupdatePhoneCall &update);

	~Instance();

private:
	void initiateActualCall();
	void callFailed();

	static constexpr auto kSaltSize = 256;
	static constexpr auto kAuthKeySize = 256;

	int32 _dhConfigVersion = 0;
	int32 _dhConfigG = 0;
	QByteArray _dhConfigP;

	QByteArray _g_a;
	std::array<unsigned char, kSaltSize> _salt;
	std::array<unsigned char, kAuthKeySize> _authKey;
	uint64 _callId = 0;
	uint64 _accessHash = 0;
	uint64 _keyFingerprint = 0;

	std::unique_ptr<tgvoip::VoIPController> _controller;

};

Instance &Current();

} // namespace Calls
