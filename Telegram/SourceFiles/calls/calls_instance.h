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
#include "calls/calls_call.h"

namespace Calls {

class Panel;

class Instance : private MTP::Sender, private Call::Delegate, private base::Subscriber {
public:
	Instance();

	void startOutgoingCall(gsl::not_null<UserData*> user);

	void handleUpdate(const MTPDupdatePhoneCall &update);

	~Instance();

private:
	gsl::not_null<Call::Delegate*> getCallDelegate() {
		return static_cast<Call::Delegate*>(this);
	}
	DhConfig getDhConfig() const override {
		return _dhConfig;
	}
	void callFinished(gsl::not_null<Call*> call) override;
	void callFailed(gsl::not_null<Call*> call) override;
	void createCall(gsl::not_null<UserData*> user, Call::Type type);
	void refreshDhConfig();

	void handleCallUpdate(const MTPPhoneCall &call);

	DhConfig _dhConfig;

	std::unique_ptr<Call> _currentCall;
	std::unique_ptr<Panel> _currentCallPanel;

};

Instance &Current();

} // namespace Calls
