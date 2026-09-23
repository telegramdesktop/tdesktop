/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "webview/webview_interface.h"

#include <optional>
#include <rpl/producer.h>

namespace Core {

[[nodiscard]] std::optional<Webview::ProxySettings> CurrentWebviewProxy();

// Fires when the effective webview proxy becomes different from `was`,
// so that an existing webview can be closed and recreated on demand.
[[nodiscard]] rpl::producer<> WebviewProxyChangesFrom(
	std::optional<Webview::ProxySettings> was);

} // namespace Core
