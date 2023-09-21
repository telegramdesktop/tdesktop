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

#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformwindow_p.h>

#include <qwayland-wayland.h>
#include <qwayland-plasma-shell.h>

using namespace QNativeInterface;
using namespace QNativeInterface::Private;
using namespace base::Platform::Wayland;

namespace Platform {
namespace internal {
namespace {

class PlasmaShell : public Global<QtWayland::org_kde_plasma_shell> {
public:
	using Global::Global;

	using Surface = AutoDestroyer<QtWayland::org_kde_plasma_surface>;
	base::flat_map<wl_surface*, Surface> surfaces;
};

} // namespace

struct WaylandIntegration::Private : public AutoDestroyer<QtWayland::wl_registry> {
	QtWayland::org_kde_plasma_surface plasmaSurface(QWindow *window);

	std::optional<PlasmaShell> plasmaShell;

protected:
	void registry_global(
			uint32_t name,
			const QString &interface,
			uint32_t version) override {
		if (interface == qstr("org_kde_plasma_shell")) {
			plasmaShell.emplace(object(), name, version);
		}
	}

	void registry_global_remove(uint32_t name) override {
		if (plasmaShell && name == plasmaShell->id()) {
			plasmaShell = std::nullopt;
		}
	}
};

QtWayland::org_kde_plasma_surface WaylandIntegration::Private::plasmaSurface(
		QWindow *window) {
	if (!plasmaShell) {
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

	const auto it = plasmaShell->surfaces.find(surface);
	if (it != plasmaShell->surfaces.cend()) {
		return it->second;
	}

	const auto plasmaSurface = plasmaShell->get_surface(surface);
	if (!plasmaSurface) {
		return {};
	}

	const auto result = plasmaShell->surfaces.emplace(surface, plasmaSurface);

	base::qt_signal_producer(
		native,
		&QWaylandWindow::surfaceDestroyed
	) | rpl::start_with_next([=] {
		auto it = plasmaShell->surfaces.find(surface);
		if (it != plasmaShell->surfaces.cend()) {
			plasmaShell->surfaces.erase(it);
		}
	}, result.first->second.lifetime());

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

	_private->init(wl_display_get_registry(display));
	wl_display_roundtrip(display);
}

WaylandIntegration::~WaylandIntegration() = default;

WaylandIntegration *WaylandIntegration::Instance() {
	if (!IsWayland()) return nullptr;
	static std::optional<WaylandIntegration> instance(std::in_place);
	[[maybe_unused]] static const auto Inited = [] {
		base::qt_signal_producer(
			QGuiApplication::platformNativeInterface(),
			&QObject::destroyed
		) | rpl::start_with_next([] {
			instance = std::nullopt;
		}, instance->_private->lifetime());
		return true;
	}();
	if (!instance) return nullptr;
	return &*instance;
}

bool WaylandIntegration::skipTaskbarSupported() {
	return _private->plasmaShell.has_value();
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
