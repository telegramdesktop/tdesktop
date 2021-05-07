/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/main_window_linux.h"

#include "styles/style_window.h"
#include "platform/linux/specific_linux.h"
#include "platform/linux/linux_wayland_integration.h"
#include "history/history.h"
#include "history/history_widget.h"
#include "history/history_inner_widget.h"
#include "main/main_account.h" // Account::sessionChanges.
#include "main/main_session.h"
#include "mainwindow.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/sandbox.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/about_box.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "base/platform/base_platform_info.h"
#include "base/event_filter.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/input_fields.h"
#include "ui/ui_utility.h"

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include "base/platform/linux/base_linux_glibmm_helper.h"
#include "base/platform/linux/base_linux_dbus_utilities.h"
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
#include "base/platform/linux/base_linux_xcb_utilities.h"
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

#include <QtCore/QSize>
#include <QtCore/QTemporaryFile>
#include <QtGui/QWindow>

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusObjectPath>
#include <QtDBus/QDBusMetaType>

#include <statusnotifieritem.h>
#include <dbusmenuexporter.h>

#include <glibmm.h>
#include <giomm.h>
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

namespace Platform {
namespace {

using internal::WaylandIntegration;
using WorkMode = Core::Settings::WorkMode;

constexpr auto kPanelTrayIconName = "telegram-panel"_cs;
constexpr auto kMutePanelTrayIconName = "telegram-mute-panel"_cs;
constexpr auto kAttentionPanelTrayIconName = "telegram-attention-panel"_cs;

constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties"_cs;
constexpr auto kTrayIconFilename = "tdesktop-trayicon-XXXXXX.png"_cs;

constexpr auto kSNIWatcherService = "org.kde.StatusNotifierWatcher"_cs;
constexpr auto kSNIWatcherObjectPath = "/StatusNotifierWatcher"_cs;
constexpr auto kSNIWatcherInterface = kSNIWatcherService;

constexpr auto kAppMenuService = "com.canonical.AppMenu.Registrar"_cs;
constexpr auto kAppMenuObjectPath = "/com/canonical/AppMenu/Registrar"_cs;
constexpr auto kAppMenuInterface = kAppMenuService;

constexpr auto kMainMenuObjectPath = "/MenuBar"_cs;

bool TrayIconMuted = true;
int32 TrayIconCount = 0;
base::flat_map<int, QImage> TrayIconImageBack;
QIcon TrayIcon;
QString TrayIconThemeName, TrayIconName;

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
void XCBSkipTaskbar(QWindow *window, bool skip) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return;
	}

	const auto root = base::Platform::XCB::GetRootWindowFromQt();
	if (!root.has_value()) {
		return;
	}

	const auto stateAtom = base::Platform::XCB::GetAtom(
		connection,
		"_NET_WM_STATE");

	if (!stateAtom.has_value()) {
		return;
	}

	const auto skipTaskbarAtom = base::Platform::XCB::GetAtom(
		connection,
		"_NET_WM_STATE_SKIP_TASKBAR");

	if (!skipTaskbarAtom.has_value()) {
		return;
	}

	xcb_client_message_event_t xev;
	xev.response_type = XCB_CLIENT_MESSAGE;
	xev.type = *stateAtom;
	xev.sequence = 0;
	xev.window = window->winId();
	xev.format = 32;
	xev.data.data32[0] = skip ? 1 : 0;
	xev.data.data32[1] = *skipTaskbarAtom;
	xev.data.data32[2] = 0;
	xev.data.data32[3] = 0;
	xev.data.data32[4] = 0;

	xcb_send_event(
		connection,
		false,
		*root,
		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
			| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
		reinterpret_cast<const char*>(&xev));
}

void XCBSetDesktopFileName(QWindow *window) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return;
	}

	const auto desktopFileAtom = base::Platform::XCB::GetAtom(
		connection,
		"_KDE_NET_WM_DESKTOP_FILE");

	const auto utf8Atom = base::Platform::XCB::GetAtom(
		connection,
		"UTF8_STRING");

	if (!desktopFileAtom.has_value() || !utf8Atom.has_value()) {
		return;
	}

	const auto filename = QGuiApplication::desktopFileName()
		.chopped(8)
		.toUtf8();

	xcb_change_property(
		connection,
		XCB_PROP_MODE_REPLACE,
		window->winId(),
		*desktopFileAtom,
		*utf8Atom,
		8,
		filename.size(),
		filename.data());
}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

void SkipTaskbar(QWindow *window, bool skip) {
	if (const auto integration = WaylandIntegration::Instance()) {
		integration->skipTaskbar(window, skip);
	}

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	if (IsX11()) {
		XCBSkipTaskbar(window, skip);
	}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION
}

QString GetPanelIconName(int counter, bool muted) {
	return (counter > 0)
		? (muted
			? kMutePanelTrayIconName.utf16()
			: kAttentionPanelTrayIconName.utf16())
		: kPanelTrayIconName.utf16();
}

