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
#include <surface.h>
#include <xdgforeign.h>
#include <plasmashell.h>
#include <appmenu.h>

using namespace KWayland::Client;

namespace Platform {
namespace internal {

class WaylandIntegration::Private : public QObject {
public:
	Private();

	[[nodiscard]] Registry &registry() {
		return _registry;
	}

	[[nodiscard]] XdgExporter *xdgExporter() {
		return _xdgExporter.get();
	}

	[[nodiscard]] PlasmaShell *plasmaShell() {
		return _plasmaShell.get();
	}

	[[nodiscard]] AppMenuManager *appMenuManager() {
		return _appMenuManager.get();
	}

	[[nodiscard]] QEventLoop &interfacesLoop() {
		return _interfacesLoop;
	}

	[[nodiscard]] bool interfacesAnnounced() const {
		return _interfacesAnnounced;
	}

private:
	ConnectionThread _connection;
	ConnectionThread *_applicationConnection = nullptr;
	Registry _registry;
	Registry _applicationRegistry;
	std::unique_ptr<XdgExporter> _xdgExporter;
	std::unique_ptr<PlasmaShell> _plasmaShell;
	std::unique_ptr<AppMenuManager> _appMenuManager;
	QEventLoop _interfacesLoop;
	bool _interfacesAnnounced = false;
};

WaylandIntegration::Private::Private()
: _applicationConnection(ConnectionThread::fromApplication(this)) {
	_applicationRegistry.create(_applicationConnection);
	_applicationRegistry.setup();

	connect(
		_applicationConnection,
		&ConnectionThread::connectionDied,
		&_applicationRegistry,
		&Registry::destroy);

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
		if (_interfacesLoop.isRunning()) {
			_interfacesLoop.quit();
		}
	});

	connect(
		&_applicationRegistry,
		&Registry::exporterUnstableV2Announced,
		[=](uint name, uint version) {
			_xdgExporter = std::unique_ptr<XdgExporter>{
				_applicationRegistry.createXdgExporter(name, version),
			};

			connect(
				_applicationConnection,
				&ConnectionThread::connectionDied,
				_xdgExporter.get(),
				&XdgExporter::destroy);
		});

	connect(
		&_applicationRegistry,
		&Registry::plasmaShellAnnounced,
		[=](uint name, uint version) {
			_plasmaShell = std::unique_ptr<PlasmaShell>{
				_applicationRegistry.createPlasmaShell(name, version),
			};

			connect(
				_applicationConnection,
				&ConnectionThread::connectionDied,
				_plasmaShell.get(),
				&PlasmaShell::destroy);
		});

	connect(
		&_applicationRegistry,
		&Registry::appMenuAnnounced,
		[=](uint name, uint version) {
			_appMenuManager = std::unique_ptr<AppMenuManager>{
				_applicationRegistry.createAppMenuManager(name, version),
			};

			connect(
				_applicationConnection,
				&ConnectionThread::connectionDied,
				_appMenuManager.get(),
				&AppMenuManager::destroy);
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
	Expects(!_private->interfacesLoop().isRunning());
	if (!_private->interfacesAnnounced()) {
		_private->interfacesLoop().exec();
	}
}

bool WaylandIntegration::supportsXdgDecoration() {
	return _private->registry().hasInterface(
		Registry::Interface::XdgDecorationUnstableV1);
}

QString WaylandIntegration::nativeHandle(QWindow *window) {
	if (const auto exporter = _private->xdgExporter()) {
		if (const auto surface = Surface::fromWindow(window)) {
			if (const auto exported = exporter->exportTopLevel(
				surface,
				surface)) {
				QEventLoop loop;
				QObject::connect(
					exported,
					&XdgExported::done,
					&loop,
					&QEventLoop::quit);
				loop.exec();
				return exported->handle();
			}
		}
	}
	return {};
}

bool WaylandIntegration::skipTaskbarSupported() {
	return _private->plasmaShell();
}

void WaylandIntegration::skipTaskbar(QWindow *window, bool skip) {
	const auto shell = _private->plasmaShell();
	if (!shell) {
		return;
	}

	const auto surface = Surface::fromWindow(window);
	if (!surface) {
		return;
	}

	const auto plasmaSurface = shell->createSurface(surface, surface);
	if (!plasmaSurface) {
		return;
	}

	plasmaSurface->setSkipTaskbar(skip);
}

void WaylandIntegration::registerAppMenu(
		QWindow *window,
		const QString &serviceName,
		const QString &objectPath) {
	const auto manager = _private->appMenuManager();
	if (!manager) {
		return;
	}

	const auto surface = Surface::fromWindow(window);
	if (!surface) {
		return;
	}

	const auto appMenu = manager->create(surface, surface);
	if (!appMenu) {
		return;
	}

	appMenu->setAddress(serviceName, objectPath);
}

} // namespace internal
} // namespace Platform
