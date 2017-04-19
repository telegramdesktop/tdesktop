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
#include "calls/calls_instance.h"

#include "mtproto/connection.h"
#include "auth_session.h"
#include "calls/calls_call.h"

namespace Calls {

Instance::Instance() = default;

void Instance::startOutgoingCall(gsl::not_null<UserData*> user) {
	if (_currentCall) {
		return; // Already in a call.
	}

	_currentCall = std::make_unique<Call>(getCallDelegate(), user);
	request(MTPmessages_GetDhConfig(MTP_int(_dhConfig.version), MTP_int(Call::kSaltSize))).done([this, call = base::weak_unique_ptr<Call>(_currentCall)](const MTPmessages_DhConfig &result) {
		if (!call) {
			DEBUG_LOG(("API Warning: call was destroyed before got dhConfig."));
			return;
		}

		auto random = base::const_byte_span();
		switch (result.type()) {
		case mtpc_messages_dhConfig: {
			auto &config = result.c_messages_dhConfig();
			if (!MTP::IsPrimeAndGood(config.vp.v, config.vg.v)) {
				LOG(("API Error: bad p/g received in dhConfig."));
				callFailed(call.get());
				return;
			}
			_dhConfig.g = config.vg.v;
			_dhConfig.p = byteVectorFromMTP(config.vp);
			random = bytesFromMTP(config.vrandom);
		} break;

		case mtpc_messages_dhConfigNotModified: {
			auto &config = result.c_messages_dhConfigNotModified();
			random = bytesFromMTP(config.vrandom);
			if (!_dhConfig.g || _dhConfig.p.empty()) {
				LOG(("API Error: dhConfigNotModified on zero version."));
				callFailed(call.get());
				return;
			}
		} break;

		default: Unexpected("Type in messages.getDhConfig");
		}

		if (random.size() != Call::kSaltSize) {
			LOG(("API Error: dhConfig random bytes wrong size: %1").arg(random.size()));
			callFailed(call.get());
			return;
		}
		call->startOutgoing(random);
	}).fail([this, call = base::weak_unique_ptr<Call>(_currentCall)](const RPCError &error) {
		if (!call) {
			DEBUG_LOG(("API Warning: call was destroyed before got dhConfig."));
			return;
		}

		callFailed(call.get());
	}).send();
}

void Instance::callFinished(gsl::not_null<Call*> call, const MTPPhoneCallDiscardReason &reason) {
	if (_currentCall.get() == call) {
		_currentCall.reset();
	}
}

void Instance::callFailed(gsl::not_null<Call*> call) {
	if (_currentCall.get() == call) {
		_currentCall.reset();
	}
}

void Instance::handleUpdate(const MTPDupdatePhoneCall& update) {
	handleCallUpdate(update.vphone_call);
}

void Instance::handleCallUpdate(const MTPPhoneCall &call) {
	if (call.type() == mtpc_phoneCallRequested) {
		if (_currentCall) {
			// discard ?
		} else {
			// show call
		}
	} else if (!_currentCall || !_currentCall->handleUpdate(call)) {
		DEBUG_LOG(("API Warning: unexpected phone call update %1").arg(call.type()));
	}
}

Instance::~Instance() = default;

Instance &Current() {
	return AuthSession::Current().calls();
}

} // namespace Calls