QString GetTrayIconName(int counter, bool muted) {
	const auto iconName = GetIconName();
	const auto panelIconName = GetPanelIconName(counter, muted);

	if (QIcon::hasThemeIcon(panelIconName)) {
		return panelIconName;
	} else if (QIcon::hasThemeIcon(iconName)) {
		return iconName;
	}

	return QString();
}

int GetCounterSlice(int counter) {
	return (counter >= 1000)
		? (1000 + (counter % 100))
		: counter;
}

bool IsIconRegenerationNeeded(
		int counter,
		bool muted,
		const QString &iconThemeName = QIcon::themeName()) {
	const auto iconName = GetTrayIconName(counter, muted);
	const auto counterSlice = GetCounterSlice(counter);

	return TrayIcon.isNull()
		|| iconThemeName != TrayIconThemeName
		|| iconName != TrayIconName
		|| muted != TrayIconMuted
		|| counterSlice != TrayIconCount;
}

void UpdateIconRegenerationNeeded(
		const QIcon &icon,
		int counter,
		bool muted,
		const QString &iconThemeName) {
	const auto iconName = GetTrayIconName(counter, muted);
	const auto counterSlice = GetCounterSlice(counter);

	TrayIcon = icon;
	TrayIconMuted = muted;
	TrayIconCount = counterSlice;
	TrayIconThemeName = iconThemeName;
	TrayIconName = iconName;
}

QIcon TrayIconGen(int counter, bool muted) {
	const auto iconThemeName = QIcon::themeName();

	if (!IsIconRegenerationNeeded(counter, muted, iconThemeName)) {
		return TrayIcon;
	}

	const auto iconName = GetTrayIconName(counter, muted);
	const auto panelIconName = GetPanelIconName(counter, muted);

	if (iconName == panelIconName) {
		const auto result = QIcon::fromTheme(iconName);
		UpdateIconRegenerationNeeded(result, counter, muted, iconThemeName);
		return result;
	}

	QIcon result;
	QIcon systemIcon;

	static const auto iconSizes = {
		16,
		22,
		24,
		32,
		48,
	};

	static const auto dprSize = [](const QImage &image) {
		return image.size() / image.devicePixelRatio();
	};

	for (const auto iconSize : iconSizes) {
		auto &currentImageBack = TrayIconImageBack[iconSize];
		const auto desiredSize = QSize(iconSize, iconSize);

		if (currentImageBack.isNull()
			|| iconThemeName != TrayIconThemeName
			|| iconName != TrayIconName) {
			if (!iconName.isEmpty()) {
				if (systemIcon.isNull()) {
					systemIcon = QIcon::fromTheme(iconName);
				}

				// We can't use QIcon::actualSize here
				// since it works incorrectly with svg icon themes
				currentImageBack = systemIcon
					.pixmap(desiredSize)
					.toImage();

				const auto firstAttemptSize = dprSize(currentImageBack);

				// if current icon theme is not a svg one, Qt can return
				// a pixmap that less in size even if there are a bigger one
				if (firstAttemptSize.width() < desiredSize.width()) {
					const auto availableSizes = systemIcon.availableSizes();

					const auto biggestSize = ranges::max_element(
						availableSizes,
						std::less<>(),
						&QSize::width);

					if (biggestSize->width() > firstAttemptSize.width()) {
						currentImageBack = systemIcon
							.pixmap(*biggestSize)
							.toImage();
					}
				}
			} else {
				currentImageBack = Core::App().logo();
			}

			if (dprSize(currentImageBack) != desiredSize) {
				currentImageBack = currentImageBack.scaled(
					desiredSize * currentImageBack.devicePixelRatio(),
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);
			}
		}

		auto iconImage = currentImageBack;

		if (counter > 0) {
			const auto &bg = muted
				? st::trayCounterBgMute
				: st::trayCounterBg;
			const auto &fg = st::trayCounterFg;
			if (iconSize >= 22) {
				auto layerSize = -16;
				if (iconSize >= 48) {
					layerSize = -32;
				} else if (iconSize >= 36) {
					layerSize = -24;
				} else if (iconSize >= 32) {
					layerSize = -20;
				}
				const auto layer = App::wnd()->iconWithCounter(
					layerSize,
					counter,
					bg,
					fg,
					false);

				QPainter p(&iconImage);
				p.drawImage(
					iconImage.width() - layer.width() - 1,
					iconImage.height() - layer.height() - 1,
					layer);
			} else {
				App::wnd()->placeSmallCounter(
					iconImage,
					16,
					counter,
					bg,
					QPoint(),
					fg);
			}
		}

		result.addPixmap(Ui::PixmapFromImage(std::move(iconImage)));
	}

	UpdateIconRegenerationNeeded(result, counter, muted, iconThemeName);

	return result;
}

void SendKeySequence(
	Qt::Key key,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
	const auto focused = QApplication::focusWidget();
	if (qobject_cast<QLineEdit*>(focused)
		|| qobject_cast<QTextEdit*>(focused)
		|| qobject_cast<HistoryInner*>(focused)) {
		QApplication::postEvent(
			focused,
			new QKeyEvent(QEvent::KeyPress, key, modifiers));

		QApplication::postEvent(
			focused,
			new QKeyEvent(QEvent::KeyRelease, key, modifiers));
	}
}

