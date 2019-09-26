/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_app_config.h"

#include "main/main_session.h"
#include "base/call_delayed.h"
#include "apiwrap.h"

namespace Main {
namespace {

constexpr auto kRefreshTimeout = 3600 * crl::time(1000);

} // namespace

AppConfig::AppConfig(not_null<Session*> session) : _session(session) {
	refresh();
}

void AppConfig::refresh() {
	if (_requestId) {
		return;
	}
	_requestId = _session->api().request(MTPhelp_GetAppConfig(
	)).done([=](const MTPJSONValue &result) {
		_requestId = 0;
		refreshDelayed();
		if (result.type() == mtpc_jsonObject) {
			for (const auto &element : result.c_jsonObject().vvalue().v) {
				element.match([&](const MTPDjsonObjectValue &data) {
					_data.emplace_or_assign(qs(data.vkey()), data.vvalue());
				});
			}
		}
	}).fail([=](const RPCError &error) {
		_requestId = 0;
		refreshDelayed();
	}).send();
}

void AppConfig::refreshDelayed() {
	base::call_delayed(kRefreshTimeout, _session, [=] {
		refresh();
	});
}

double AppConfig::getDouble(const QString &key, double fallback) const {
	const auto i = _data.find(key);
	if (i == end(_data)) {
		return fallback;
	}
	return i->second.match([&](const MTPDjsonNumber &data) {
		return data.vvalue().v;
	}, [&](const auto &data) {
		return fallback;
	});
}

} // namespace Main
