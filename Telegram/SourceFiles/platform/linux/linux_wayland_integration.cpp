/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_wayland_integration.h"

#include "base/platform/base_platform_info.h"
#include "base/qt_signal_producer.h"
#include "base/flat_map.h"
#include "qwayland-plasma-shell.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client.h>

namespace Platform {
namespace internal {
namespace {

struct WlRegistryDeleter {
	void operator()(wl_registry *value) {
		wl_registry_destroy(value);
	}
};

struct PlasmaSurfaceDeleter {
	void operator()(org_kde_plasma_surface *value) {
		org_kde_plasma_surface_destroy(value);
	}
};

template <typename T>
class QtWaylandAutoDestroyer : public T {
public:
	QtWaylandAutoDestroyer() = default;

	~QtWaylandAutoDestroyer() {
		if (!this->isInitialized()) {
			return;
		}

		static constexpr auto HasDestroy = requires(const T &t) {
			t.destroy();
		};

		if constexpr (HasDestroy) {
			this->destroy();
		} else {
			free(this->object());
			this->init(nullptr);
		}
	}
};

} // namespace

struct WaylandIntegration::Private {
	org_kde_plasma_surface *plasmaSurface(QWindow *window);
	std::unique_ptr<wl_registry, WlRegistryDeleter> registry;
	QtWaylandAutoDestroyer<QtWayland::org_kde_plasma_shell> plasmaShell;
	uint32_t plasmaShellName = 0;
	base::flat_map<wl_surface*, std::unique_ptr<
		org_kde_plasma_surface,
		PlasmaSurfaceDeleter>> plasmaSurfaces;
	rpl::lifetime lifetime;

	static const struct wl_registry_listener RegistryListener;
};

const struct wl_registry_listener WaylandIntegration::Private::RegistryListener = {
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
			free(data->plasmaShell.object());
			data->plasmaShell.init(nullptr);
			data->plasmaShellName = 0;
		}
	}),
};

org_kde_plasma_surface *WaylandIntegration::Private::plasmaSurface(
		QWindow *window) {
	if (!plasmaShell.isInitialized()) {
		return nullptr;
	}

	const auto native = QGuiApplication::platformNativeInterface();
	if (!native) {
		return nullptr;
	}

	const auto surface = reinterpret_cast<wl_surface*>(
		native->nativeResourceForWindow(QByteArray("surface"), window));

	if (!surface) {
		return nullptr;
	}

	const auto it = plasmaSurfaces.find(surface);
	if (it != plasmaSurfaces.cend()) {
		return it->second.get();
	}

	const auto result = plasmaShell.get_surface(surface);
	if (!result) {
		return nullptr;
	}

	plasmaSurfaces.emplace(surface, result);

	base::qt_signal_producer(
		window,
		&QObject::destroyed
	) | rpl::start_with_next([=] {
		auto it = plasmaSurfaces.find(surface);
		if (it != plasmaSurfaces.cend()) {
			plasmaSurfaces.erase(it);
		}
	}, lifetime);

	return result;
}

WaylandIntegration::WaylandIntegration()
: _private(std::make_unique<Private>()) {
	const auto native = QGuiApplication::platformNativeInterface();
	if (!native) {
		return;
	}

	const auto display = reinterpret_cast<wl_display*>(
		native->nativeResourceForIntegration(QByteArray("wl_display")));

	if (!display) {
		return;
	}

	_private->registry.reset(wl_display_get_registry(display));
	wl_registry_add_listener(
		_private->registry.get(),
		&Private::RegistryListener,
		_private.get());

	base::qt_signal_producer(
		native,
		&QObject::destroyed
	) | rpl::start_with_next([=] {
		// too late for standard destructors, just free
		for (auto it = _private->plasmaSurfaces.begin()
			; it != _private->plasmaSurfaces.cend()
			; ++it) {
			free(it->second.release());
			_private->plasmaSurfaces.erase(it);
		}
		free(_private->plasmaShell.object());
		_private->plasmaShell.init(nullptr);
		free(_private->registry.release());
	}, _private->lifetime);

	wl_display_roundtrip(display);
}

WaylandIntegration::~WaylandIntegration() = default;

WaylandIntegration *WaylandIntegration::Instance() {
	if (!IsWayland()) return nullptr;
	static WaylandIntegration instance;
	return &instance;
}

bool WaylandIntegration::skipTaskbarSupported() {
	return _private->plasmaShell.isInitialized();
}

void WaylandIntegration::skipTaskbar(QWindow *window, bool skip) {
	auto plasmaSurface = _private->plasmaSurface(window);
	if (!plasmaSurface) {
		return;
	}

	org_kde_plasma_surface_set_skip_taskbar(plasmaSurface, skip);
}

} // namespace internal
} // namespace Platform