void ForceDisabled(QAction *action, bool disabled) {
	if (action->isEnabled()) {
		if (disabled) action->setDisabled(true);
	} else if (!disabled) {
		action->setDisabled(false);
	}
}

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
bool IsIndicatorApplication() {
	// Hack for indicator-application,
	// which doesn't handle icons sent across D-Bus:
	// save the icon to a temp file
	// and set the icon name to that filename.
	static const auto Result = [] {
		try {
			const auto connection = Gio::DBus::Connection::get_sync(
				Gio::DBus::BusType::BUS_TYPE_SESSION);

			const auto ubuntuIndicator = base::Platform::DBus::NameHasOwner(
				connection,
				"com.canonical.indicator.application");

			const auto ayatanaIndicator = base::Platform::DBus::NameHasOwner(
				connection,
				"org.ayatana.indicator.application");

			return ubuntuIndicator || ayatanaIndicator;
		} catch (...) {
		}

		return false;
	}();

	return Result;
}

std::unique_ptr<QTemporaryFile> TrayIconFile(
		const QIcon &icon,
		QObject *parent = nullptr) {
	static const auto templateName = AppRuntimeDirectory()
		+ kTrayIconFilename.utf16();

	static const auto dprSize = [](const QPixmap &pixmap) {
		return pixmap.size() / pixmap.devicePixelRatio();
	};

	static const auto desiredSize = QSize(22, 22);

	static const auto scalePixmap = [=](const QPixmap &pixmap) {
		if (dprSize(pixmap) != desiredSize) {
			return pixmap.scaled(
				desiredSize * pixmap.devicePixelRatio(),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
		} else {
			return pixmap;
		}
	};

	auto ret = std::make_unique<QTemporaryFile>(
		templateName,
		parent);

	ret->open();

	const auto firstAttempt = icon.pixmap(desiredSize);
	const auto firstAttemptSize = dprSize(firstAttempt);

	if (firstAttemptSize.width() < desiredSize.width()) {
		const auto availableSizes = icon.availableSizes();

		const auto biggestSize = ranges::max_element(
			availableSizes,
			std::less<>(),
			&QSize::width);

		if (biggestSize->width() > firstAttemptSize.width()) {
			scalePixmap(icon.pixmap(*biggestSize)).save(ret.get());
		} else {
			scalePixmap(firstAttempt).save(ret.get());
		}
	} else {
		scalePixmap(firstAttempt).save(ret.get());
	}

	ret->close();

	return ret;
}

bool UseUnityCounter() {
	static const auto Result = [&] {
		try {
			const auto connection = Gio::DBus::Connection::get_sync(
				Gio::DBus::BusType::BUS_TYPE_SESSION);

			return base::Platform::DBus::NameHasOwner(
				connection,
				"com.canonical.Unity");
		} catch (...) {
		}

		return false;
	}();

	return Result;
}

bool IsSNIAvailable() {
	try {
		const auto connection = [] {
			try {
				return Gio::DBus::Connection::get_sync(
					Gio::DBus::BusType::BUS_TYPE_SESSION);
			} catch (...) {
				return Glib::RefPtr<Gio::DBus::Connection>();
			}
		}();

		if (!connection) {
			return false;
		}

		auto reply = connection->call_sync(
			std::string(kSNIWatcherObjectPath),
			std::string(kPropertiesInterface),
			"Get",
			base::Platform::MakeGlibVariant(std::tuple{
				Glib::ustring(std::string(kSNIWatcherInterface)),
				Glib::ustring("IsStatusNotifierHostRegistered"),
			}),
			std::string(kSNIWatcherService));

		return base::Platform::GlibVariantCast<bool>(
			base::Platform::GlibVariantCast<Glib::VariantBase>(
				reply.get_child(0)));
	} catch (const Glib::Error &e) {
		static const auto NotSupportedErrors = {
			"org.freedesktop.DBus.Error.ServiceUnknown",
		};

		const auto errorName = Gio::DBus::ErrorUtils::get_remote_error(e);
		if (ranges::contains(NotSupportedErrors, errorName)) {
			return false;
		}

		LOG(("SNI Error: %1")
			.arg(QString::fromStdString(e.what())));
	} catch (const std::exception &e) {
		LOG(("SNI Error: %1")
			.arg(QString::fromStdString(e.what())));
	}

	return false;
}

uint djbStringHash(const std::string &string) {
	uint hash = 5381;
	for (const auto &curChar : string) {
		hash = (hash << 5) + hash + curChar;
	}
	return hash;
}

bool IsAppMenuSupported() {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		return base::Platform::DBus::NameHasOwner(
			connection,
			std::string(kAppMenuService));
	} catch (...) {
	}

	return false;
}

