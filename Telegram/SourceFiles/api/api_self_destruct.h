/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class ApiWrap;

namespace Api {

class SelfDestruct final {
public:
	explicit SelfDestruct(not_null<ApiWrap*> api);

	void reload();
	void updateAccountTTL(int days);
	void updateDefaultHistoryTTL(TimeId period);

	[[nodiscard]] rpl::producer<int> daysAccountTTL() const;
	[[nodiscard]] rpl::producer<TimeId> periodDefaultHistoryTTL() const;
	[[nodiscard]] TimeId periodDefaultHistoryTTLCurrent() const;

private:
	MTP::Sender _api;
	struct {
		mtpRequestId requestId = 0;
		rpl::variable<int> days = 0;
	} _accountTTL;

	struct {
		mtpRequestId requestId = 0;
		rpl::variable<TimeId> period = 0;
	} _defaultHistoryTTL;

};

} // namespace Api
