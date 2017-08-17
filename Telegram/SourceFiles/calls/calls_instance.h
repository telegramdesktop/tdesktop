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

namespace Media {
namespace Audio {
class Track;
} // namespace Audio
} // namespace Media

namespace Calls {

class Panel;

class Instance : private MTP::Sender, private Call::Delegate, private base::Subscriber {
public:
	Instance();

	void startOutgoingCall(not_null<UserData*> user);
	void handleUpdate(const MTPDupdatePhoneCall &update);
	void showInfoPanel(not_null<Call*> call);

	base::Observable<Call*> &currentCallChanged() {
		return _currentCallChanged;
	}

	base::Observable<FullMsgId> &newServiceMessage() {
		return _newServiceMessage;
	}

	bool isQuitPrevent();

	~Instance();

private:
	not_null<Call::Delegate*> getCallDelegate() {
		return static_cast<Call::Delegate*>(this);
	}
	DhConfig getDhConfig() const override {
		return _dhConfig;
	}
	void callFinished(not_null<Call*> call) override;
	void callFailed(not_null<Call*> call) override;
	void callRedial(not_null<Call*> call) override;
	using Sound = Call::Delegate::Sound;
	void playSound(Sound sound) override;
	void createCall(not_null<UserData*> user, Call::Type type);
	void destroyCall(not_null<Call*> call);
	void destroyCurrentPanel();

	void refreshDhConfig();
	void refreshServerConfig();

	bool alreadyInCall();
	void handleCallUpdate(const MTPPhoneCall &call);

	DhConfig _dhConfig;

	TimeMs _lastServerConfigUpdateTime = 0;
	mtpRequestId _serverConfigRequestId = 0;

	std::unique_ptr<Call> _currentCall;
	std::unique_ptr<Panel> _currentCallPanel;
	base::Observable<Call*> _currentCallChanged;
	base::Observable<FullMsgId> _newServiceMessage;
	std::vector<QPointer<Panel>> _pendingPanels;

	std::unique_ptr<Media::Audio::Track> _callConnectingTrack;
	std::unique_ptr<Media::Audio::Track> _callEndedTrack;
	std::unique_ptr<Media::Audio::Track> _callBusyTrack;

};

Instance &Current();

} // namespace Calls