// This call must be made from the same bus connection as DBusMenuExporter
// So it must use QDBusConnection
void RegisterAppMenu(QWindow *window, const QString &menuPath) {
	if (const auto integration = WaylandIntegration::Instance()) {
		integration->registerAppMenu(
			window,
			QDBusConnection::sessionBus().baseService(),
			menuPath);
		return;
	}

	auto message = QDBusMessage::createMethodCall(
		kAppMenuService.utf16(),
		kAppMenuObjectPath.utf16(),
		kAppMenuInterface.utf16(),
		qsl("RegisterWindow"));

	message.setArguments({
		window->winId(),
		QVariant::fromValue(QDBusObjectPath(menuPath))
	});

	QDBusConnection::sessionBus().send(message);
}

// This call must be made from the same bus connection as DBusMenuExporter
// So it must use QDBusConnection
void UnregisterAppMenu(QWindow *window) {
	if (const auto integration = WaylandIntegration::Instance()) {
		return;
	}

	auto message = QDBusMessage::createMethodCall(
		kAppMenuService.utf16(),
		kAppMenuObjectPath.utf16(),
		kAppMenuInterface.utf16(),
		qsl("UnregisterWindow"));

	message.setArguments({
		window->winId()
	});

	QDBusConnection::sessionBus().send(message);
}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

} // namespace

class MainWindow::Private {
public:
	explicit Private(not_null<MainWindow*> window)
	: _public(window) {
	}

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	Glib::RefPtr<Gio::DBus::Connection> dbusConnection;

	StatusNotifierItem *sniTrayIcon = nullptr;
	uint sniRegisteredSignalId = 0;
	uint sniWatcherId = 0;
	std::unique_ptr<QTemporaryFile> trayIconFile;

	bool appMenuSupported = false;
	uint appMenuWatcherId = 0;
	DBusMenuExporter *mainMenuExporter = nullptr;

	void setSNITrayIcon(int counter, bool muted);
	void attachToSNITrayIcon();
	void handleSNIHostRegistered();

	void handleSNIOwnerChanged(
		const QString &service,
		const QString &oldOwner,
		const QString &newOwner);

	void handleAppMenuOwnerChanged(
		const QString &service,
		const QString &oldOwner,
		const QString &newOwner);
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

private:
	not_null<MainWindow*> _public;
};

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
void MainWindow::Private::setSNITrayIcon(int counter, bool muted) {
	const auto iconName = GetTrayIconName(counter, muted);
	const auto panelIconName = GetPanelIconName(counter, muted);

	if (iconName == panelIconName) {
		if (sniTrayIcon->iconName() == iconName) {
			return;
		}

		sniTrayIcon->setIconByName(iconName);
		sniTrayIcon->setToolTipIconByName(iconName);
	} else if (IsIndicatorApplication()) {
		if (!IsIconRegenerationNeeded(counter, muted)
			&& trayIconFile
			&& sniTrayIcon->iconName() == trayIconFile->fileName()) {
			return;
		}

		const auto icon = TrayIconGen(counter, muted);
		trayIconFile = TrayIconFile(icon, _public);

		if (trayIconFile) {
			// indicator-application doesn't support tooltips
			sniTrayIcon->setIconByName(trayIconFile->fileName());
		}
	} else {
		if (!IsIconRegenerationNeeded(counter, muted)
			&& !sniTrayIcon->iconPixmap().isEmpty()
			&& sniTrayIcon->iconName().isEmpty()) {
			return;
		}

		const auto icon = TrayIconGen(counter, muted);
		sniTrayIcon->setIconByPixmap(icon);
		sniTrayIcon->setToolTipIconByPixmap(icon);
	}
}

void MainWindow::Private::attachToSNITrayIcon() {
	sniTrayIcon->setToolTipTitle(AppName.utf16());
	connect(sniTrayIcon,
		&StatusNotifierItem::activateRequested,
		[=](const QPoint &) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				_public->handleTrayIconActication(QSystemTrayIcon::Trigger);
			});
	});
	connect(sniTrayIcon,
		&StatusNotifierItem::secondaryActivateRequested,
		[=](const QPoint &) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				_public->handleTrayIconActication(QSystemTrayIcon::MiddleClick);
			});
	});
}

void MainWindow::Private::handleSNIHostRegistered() {
	if (_public->_sniAvailable) {
		return;
	}

	_public->_sniAvailable = true;

	if (Core::App().settings().workMode() == WorkMode::WindowOnly) {
		return;
	}

	LOG(("Switching to SNI tray icon..."));

	if (_public->trayIcon) {
		_public->trayIcon->setContextMenu(nullptr);
		_public->trayIcon->deleteLater();
	}
	_public->trayIcon = nullptr;

	_public->psSetupTrayIcon();

	SkipTaskbar(
		_public->windowHandle(),
		Core::App().settings().workMode() == WorkMode::TrayOnly);
}

