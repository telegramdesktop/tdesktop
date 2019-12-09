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
	void update(int days);

	rpl::producer<int> days() const;

private:
	MTP::Sender _api;
	mtpRequestId _requestId = 0;
	rpl::variable<int> _days = 0;

};

} // namespace Api
