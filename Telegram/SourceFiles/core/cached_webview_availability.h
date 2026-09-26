/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "webview/webview_interface.h"

namespace Core {

[[nodiscard]] inline const Webview::Available &CachedWebviewAvailability() {
	static const auto result = Webview::Availability();
	return result;
}

} // namespace Core