void MainWindow::Private::handleSNIOwnerChanged(
		const QString &service,
		const QString &oldOwner,
		const QString &newOwner) {
	_public->_sniAvailable = IsSNIAvailable();

	if (Core::App().settings().workMode() == WorkMode::WindowOnly) {
		return;
	}

	if (oldOwner.isEmpty() && !newOwner.isEmpty() && _public->_sniAvailable) {
		LOG(("Switching to SNI tray icon..."));
	} else if (!oldOwner.isEmpty() && newOwner.isEmpty()) {
		LOG(("Switching to Qt tray icon..."));
	} else {
		return;
	}

	if (_public->trayIcon) {
		_public->trayIcon->setContextMenu(0);
		_public->trayIcon->deleteLater();
	}
	_public->trayIcon = nullptr;

	if (_public->trayAvailable()) {
		_public->psSetupTrayIcon();
	} else {
		LOG(("System tray is not available."));
	}

	SkipTaskbar(
		_public->windowHandle(),
		(Core::App().settings().workMode() == WorkMode::TrayOnly)
			&& _public->trayAvailable());
}

void MainWindow::Private::handleAppMenuOwnerChanged(
		const QString &service,
		const QString &oldOwner,
		const QString &newOwner) {
	if (oldOwner.isEmpty() && !newOwner.isEmpty()) {
		appMenuSupported = true;
		LOG(("Using D-Bus global menu."));
	} else if (!oldOwner.isEmpty() && newOwner.isEmpty()) {
		appMenuSupported = false;
		LOG(("Not using D-Bus global menu."));
	}

	if (appMenuSupported && mainMenuExporter) {
		RegisterAppMenu(_public->windowHandle(), kMainMenuObjectPath.utf16());
	} else {
		UnregisterAppMenu(_public->windowHandle());
	}
}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

MainWindow::MainWindow(not_null<Window::Controller*> controller)
: Window::MainWindow(controller)
, _private(std::make_unique<Private>(this)) {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	qDBusRegisterMetaType<ToolTip>();
	qDBusRegisterMetaType<IconPixmap>();
	qDBusRegisterMetaType<IconPixmapList>();
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
}

void MainWindow::initHook() {
	base::install_event_filter(windowHandle(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Expose) {
			auto ee = static_cast<QExposeEvent*>(e.get());
			if (ee->region().isNull()) {
				return base::EventFilterResult::Continue;
			}
			if (!windowHandle()
				|| windowHandle()->parent()
				|| !windowHandle()->isVisible()) {
				return base::EventFilterResult::Continue;
			}
			handleNativeSurfaceChanged(true);
		} else if (e->type() == QEvent::Hide) {
			if (!windowHandle() || windowHandle()->parent()) {
				return base::EventFilterResult::Continue;
			}
			handleNativeSurfaceChanged(false);
		}
		return base::EventFilterResult::Continue;
	});

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	_sniAvailable = IsSNIAvailable();
	_private->appMenuSupported = IsAppMenuSupported();

	try {
		_private->dbusConnection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		_private->sniRegisteredSignalId = _private->dbusConnection->signal_subscribe(
			[](
				const Glib::RefPtr<Gio::DBus::Connection> &connection,
				const Glib::ustring &sender_name,
				const Glib::ustring &object_path,
				const Glib::ustring &interface_name,
				const Glib::ustring &signal_name,
				const Glib::VariantContainerBase &parameters) {
				if (signal_name == "StatusNotifierHostRegistered") {
					crl::on_main([] {
						if (const auto window = App::wnd()) {
							window->_private->handleSNIHostRegistered();
						}
					});
				}
			},
			std::string(kSNIWatcherService),
			std::string(kSNIWatcherInterface),
			"StatusNotifierHostRegistered",
			std::string(kSNIWatcherObjectPath));

		_private->sniWatcherId = base::Platform::DBus::RegisterServiceWatcher(
			_private->dbusConnection,
			std::string(kSNIWatcherService),
			[=](
				const Glib::ustring &service,
				const Glib::ustring &oldOwner,
				const Glib::ustring &newOwner) {
				_private->handleSNIOwnerChanged(
					QString::fromStdString(service),
					QString::fromStdString(oldOwner),
					QString::fromStdString(newOwner));
			});

		_private->appMenuWatcherId = base::Platform::DBus::RegisterServiceWatcher(
			_private->dbusConnection,
			std::string(kAppMenuService),
			[=](
				const Glib::ustring &service,
				const Glib::ustring &oldOwner,
				const Glib::ustring &newOwner) {
				_private->handleAppMenuOwnerChanged(
					QString::fromStdString(service),
					QString::fromStdString(oldOwner),
					QString::fromStdString(newOwner));
			});
	} catch (...) {
	}

	if (_private->appMenuSupported) {
		LOG(("Using D-Bus global menu."));
	} else {
		LOG(("Not using D-Bus global menu."));
	}

	if (UseUnityCounter()) {
		LOG(("Using Unity launcher counter."));
	} else {
		LOG(("Not using Unity launcher counter."));
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	XCBSetDesktopFileName(windowHandle());
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

	LOG(("System tray available: %1").arg(Logs::b(trayAvailable())));
}

