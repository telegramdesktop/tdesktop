/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_sensitive_content.h"

#include "apiwrap.h"

namespace Api {

SensitiveContent::SensitiveContent(not_null<ApiWrap*> api)
: _api(api->instance()) {
}

void SensitiveContent::reload() {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPaccount_GetContentSettings(
	)).done([=](const MTPaccount_ContentSettings &result) {
		_requestId = 0;
		result.match([&](const MTPDaccount_contentSettings &data) {
			_enabled = data.is_sensitive_enabled();
			_canChange = data.is_sensitive_can_change();
		});
	}).fail([=](const RPCError &error) {
		_requestId = 0;
	}).send();
}

bool SensitiveContent::enabledCurrent() const {
	return _enabled.current();
}

rpl::producer<bool> SensitiveContent::enabled() const {
	return _enabled.value();
}

rpl::producer<bool> SensitiveContent::canChange() const {
	return _canChange.value();
}

void SensitiveContent::update(bool enabled) {
	if (!_canChange.current()) {
		return;
	}
	using Flag = MTPaccount_SetContentSettings::Flag;
	_api.request(_requestId).cancel();
	_requestId = _api.request(MTPaccount_SetContentSettings(
		MTP_flags(enabled ? Flag::f_sensitive_enabled : Flag(0))
	)).done([=](const MTPBool &result) {
		_requestId = 0;
	}).fail([=](const RPCError &error) {
		_requestId = 0;
	}).send();
	_enabled = enabled;
}

} // namespace Api
