/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_webview_proxy.h"

#include "core/application.h"
#include "core/core_settings.h"

#include <rpl/filter.h>
#include <rpl/map.h>

namespace Core {

std::optional<Webview::ProxySettings> CurrentWebviewProxy() {
	const auto &proxy = App().settings().proxy();
	if (!proxy.isEnabled()) {
		return std::nullopt;
	}
	const auto data = proxy.selected();
	if (data.type != MTP::ProxyData::Type::Socks5) {
		return std::nullopt;
	}
	return Webview::ProxySettings{
		.type = Webview::ProxyType::SOCKS5,
		.host = data.host.toStdString(),
		.port = std::to_string(data.port),
		.username = data.user.toStdString(),
		.password = data.password.toStdString(),
	};
}

rpl::producer<> WebviewProxyChangesFrom(
		std::optional<Webview::ProxySettings> was) {
	return App().settings().proxy().connectionTypeChanges(
	) | rpl::map([=] {
		return CurrentWebviewProxy();
	}) | rpl::filter([=](
			const std::optional<Webview::ProxySettings> &now) {
		return now != was;
	}) | rpl::to_empty;
}

} // namespace Core
