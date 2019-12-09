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

class SensitiveContent final {
public:
	explicit SensitiveContent(not_null<ApiWrap*> api);

	void reload();
	void update(bool enabled);

	[[nodiscard]] bool enabledCurrent() const;
	[[nodiscard]] rpl::producer<bool> enabled() const;
	[[nodiscard]] rpl::producer<bool> canChange() const;

private:
	MTP::Sender _api;
	mtpRequestId _requestId = 0;
	rpl::variable<bool> _enabled = false;
	rpl::variable<bool> _canChange = false;

};

} // namespace Api
