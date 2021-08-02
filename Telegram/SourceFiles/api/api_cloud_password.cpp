/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_cloud_password.h"

#include "base/openssl_help.h"
#include "core/core_cloud_password.h"
#include "apiwrap.h"

namespace Api {

// #TODO Add ability to set recovery email separately.

CloudPassword::CloudPassword(not_null<ApiWrap*> api)
: _api(&api->instance()) {
}

void CloudPassword::reload() {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPaccount_GetPassword(
	)).done([=](const MTPaccount_Password &result) {
		_requestId = 0;
		result.match([&](const MTPDaccount_password &data) {
			openssl::AddRandomSeed(bytes::make_span(data.vsecure_random().v));
			if (_state) {
				*_state = Core::ParseCloudPasswordState(data);
			} else {
				_state = std::make_unique<Core::CloudPasswordState>(
					Core::ParseCloudPasswordState(data));
			}
			_stateChanges.fire_copy(*_state);
		});
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
	}).send();
}

void CloudPassword::applyPendingReset(
		const MTPaccount_ResetPasswordResult &data) {
	if (!_state) {
		reload();
		return;
	}
	data.match([&](const MTPDaccount_resetPasswordOk &data) {
		reload();
	}, [&](const MTPDaccount_resetPasswordRequestedWait &data) {
		const auto until = data.vuntil_date().v;
		if (_state->pendingResetDate != until) {
			_state->pendingResetDate = until;
			_stateChanges.fire_copy(*_state);
		}
	}, [&](const MTPDaccount_resetPasswordFailedWait &data) {
	});
}

void CloudPassword::clearUnconfirmedPassword() {
	_requestId = _api.request(MTPaccount_CancelPasswordEmail(
	)).done([=](const MTPBool &result) {
		_requestId = 0;
		reload();
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		reload();
	}).send();
}

rpl::producer<Core::CloudPasswordState> CloudPassword::state() const {
	return _state
		? _stateChanges.events_starting_with_copy(*_state)
		: (_stateChanges.events() | rpl::type_erased());
}

auto CloudPassword::stateCurrent() const
-> std::optional<Core::CloudPasswordState> {
	return _state
		? base::make_optional(*_state)
		: std::nullopt;
}

} // namespace Api
