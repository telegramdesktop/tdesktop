/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_wayland_integration.h"

#include "base/platform/linux/base_linux_wayland_utilities.h"
#include "base/platform/base_platform_info.h"
#include "base/qt_signal_producer.h"
#include "base/flat_map.h"
#include "qwayland-plasma-shell.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformwindow_p.h>
#include <wayland-client.h>

using namespace QNativeInterface;
using namespace QNativeInterface::Private;
using namespace base::Platform::Wayland;

namespace Platform {
namespace internal {

struct WaylandIntegration::Private {
	QtWayland::org_kde_plasma_surface plasmaSurface(QWindow *window);

	std::unique_ptr<wl_registry, RegistryDeleter> registry;
	AutoDestroyer<QtWayland::org_kde_plasma_shell> plasmaShell;
	uint32_t plasmaShellName = 0;
	base::flat_map<
		wl_surface*,
		AutoDestroyer<QtWayland::org_kde_plasma_surface>
	> plasmaSurfaces;
	rpl::lifetime lifetime;

	static const wl_registry_listener RegistryListener;
};

const wl_registry_listener WaylandIntegration::Private::RegistryListener = {
	decltype(wl_registry_listener::global)(+[](
			Private *data,
			wl_registry *registry,
			uint32_t name,
			const char *interface,
			uint32_t version) {
		if (interface == qstr("org_kde_plasma_shell")) {
			data->plasmaShell.init(registry, name, version);
			data->plasmaShellName = name;
		}
	}),
	decltype(wl_registry_listener::global_remove)(+[](
			Private *data,
			wl_registry *registry,
			uint32_t name) {
		if (name == data->plasmaShellName) {
			data->plasmaShell = {};
			data->plasmaShellName = 0;
		}
	}),
};

QtWayland::org_kde_plasma_surface WaylandIntegration::Private::plasmaSurface(
		QWindow *window) {
	if (!plasmaShell.isInitialized()) {
		return {};
	}

	const auto native = window->nativeInterface<QWaylandWindow>();
	if (!native) {
		return {};
	}

	const auto surface = native->surface();
	if (!surface) {
		return {};
	}

	const auto it = plasmaSurfaces.find(surface);
	if (it != plasmaSurfaces.cend()) {
		return it->second;
	}

	const auto plasmaSurface = plasmaShell.get_surface(surface);
	if (!plasmaSurface) {
		return {};
	}

	const auto result = plasmaSurfaces.emplace(surface, plasmaSurface);

	base::qt_signal_producer(
		native,
		&QWaylandWindow::surfaceDestroyed
	) | rpl::start_with_next([=] {
		auto it = plasmaSurfaces.find(surface);
		if (it != plasmaSurfaces.cend()) {
			plasmaSurfaces.erase(it);
		}
	}, lifetime);

	return result.first->second;
}

WaylandIntegration::WaylandIntegration()
: _private(std::make_unique<Private>()) {
	const auto native = qApp->nativeInterface<QWaylandApplication>();
	if (!native) {
		return;
	}

	const auto display = native->display();
	if (!display) {
		return;
	}

	_private->registry.reset(wl_display_get_registry(display));
	wl_registry_add_listener(
		_private->registry.get(),
		&Private::RegistryListener,
		_private.get());

	wl_display_roundtrip(display);
}

WaylandIntegration::~WaylandIntegration() = default;

WaylandIntegration *WaylandIntegration::Instance() {
	if (!IsWayland()) return nullptr;
	static std::optional<WaylandIntegration> instance(std::in_place);
	base::qt_signal_producer(
		QGuiApplication::platformNativeInterface(),
		&QObject::destroyed
	) | rpl::start_with_next([&] {
		instance = std::nullopt;
	}, instance->_private->lifetime);
	if (!instance) return nullptr;
	return &*instance;
}

bool WaylandIntegration::skipTaskbarSupported() {
	return _private->plasmaShell.isInitialized();
}

void WaylandIntegration::skipTaskbar(QWindow *window, bool skip) {
	auto plasmaSurface = _private->plasmaSurface(window);
	if (!plasmaSurface.isInitialized()) {
		return;
	}

	plasmaSurface.set_skip_taskbar(skip);
}

} // namespace internal
} // namespace Platform
