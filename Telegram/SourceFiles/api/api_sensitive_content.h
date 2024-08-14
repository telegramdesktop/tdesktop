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

	void preload();
	void reload(bool force = false);
	void update(bool enabled);

	[[nodiscard]] bool enabledCurrent() const;
	[[nodiscard]] rpl::producer<bool> enabled() const;
	[[nodiscard]] rpl::producer<bool> canChange() const;

private:
	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	mtpRequestId _loadRequestId = 0;
	mtpRequestId _saveRequestId = 0;
	rpl::variable<bool> _enabled = false;
	rpl::variable<bool> _canChange = false;
	base::Timer _appConfigReloadTimer;
	bool _appConfigReloadForce = false;
	bool _loadPending = false;
	bool _loaded = false;

};

} // namespace Api