bool MainWindow::hasTrayIcon() const {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	return trayIcon || (_sniAvailable && _private->sniTrayIcon);
#else
	return trayIcon;
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
}

bool MainWindow::isActiveForTrayMenu() {
	updateIsActive();
	return Platform::IsWayland() ? isVisible() : isActive();
}

void MainWindow::psShowTrayMenu() {
	_trayIconMenuXEmbed->popup(QCursor::pos());
}

void MainWindow::psTrayMenuUpdated() {
}

void MainWindow::psSetupTrayIcon() {
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();

	if (_sniAvailable) {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
		LOG(("Using SNI tray icon."));
		if (!_private->sniTrayIcon) {
			_private->sniTrayIcon = new StatusNotifierItem(
				QCoreApplication::applicationName(),
				this);

			_private->sniTrayIcon->setTitle(AppName.utf16());
			_private->sniTrayIcon->setContextMenu(trayIconMenu);
			_private->setSNITrayIcon(counter, muted);

			_private->attachToSNITrayIcon();
		}
		updateIconCounters();
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	} else {
		LOG(("Using Qt tray icon."));
		if (!trayIcon) {
			trayIcon = new QSystemTrayIcon(this);
			trayIcon->setIcon(TrayIconGen(counter, muted));

			attachToTrayIcon(trayIcon);
		}
		updateIconCounters();

		trayIcon->show();
	}
}

void MainWindow::workmodeUpdated(Core::Settings::WorkMode mode) {
	if (!trayAvailable()) {
		return;
	} else if (mode == WorkMode::WindowOnly) {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
		if (_private->sniTrayIcon) {
			_private->sniTrayIcon->setContextMenu(0);
			_private->sniTrayIcon->deleteLater();
		}
		_private->sniTrayIcon = nullptr;
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

		if (trayIcon) {
			trayIcon->setContextMenu(0);
			trayIcon->deleteLater();
		}
		trayIcon = nullptr;
	} else {
		psSetupTrayIcon();
	}

	SkipTaskbar(windowHandle(), mode == WorkMode::TrayOnly);
}

void MainWindow::unreadCounterChangedHook() {
	setWindowTitle(titleText());
	updateIconCounters();
}

