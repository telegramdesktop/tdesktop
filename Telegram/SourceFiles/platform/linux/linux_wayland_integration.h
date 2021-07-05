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
	[[nodiscard]] static WaylandIntegration *Instance();
	void waitForInterfaceAnnounce();
	[[nodiscard]] bool supportsXdgDecoration();
	[[nodiscard]] QString nativeHandle(QWindow *window);
	[[nodiscard]] bool skipTaskbarSupported();
	void skipTaskbar(QWindow *window, bool skip);
	void registerAppMenu(
		QWindow *window,
		const QString &serviceName,
		const QString &objectPath);

private:
	WaylandIntegration();
	~WaylandIntegration();

	class Private;
	const std::unique_ptr<Private> _private;
};

} // namespace internal
} // namespace Platform
