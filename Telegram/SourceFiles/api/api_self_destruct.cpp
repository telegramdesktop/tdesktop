/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_self_destruct.h"

#include "apiwrap.h"

namespace Api {

SelfDestruct::SelfDestruct(not_null<ApiWrap*> api)
: _api(&api->instance()) {
}

void SelfDestruct::reload() {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPaccount_GetAccountTTL(
	)).done([=](const MTPAccountDaysTTL &result) {
		_requestId = 0;
		result.match([&](const MTPDaccountDaysTTL &data) {
			_days = data.vdays().v;
		});
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
	}).send();
}

rpl::producer<int> SelfDestruct::days() const {
	using namespace rpl::mappers;

	return _days.value() | rpl::filter(_1 != 0);
}

void SelfDestruct::update(int days) {
	_api.request(_requestId).cancel();
	_requestId = _api.request(MTPaccount_SetAccountTTL(
		MTP_accountDaysTTL(MTP_int(days))
	)).done([=](const MTPBool &result) {
		_requestId = 0;
	}).fail([=](const MTP::Error &result) {
		_requestId = 0;
	}).send();
	_days = days;
}

} // namespace Api
