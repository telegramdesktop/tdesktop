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

#include "base/weak_unique_ptr.h"
#include "mtproto/sender.h"

namespace tgvoip {
class VoIPController;
} // namespace tgvoip

namespace Calls {

struct DhConfig {
	int32 version = 0;
	int32 g = 0;
	std::vector<gsl::byte> p;
};

class Call : public base::enable_weak_from_this, private MTP::Sender {
public:
	class Delegate {
	public:
		virtual DhConfig getDhConfig() const = 0;
		virtual void callFinished(gsl::not_null<Call*> call, const MTPPhoneCallDiscardReason &reason) = 0;
		virtual void callFailed(gsl::not_null<Call*> call) = 0;

	};

	static constexpr auto kSaltSize = 256;

	Call(gsl::not_null<Delegate*> instance, gsl::not_null<UserData*> user);

	void startOutgoing(base::const_byte_span random);
	bool handleUpdate(const MTPPhoneCall &call);

	~Call();

private:
	static constexpr auto kAuthKeySize = 256;

	void generateSalt(base::const_byte_span random);
	void handleControllerStateChange(tgvoip::VoIPController *controller, int state);
	void createAndStartController(const MTPDphoneCall &call);
	void destroyController();

	template <typename Type>
	bool checkCallCommonFields(const Type &call);
	bool checkCallFields(const MTPDphoneCall &call);
	bool checkCallFields(const MTPDphoneCallAccepted &call);

	void confirmAcceptedCall(const MTPDphoneCallAccepted &call);

	void failed();

	DhConfig _dhConfig;
	gsl::not_null<Delegate*> _delegate;
	gsl::not_null<UserData*> _user;
	std::vector<gsl::byte> _g_a;
	std::array<gsl::byte, kSaltSize> _salt;
	std::array<gsl::byte, kAuthKeySize> _authKey;
	uint64 _id = 0;
	uint64 _accessHash = 0;
	uint64 _keyFingerprint = 0;

	std::unique_ptr<tgvoip::VoIPController> _controller;

};

} // namespace Calls
