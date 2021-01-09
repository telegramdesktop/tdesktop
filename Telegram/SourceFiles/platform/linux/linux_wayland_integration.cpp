/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_wayland_integration.h"

#include "base/platform/base_platform_info.h"

#include <QtGui/QWindow>

#include <private/qwaylanddisplay_p.h>
#include <private/qwaylandwindow_p.h>
#include <private/qwaylandshellsurface_p.h>

#include <connection_thread.h>
#include <registry.h>

#if QT_VERSION < QT_VERSION_CHECK(5, 13, 0) && !defined DESKTOP_APP_QT_PATCHED
#include <wayland-client.h>
#endif // Qt < 5.13 && !DESKTOP_APP_QT_PATCHED

using QtWaylandClient::QWaylandWindow;
using namespace KWayland::Client;

namespace Platform {
namespace internal {

namespace {

#if QT_VERSION < QT_VERSION_CHECK(5, 13, 0) && !defined DESKTOP_APP_QT_PATCHED
enum wl_shell_surface_resize WlResizeFromEdges(Qt::Edges edges) {
	if (edges == (Qt::TopEdge | Qt::LeftEdge))
		return WL_SHELL_SURFACE_RESIZE_TOP_LEFT;
	if (edges == Qt::TopEdge)
		return WL_SHELL_SURFACE_RESIZE_TOP;
	if (edges == (Qt::TopEdge | Qt::RightEdge))
		return WL_SHELL_SURFACE_RESIZE_TOP_RIGHT;
	if (edges == Qt::RightEdge)
		return WL_SHELL_SURFACE_RESIZE_RIGHT;
	if (edges == (Qt::RightEdge | Qt::BottomEdge))
		return WL_SHELL_SURFACE_RESIZE_BOTTOM_RIGHT;
	if (edges == Qt::BottomEdge)
		return WL_SHELL_SURFACE_RESIZE_BOTTOM;
	if (edges == (Qt::BottomEdge | Qt::LeftEdge))
		return WL_SHELL_SURFACE_RESIZE_BOTTOM_LEFT;
	if (edges == Qt::LeftEdge)
		return WL_SHELL_SURFACE_RESIZE_LEFT;

	return WL_SHELL_SURFACE_RESIZE_NONE;
}
#endif // Qt < 5.13 && !DESKTOP_APP_QT_PATCHED

} // namespace

class WaylandIntegration::Private : public QObject {
public:
	Private();

	[[nodiscard]] Registry &registry() {
		return _registry;
	}

	[[nodiscard]] QEventLoop &interfacesLoop() {
		return _interfacesLoop;
	}

	[[nodiscard]] bool interfacesAnnounced() const {
		return _interfacesAnnounced;
	}

private:
	ConnectionThread _connection;
	Registry _registry;
	QEventLoop _interfacesLoop;
	bool _interfacesAnnounced = false;
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
		_interfacesLoop.quit();
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
		_private->interfacesLoop().exec();
	}
}

bool WaylandIntegration::supportsXdgDecoration() {
	return _private->registry().hasInterface(
		Registry::Interface::XdgDecorationUnstableV1);
}

bool WaylandIntegration::startMove(QWindow *window) {
	// There are startSystemMove on Qt 5.15
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0) && !defined DESKTOP_APP_QT_PATCHED
	if (const auto waylandWindow = static_cast<QWaylandWindow*>(
		window->handle())) {
		if (const auto seat = waylandWindow->display()->lastInputDevice()) {
			if (const auto shellSurface = waylandWindow->shellSurface()) {
				return shellSurface->move(seat);
			}
		}
	}
#endif // Qt < 5.15 && !DESKTOP_APP_QT_PATCHED

	return false;
}

bool WaylandIntegration::startResize(QWindow *window, Qt::Edges edges) {
	// There are startSystemResize on Qt 5.15
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0) && !defined DESKTOP_APP_QT_PATCHED
	if (const auto waylandWindow = static_cast<QWaylandWindow*>(
		window->handle())) {
		if (const auto seat = waylandWindow->display()->lastInputDevice()) {
			if (const auto shellSurface = waylandWindow->shellSurface()) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
				shellSurface->resize(seat, edges);
				return true;
#else // Qt >= 5.13
				shellSurface->resize(seat, WlResizeFromEdges(edges));
				return true;
#endif // Qt < 5.13
			}
		}
	}
#endif // Qt < 5.15 && !DESKTOP_APP_QT_PATCHED

	return false;
}

bool WaylandIntegration::showWindowMenu(QWindow *window) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0) || defined DESKTOP_APP_QT_PATCHED
	if (const auto waylandWindow = static_cast<QWaylandWindow*>(
		window->handle())) {
		if (const auto seat = waylandWindow->display()->lastInputDevice()) {
			if (const auto shellSurface = waylandWindow->shellSurface()) {
				return shellSurface->showWindowMenu(seat);
			}
		}
	}
#endif // Qt >= 5.13 || DESKTOP_APP_QT_PATCHED

	return false;
}

} // namespace internal
} // namespace Platform
