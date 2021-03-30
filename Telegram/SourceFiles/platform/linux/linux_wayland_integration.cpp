/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_wayland_integration.h"

#include "base/platform/base_platform_info.h"

#include <connection_thread.h>
#include <registry.h>

using namespace KWayland::Client;

namespace Platform {
namespace internal {

class WaylandIntegration::Private : public QObject {
public:
	Private();

	[[nodiscard]] Registry &registry() {
		return _registry;
	}

	[[nodiscard]] std::atomic<bool> &interfacesAnnounced() {
		return _interfacesAnnounced;
	}

private:
	ConnectionThread _connection;
	Registry _registry;
	std::atomic<bool> _interfacesAnnounced = false;
};

WaylandIntegration::Private::Private() {
	connect(&_connection, &ConnectionThread::connected, [=] {
		LOG(("Successfully connected to Wayland server at socket: %1")
			.arg(_connection.socketName()));

		_registry.create(&_connection);
		_registry.setup();
	});

	connect(
		&_connection,
		&ConnectionThread::connectionDied,
		&_registry,
		&Registry::destroy);

	connect(&_registry, &Registry::interfacesAnnounced, [=] {
		_interfacesAnnounced = true;
	});

	_connection.initConnection();
}

WaylandIntegration::WaylandIntegration()
: _private(std::make_unique<Private>()) {
}

WaylandIntegration::~WaylandIntegration() = default;

WaylandIntegration *WaylandIntegration::Instance() {
	if (!IsWayland()) return nullptr;
	static WaylandIntegration instance;
	return &instance;
}

void WaylandIntegration::waitForInterfaceAnnounce() {
	if (!_private->interfacesAnnounced()) {
		QEventLoop loop;
		QObject::connect(
			&_private->registry(),
			&Registry::interfacesAnnounced,
			&loop,
			&QEventLoop::quit);
		loop.exec();
	}
}

bool WaylandIntegration::supportsXdgDecoration() {
	return _private->registry().hasInterface(
		Registry::Interface::XdgDecorationUnstableV1);
}

} // namespace internal
} // namespace Platform
