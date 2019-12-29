/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "base/timer.h"

class ApiWrap;

namespace Main {
class Session;
} // namespace Main

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
	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	mtpRequestId _requestId = 0;
	rpl::variable<bool> _enabled = false;
	rpl::variable<bool> _canChange = false;
	base::Timer _appConfigReloadTimer;

};

} // namespace Api
