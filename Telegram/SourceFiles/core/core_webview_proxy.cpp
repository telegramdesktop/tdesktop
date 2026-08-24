/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_webview_proxy.h"

#include "core/application.h"
#include "core/core_settings.h"

namespace Core {

std::optional<Webview::ProxySettings> CurrentWebviewProxy() {
	const auto &proxy = App().settings().proxy();
	if (!proxy.isEnabled()) {
		return std::nullopt;
	}
	const auto data = proxy.selected();
	return Webview::ProxySettings{
		.host = data.host.toStdString(),
		.port = std::to_string(data.port),
		.username = data.user.toStdString(),
		.password = data.password.toStdString(),
	};
}

} // namespace Core
