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

struct WaylandIntegration::Private {
	std::unique_ptr<ConnectionThread> connection;
	Registry registry;
	std::unique_ptr<XdgExporter> xdgExporter;
	std::unique_ptr<PlasmaShell> plasmaShell;
	std::unique_ptr<AppMenuManager> appMenuManager;
};

WaylandIntegration::WaylandIntegration()
: _private(std::make_unique<Private>()) {
	_private->connection = std::unique_ptr<ConnectionThread>{
		ConnectionThread::fromApplication(),
	};

	_private->registry.create(_private->connection.get());
	_private->registry.setup();

	QObject::connect(
		_private->connection.get(),
		&ConnectionThread::connectionDied,
		&_private->registry,
		&Registry::destroy);

	QObject::connect(
		&_private->registry,
		&Registry::exporterUnstableV2Announced,
		[=](uint name, uint version) {
			_private->xdgExporter = std::unique_ptr<XdgExporter>{
				_private->registry.createXdgExporter(name, version),
			};

			QObject::connect(
				_private->connection.get(),
				&ConnectionThread::connectionDied,
				_private->xdgExporter.get(),
				&XdgExporter::destroy);
		});

	QObject::connect(
		&_private->registry,
		&Registry::plasmaShellAnnounced,
		[=](uint name, uint version) {
			_private->plasmaShell = std::unique_ptr<PlasmaShell>{
				_private->registry.createPlasmaShell(name, version),
			};

			QObject::connect(
				_private->connection.get(),
				&ConnectionThread::connectionDied,
				_private->plasmaShell.get(),
				&PlasmaShell::destroy);
		});

	QObject::connect(
		&_private->registry,
		&Registry::appMenuAnnounced,
		[=](uint name, uint version) {
			_private->appMenuManager = std::unique_ptr<AppMenuManager>{
				_private->registry.createAppMenuManager(name, version),
			};

			QObject::connect(
				_private->connection.get(),
				&ConnectionThread::connectionDied,
				_private->appMenuManager.get(),
				&AppMenuManager::destroy);
		});
}

WaylandIntegration::~WaylandIntegration() = default;

WaylandIntegration *WaylandIntegration::Instance() {
	if (!IsWayland()) return nullptr;
	static WaylandIntegration instance;
	return &instance;
}

QString WaylandIntegration::nativeHandle(QWindow *window) {
	if (const auto exporter = _private->xdgExporter.get()) {
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
	return _private->plasmaShell != nullptr;
}

void WaylandIntegration::skipTaskbar(QWindow *window, bool skip) {
	const auto shell = _private->plasmaShell.get();
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
	const auto manager = _private->appMenuManager.get();
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