void MainWindow::updateIconCounters() {
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();

	updateWindowIcon();

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	if (UseUnityCounter()) {
		const auto launcherUrl = Glib::ustring(
			"application://"
				+ QGuiApplication::desktopFileName().toStdString());
		const auto counterSlice = std::min(counter, 9999);
		std::map<Glib::ustring, Glib::VariantBase> dbusUnityProperties;

		if (counterSlice > 0) {
			// According to the spec, it should be of 'x' D-Bus signature,
			// which corresponds to gint64 type with glib
			// https://wiki.ubuntu.com/Unity/LauncherAPI#Low_level_DBus_API:_com.canonical.Unity.LauncherEntry
			dbusUnityProperties["count"] = Glib::Variant<gint64>::create(
				counterSlice);
			dbusUnityProperties["count-visible"] =
				Glib::Variant<bool>::create(true);
		} else {
			dbusUnityProperties["count-visible"] =
				Glib::Variant<bool>::create(false);
		}

		try {
			if (_private->dbusConnection) {
				_private->dbusConnection->emit_signal(
					"/com/canonical/unity/launcherentry/"
						+ std::to_string(djbStringHash(launcherUrl)),
					"com.canonical.Unity.LauncherEntry",
					"Update",
					{},
					base::Platform::MakeGlibVariant(std::tuple{
						launcherUrl,
						dbusUnityProperties,
					}));
			}
		} catch (...) {
		}
	}

	if (_private->sniTrayIcon) {
		_private->setSNITrayIcon(counter, muted);
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	if (trayIcon && IsIconRegenerationNeeded(counter, muted)) {
		trayIcon->setIcon(TrayIconGen(counter, muted));
	}
}

void MainWindow::initTrayMenuHook() {
	_trayIconMenuXEmbed.emplace(nullptr, trayIconMenu);
	_trayIconMenuXEmbed->deleteOnHide(false);
}

void MainWindow::createGlobalMenu() {
	const auto ensureWindowShown = [=] {
		if (isHidden()) {
			showFromTray();
		}
	};

	psMainMenu = new QMenu(this);

	auto file = psMainMenu->addMenu(tr::lng_mac_menu_file(tr::now));

	psLogout = file->addAction(
		tr::lng_mac_menu_logout(tr::now),
		this,
		[=] {
			ensureWindowShown();
			controller().showLogoutConfirmation();
		});

	auto quit = file->addAction(
		tr::lng_mac_menu_quit_telegram(tr::now, lt_telegram, qsl("Telegram")),
		this,
		[=] { quitFromTray(); },
		QKeySequence::Quit);

	quit->setMenuRole(QAction::QuitRole);

	auto edit = psMainMenu->addMenu(tr::lng_mac_menu_edit(tr::now));

	psUndo = edit->addAction(
		tr::lng_linux_menu_undo(tr::now),
		this,
		[=] { psLinuxUndo(); },
		QKeySequence::Undo);

	psRedo = edit->addAction(
		tr::lng_linux_menu_redo(tr::now),
		this,
		[=] { psLinuxRedo(); },
		QKeySequence::Redo);

	edit->addSeparator();

	psCut = edit->addAction(
		tr::lng_mac_menu_cut(tr::now),
		this,
		[=] { psLinuxCut(); },
		QKeySequence::Cut);
	psCopy = edit->addAction(
		tr::lng_mac_menu_copy(tr::now),
		this,
		[=] { psLinuxCopy(); },
		QKeySequence::Copy);

	psPaste = edit->addAction(
		tr::lng_mac_menu_paste(tr::now),
		this,
		[=] { psLinuxPaste(); },
		QKeySequence::Paste);

	psDelete = edit->addAction(
		tr::lng_mac_menu_delete(tr::now),
		this,
		[=] { psLinuxDelete(); },
		QKeySequence(Qt::ControlModifier | Qt::Key_Backspace));

	edit->addSeparator();

	psBold = edit->addAction(
		tr::lng_menu_formatting_bold(tr::now),
		this,
		[=] { psLinuxBold(); },
		QKeySequence::Bold);

	psItalic = edit->addAction(
		tr::lng_menu_formatting_italic(tr::now),
		this,
		[=] { psLinuxItalic(); },
		QKeySequence::Italic);

	psUnderline = edit->addAction(
		tr::lng_menu_formatting_underline(tr::now),
		this,
		[=] { psLinuxUnderline(); },
		QKeySequence::Underline);

	psStrikeOut = edit->addAction(
		tr::lng_menu_formatting_strike_out(tr::now),
		this,
		[=] { psLinuxStrikeOut(); },
		Ui::kStrikeOutSequence);

	psMonospace = edit->addAction(
		tr::lng_menu_formatting_monospace(tr::now),
		this,
		[=] { psLinuxMonospace(); },
		Ui::kMonospaceSequence);

	psClearFormat = edit->addAction(
		tr::lng_menu_formatting_clear(tr::now),
		this,
		[=] { psLinuxClearFormat(); },
		Ui::kClearFormatSequence);

	edit->addSeparator();

	psSelectAll = edit->addAction(
		tr::lng_mac_menu_select_all(tr::now),
		this, [=] { psLinuxSelectAll(); },
		QKeySequence::SelectAll);

	edit->addSeparator();

	auto prefs = edit->addAction(
		tr::lng_mac_menu_preferences(tr::now),
		this,
		[=] {
			ensureWindowShown();
			controller().showSettings();
		},
		QKeySequence(Qt::ControlModifier | Qt::Key_Comma));

	prefs->setMenuRole(QAction::PreferencesRole);

	auto tools = psMainMenu->addMenu(tr::lng_linux_menu_tools(tr::now));

	psContacts = tools->addAction(
		tr::lng_mac_menu_contacts(tr::now),
		crl::guard(this, [=] {
			if (isHidden()) {
				showFromTray();
			}

			if (!sessionController()) {
				return;
			}

			sessionController()->show(
				PrepareContactsBox(sessionController()));
		}));

	psAddContact = tools->addAction(
		tr::lng_mac_menu_add_contact(tr::now),
		this,
		[=] {
			Expects(sessionController() != nullptr);
			ensureWindowShown();
			sessionController()->showAddContact();
		});

	tools->addSeparator();

	psNewGroup = tools->addAction(
		tr::lng_mac_menu_new_group(tr::now),
		this,
		[=] {
			Expects(sessionController() != nullptr);
			ensureWindowShown();
			sessionController()->showNewGroup();
		});

	psNewChannel = tools->addAction(
		tr::lng_mac_menu_new_channel(tr::now),
		this,
		[=] {
			Expects(sessionController() != nullptr);
			ensureWindowShown();
			sessionController()->showNewChannel();
		});

	auto help = psMainMenu->addMenu(tr::lng_linux_menu_help(tr::now));

	auto about = help->addAction(
		tr::lng_mac_menu_about_telegram(
			tr::now,
			lt_telegram,
			qsl("Telegram")),
		[=] {
			ensureWindowShown();
			controller().show(Box<AboutBox>());
		});

	about->setMenuRole(QAction::AboutQtRole);

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	_private->mainMenuExporter = new DBusMenuExporter(
		kMainMenuObjectPath.utf16(),
		psMainMenu);

	if (_private->appMenuSupported) {
		RegisterAppMenu(windowHandle(), kMainMenuObjectPath.utf16());
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	updateGlobalMenu();
}

void MainWindow::psLinuxUndo() {
	SendKeySequence(Qt::Key_Z, Qt::ControlModifier);
}

void MainWindow::psLinuxRedo() {
	SendKeySequence(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
}

void MainWindow::psLinuxCut() {
	SendKeySequence(Qt::Key_X, Qt::ControlModifier);
}

void MainWindow::psLinuxCopy() {
	SendKeySequence(Qt::Key_C, Qt::ControlModifier);
}

void MainWindow::psLinuxPaste() {
	SendKeySequence(Qt::Key_V, Qt::ControlModifier);
}

void MainWindow::psLinuxDelete() {
	SendKeySequence(Qt::Key_Delete);
}

void MainWindow::psLinuxSelectAll() {
	SendKeySequence(Qt::Key_A, Qt::ControlModifier);
}

void MainWindow::psLinuxBold() {
	SendKeySequence(Qt::Key_B, Qt::ControlModifier);
}

void MainWindow::psLinuxItalic() {
	SendKeySequence(Qt::Key_I, Qt::ControlModifier);
}

void MainWindow::psLinuxUnderline() {
	SendKeySequence(Qt::Key_U, Qt::ControlModifier);
}

void MainWindow::psLinuxStrikeOut() {
	SendKeySequence(Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
}

void MainWindow::psLinuxMonospace() {
	SendKeySequence(Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
}

void MainWindow::psLinuxClearFormat() {
	SendKeySequence(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
}

void MainWindow::updateGlobalMenuHook() {
	if (!positionInited()) {
		return;
	}

	const auto focused = QApplication::focusWidget();
	auto canUndo = false;
	auto canRedo = false;
	auto canCut = false;
	auto canCopy = false;
	auto canPaste = false;
	auto canDelete = false;
	auto canSelectAll = false;
	const auto clipboardHasText = QGuiApplication::clipboard()
		->ownsClipboard();
	auto markdownEnabled = false;
	if (const auto edit = qobject_cast<QLineEdit*>(focused)) {
		canCut = canCopy = canDelete = edit->hasSelectedText();
		canSelectAll = !edit->text().isEmpty();
		canUndo = edit->isUndoAvailable();
		canRedo = edit->isRedoAvailable();
		canPaste = clipboardHasText;
	} else if (const auto edit = qobject_cast<QTextEdit*>(focused)) {
		canCut = canCopy = canDelete = edit->textCursor().hasSelection();
		canSelectAll = !edit->document()->isEmpty();
		canUndo = edit->document()->isUndoAvailable();
		canRedo = edit->document()->isRedoAvailable();
		canPaste = clipboardHasText;
		if (canCopy) {
			if (const auto inputField = qobject_cast<Ui::InputField*>(
				focused->parentWidget())) {
				markdownEnabled = inputField->isMarkdownEnabled();
			}
		}
	} else if (const auto list = qobject_cast<HistoryInner*>(focused)) {
		canCopy = list->canCopySelected();
		canDelete = list->canDeleteSelected();
	}
	updateIsActive();
	const auto logged = (sessionController() != nullptr);
	const auto inactive = !logged || controller().locked();
	const auto support = logged && account().session().supportMode();
	ForceDisabled(psLogout, !logged && !Core::App().passcodeLocked());
	ForceDisabled(psUndo, !canUndo);
	ForceDisabled(psRedo, !canRedo);
	ForceDisabled(psCut, !canCut);
	ForceDisabled(psCopy, !canCopy);
	ForceDisabled(psPaste, !canPaste);
	ForceDisabled(psDelete, !canDelete);
	ForceDisabled(psSelectAll, !canSelectAll);
	ForceDisabled(psContacts, inactive || support);
	ForceDisabled(psAddContact, inactive);
	ForceDisabled(psNewGroup, inactive || support);
	ForceDisabled(psNewChannel, inactive || support);

	ForceDisabled(psBold, !markdownEnabled);
	ForceDisabled(psItalic, !markdownEnabled);
	ForceDisabled(psUnderline, !markdownEnabled);
	ForceDisabled(psStrikeOut, !markdownEnabled);
	ForceDisabled(psMonospace, !markdownEnabled);
	ForceDisabled(psClearFormat, !markdownEnabled);
}

void MainWindow::handleNativeSurfaceChanged(bool exist) {
	if (exist) {
		SkipTaskbar(
			windowHandle(),
			(Core::App().settings().workMode() == WorkMode::TrayOnly)
				&& trayAvailable());
	}

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	if (_private->appMenuSupported && _private->mainMenuExporter) {
		if (exist) {
			RegisterAppMenu(windowHandle(), kMainMenuObjectPath.utf16());
		} else {
			UnregisterAppMenu(windowHandle());
		}
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
}

MainWindow::~MainWindow() {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	if (_private->dbusConnection) {
		if (_private->sniRegisteredSignalId != 0) {
			_private->dbusConnection->signal_unsubscribe(
				_private->sniRegisteredSignalId);
		}

		if (_private->sniWatcherId != 0) {
			_private->dbusConnection->signal_unsubscribe(
				_private->sniWatcherId);
		}

		if (_private->appMenuWatcherId != 0) {
			_private->dbusConnection->signal_unsubscribe(
				_private->appMenuWatcherId);
		}
	}

	if (_private->appMenuSupported) {
		UnregisterAppMenu(windowHandle());
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
}

} // namespace Platform
