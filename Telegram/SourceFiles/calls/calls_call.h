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
#include "base/timer.h"
#include "mtproto/sender.h"
#include "mtproto/auth_key.h"

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
		virtual void callFinished(gsl::not_null<Call*> call) = 0;
		virtual void callFailed(gsl::not_null<Call*> call) = 0;

	};

	static constexpr auto kRandomPowerSize = 256;
	static constexpr auto kSha256Size = 32;

	enum class Type {
		Incoming,
		Outgoing,
	};
	Call(gsl::not_null<Delegate*> delegate, gsl::not_null<UserData*> user, Type type);

	Type type() const {
		return _type;
	}
	gsl::not_null<UserData*> user() const {
		return _user;
	}

	void start(base::const_byte_span random);
	bool handleUpdate(const MTPPhoneCall &call);

	enum State {
		Starting,
		WaitingInit,
		WaitingInitAck,
		Established,
		Failed,
		HangingUp,
		Ended,
		ExchangingKeys,
		Waiting,
		Requesting,
		WaitingIncoming,
		Ringing,
		Busy,
	};
	State state() const {
		return _state;
	}
	base::Observable<State> &stateChanged() {
		return _stateChanged;
	}

	void setMute(bool mute);
	bool isMute() const {
		return _mute;
	}
	base::Observable<bool> &muteChanged() {
		return _muteChanged;
	}

	TimeMs getDurationMs() const;

	void answer();
	void hangup();

	bool isKeyShaForFingerprintReady() const;
	std::array<gsl::byte, kSha256Size> getKeyShaForFingerprint() const;

	~Call();

private:
	void finish(const MTPPhoneCallDiscardReason &reason);
	void startOutgoing();
	void startIncoming();

	void generateModExpFirst(base::const_byte_span randomSeed);
	void handleControllerStateChange(tgvoip::VoIPController *controller, int state);
	void createAndStartController(const MTPDphoneCall &call);

	template <typename T>
	bool checkCallCommonFields(const T &call);
	bool checkCallFields(const MTPDphoneCall &call);
	bool checkCallFields(const MTPDphoneCallAccepted &call);

	void confirmAcceptedCall(const MTPDphoneCallAccepted &call);
	void startConfirmedCall(const MTPDphoneCall &call);
	void setState(State state);
	void setStateQueued(State state);

	gsl::not_null<Delegate*> _delegate;
	gsl::not_null<UserData*> _user;
	Type _type = Type::Outgoing;
	State _state = State::Starting;
	bool _finishAfterRequestingCall = false;
	base::Observable<State> _stateChanged;
	TimeMs _startTime = 0;
	base::DelayedCallTimer _hangupByTimeoutTimer;
	bool _mute = false;
	base::Observable<bool> _muteChanged;

	DhConfig _dhConfig;
	std::vector<gsl::byte> _ga;
	std::vector<gsl::byte> _gb;
	std::array<gsl::byte, kSha256Size> _gaHash;
	std::array<gsl::byte, kRandomPowerSize> _randomPower;
	MTP::AuthKey::Data _authKey;
	MTPPhoneCallProtocol _protocol;

	uint64 _id = 0;
	uint64 _accessHash = 0;
	uint64 _keyFingerprint = 0;

	std::unique_ptr<tgvoip::VoIPController> _controller;

};

} // namespace Calls
