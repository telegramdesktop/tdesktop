/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {
namespace internal {

class WaylandIntegration {
public:
	WaylandIntegration();
	~WaylandIntegration();

	[[nodiscard]] static WaylandIntegration *Instance();

	[[nodiscard]] bool skipTaskbarSupported();
	void skipTaskbar(QWindow *window, bool skip);

private:
	struct Private;
	const std::unique_ptr<Private> _private;
};

} // namespace internal
} // namespace Platform
