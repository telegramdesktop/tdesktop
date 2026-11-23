/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {

struct GlassCapabilities {
	bool composition = false;
	bool transparency = false;
	bool highContrast = false;
	bool reducedMotion = false;
	bool remoteSession = false;
};

[[nodiscard]] GlassCapabilities QueryGlassCapabilities();

} // namespace Platform
