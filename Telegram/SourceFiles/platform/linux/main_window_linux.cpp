/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/main_window_linux.h"

#include "styles/style_window.h"
#include "platform/linux/specific_linux.h"
#include "history/history.h"
#include "history/history_widget.h"
#include "history/history_inner_widget.h"
#include "main/main_account.h" // Account::sessionChanges.
#include "main/main_session.h"
#include "mainwindow.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/about_box.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "base/platform/base_platform_info.h"
#include "base/call_delayed.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/input_fields.h"
#include "facades.h"
#include "app.h"

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
bool XCBSkipTaskbar(QWindow *window, bool set) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return false;
	}

	const auto root = base::Platform::XCB::GetRootWindowFromQt();
	if (!root.has_value()) {
		return false;
	}

	const auto stateAtom = base::Platform::XCB::GetAtom(
		connection,
		"_NET_WM_STATE");

	if (!stateAtom.has_value()) {
		return false;
	}

	const auto skipTaskbarAtom = base::Platform::XCB::GetAtom(
		connection,
		"_NET_WM_STATE_SKIP_TASKBAR");

	if (!skipTaskbarAtom.has_value()) {
		return false;
	}

	xcb_client_message_event_t xev;
	xev.response_type = XCB_CLIENT_MESSAGE;
	xev.type = *stateAtom;
	xev.sequence = 0;
	xev.window = window->winId();
	xev.format = 32;
	xev.data.data32[0] = set ? 1 : 0;
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

	return true;
}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

bool SkipTaskbar(QWindow *window, bool set) {
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	if (IsX11()) {
		return XCBSkipTaskbar(window, set);
	}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

	return false;
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

		result.addPixmap(App::pixmapFromImageInPlace(
			std::move(iconImage)));
	}

	UpdateIconRegenerationNeeded(result, counter, muted, iconThemeName);

	return result;
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
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

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
			"org.freedesktop.DBus.Error.Disconnected",
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
void RegisterAppMenu(uint winId, const QString &menuPath) {
	auto message = QDBusMessage::createMethodCall(
		kAppMenuService.utf16(),
		kAppMenuObjectPath.utf16(),
		kAppMenuInterface.utf16(),
		qsl("RegisterWindow"));

	message.setArguments({
		winId,
		QVariant::fromValue(QDBusObjectPath(menuPath))
	});

	QDBusConnection::sessionBus().send(message);
}

// This call must be made from the same bus connection as DBusMenuExporter
// So it must use QDBusConnection
void UnregisterAppMenu(uint winId) {
	auto message = QDBusMessage::createMethodCall(
		kAppMenuService.utf16(),
		kAppMenuObjectPath.utf16(),
		kAppMenuInterface.utf16(),
		qsl("UnregisterWindow"));

	message.setArguments({
		winId
	});

	QDBusConnection::sessionBus().send(message);
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
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

} // namespace

class MainWindow::Private {
public:
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	Glib::RefPtr<Gio::DBus::Connection> dbusConnection;
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
};

MainWindow::MainWindow(not_null<Window::Controller*> controller)
: Window::MainWindow(controller)
, _private(std::make_unique<Private>()) {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	qDBusRegisterMetaType<ToolTip>();
	qDBusRegisterMetaType<IconPixmap>();
	qDBusRegisterMetaType<IconPixmapList>();
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
}

void MainWindow::initHook() {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	_sniAvailable = IsSNIAvailable();
	_appMenuSupported = IsAppMenuSupported();

	try {
		_private->dbusConnection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		_sniRegisteredSignalId = _private->dbusConnection->signal_subscribe(
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
							window->handleSNIHostRegistered();
						}
					});
				}
			},
			std::string(kSNIWatcherService),
			std::string(kSNIWatcherInterface),
			"StatusNotifierHostRegistered",
			std::string(kSNIWatcherObjectPath));

		_sniWatcherId = base::Platform::DBus::RegisterServiceWatcher(
			_private->dbusConnection,
			std::string(kSNIWatcherService),
			[=](
				const Glib::ustring &service,
				const Glib::ustring &oldOwner,
				const Glib::ustring &newOwner) {
				handleSNIOwnerChanged(
					QString::fromStdString(service),
					QString::fromStdString(oldOwner),
					QString::fromStdString(newOwner));
			});

		_appMenuWatcherId = base::Platform::DBus::RegisterServiceWatcher(
			_private->dbusConnection,
			std::string(kAppMenuService),
			[=](
				const Glib::ustring &service,
				const Glib::ustring &oldOwner,
				const Glib::ustring &newOwner) {
				handleAppMenuOwnerChanged(
					QString::fromStdString(service),
					QString::fromStdString(oldOwner),
					QString::fromStdString(newOwner));
			});
	} catch (...) {
	}

	if (_appMenuSupported) {
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

	LOG(("System tray available: %1").arg(Logs::b(trayAvailable())));
}

bool MainWindow::hasTrayIcon() const {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	return trayIcon || (_sniAvailable && _sniTrayIcon);
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

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
void MainWindow::setSNITrayIcon(int counter, bool muted) {
	const auto iconName = GetTrayIconName(counter, muted);
	const auto panelIconName = GetPanelIconName(counter, muted);

	if (iconName == panelIconName) {
		if (_sniTrayIcon->iconName() == iconName) {
			return;
		}

		_sniTrayIcon->setIconByName(iconName);
		_sniTrayIcon->setToolTipIconByName(iconName);
	} else if (IsIndicatorApplication()) {
		if (!IsIconRegenerationNeeded(counter, muted)
			&& _trayIconFile
			&& _sniTrayIcon->iconName() == _trayIconFile->fileName()) {
			return;
		}

		const auto icon = TrayIconGen(counter, muted);
		_trayIconFile = TrayIconFile(icon, this);

		if (_trayIconFile) {
			// indicator-application doesn't support tooltips
			_sniTrayIcon->setIconByName(_trayIconFile->fileName());
		}
	} else {
		if (!IsIconRegenerationNeeded(counter, muted)
			&& !_sniTrayIcon->iconPixmap().isEmpty()
			&& _sniTrayIcon->iconName().isEmpty()) {
			return;
		}

		const auto icon = TrayIconGen(counter, muted);
		_sniTrayIcon->setIconByPixmap(icon);
		_sniTrayIcon->setToolTipIconByPixmap(icon);
	}
}

void MainWindow::attachToSNITrayIcon() {
	_sniTrayIcon->setToolTipTitle(AppName.utf16());
	connect(_sniTrayIcon,
		&StatusNotifierItem::activateRequested,
		this,
		[=](const QPoint &) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				handleTrayIconActication(QSystemTrayIcon::Trigger);
			});
	});
	connect(_sniTrayIcon,
		&StatusNotifierItem::secondaryActivateRequested,
		this,
		[=](const QPoint &) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				handleTrayIconActication(QSystemTrayIcon::MiddleClick);
			});
	});
}

void MainWindow::handleSNIHostRegistered() {
	if (_sniAvailable) {
		return;
	}

	_sniAvailable = true;

	if (Global::WorkMode().value() == dbiwmWindowOnly) {
		return;
	}

	LOG(("Switching to SNI tray icon..."));

	if (trayIcon) {
		trayIcon->setContextMenu(nullptr);
		trayIcon->deleteLater();
	}
	trayIcon = nullptr;

	psSetupTrayIcon();

	SkipTaskbar(
		windowHandle(),
		Global::WorkMode().value() == dbiwmTrayOnly);
}

void MainWindow::handleSNIOwnerChanged(
		const QString &service,
		const QString &oldOwner,
		const QString &newOwner) {
	_sniAvailable = IsSNIAvailable();

	if (Global::WorkMode().value() == dbiwmWindowOnly) {
		return;
	}

	if (oldOwner.isEmpty() && !newOwner.isEmpty() && _sniAvailable) {
		LOG(("Switching to SNI tray icon..."));
	} else if (!oldOwner.isEmpty() && newOwner.isEmpty()) {
		LOG(("Switching to Qt tray icon..."));
	} else {
		return;
	}

	if (trayIcon) {
		trayIcon->setContextMenu(0);
		trayIcon->deleteLater();
	}
	trayIcon = nullptr;

	if (trayAvailable()) {
		psSetupTrayIcon();
	} else {
		LOG(("System tray is not available."));
	}

	SkipTaskbar(
		windowHandle(),
		(Global::WorkMode().value() == dbiwmTrayOnly) && trayAvailable());
}

void MainWindow::handleAppMenuOwnerChanged(
		const QString &service,
		const QString &oldOwner,
		const QString &newOwner) {
	if (oldOwner.isEmpty() && !newOwner.isEmpty()) {
		_appMenuSupported = true;
		LOG(("Using D-Bus global menu."));
	} else if (!oldOwner.isEmpty() && newOwner.isEmpty()) {
		_appMenuSupported = false;
		LOG(("Not using D-Bus global menu."));
	}

	if (_appMenuSupported && _mainMenuExporter) {
		RegisterAppMenu(winId(), kMainMenuObjectPath.utf16());
	} else {
		UnregisterAppMenu(winId());
	}
}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

void MainWindow::psSetupTrayIcon() {
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();

	if (_sniAvailable) {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
		LOG(("Using SNI tray icon."));
		if (!_sniTrayIcon) {
			_sniTrayIcon = new StatusNotifierItem(
				QCoreApplication::applicationName(),
				this);

			_sniTrayIcon->setTitle(AppName.utf16());
			_sniTrayIcon->setContextMenu(trayIconMenu);
			setSNITrayIcon(counter, muted);

			attachToSNITrayIcon();
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

void MainWindow::workmodeUpdated(DBIWorkMode mode) {
	if (!trayAvailable()) {
		return;
	} else if (mode == dbiwmWindowOnly) {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
		if (_sniTrayIcon) {
			_sniTrayIcon->setContextMenu(0);
			_sniTrayIcon->deleteLater();
		}
		_sniTrayIcon = nullptr;
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

		if (trayIcon) {
			trayIcon->setContextMenu(0);
			trayIcon->deleteLater();
		}
		trayIcon = nullptr;
	} else {
		psSetupTrayIcon();
	}

	SkipTaskbar(windowHandle(), mode == dbiwmTrayOnly);
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

	if (_sniTrayIcon) {
		setSNITrayIcon(counter, muted);
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

#ifdef DESKTOP_APP_DISABLE_DBUS_INTEGRATION

void MainWindow::createGlobalMenu() {
}

void MainWindow::updateGlobalMenuHook() {
}

#else // DESKTOP_APP_DISABLE_DBUS_INTEGRATION

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

			Ui::show(PrepareContactsBox(sessionController()));
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

	_mainMenuExporter = new DBusMenuExporter(
		kMainMenuObjectPath.utf16(),
		psMainMenu);

	if (_appMenuSupported) {
		RegisterAppMenu(winId(), kMainMenuObjectPath.utf16());
	}

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

#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

void MainWindow::handleVisibleChangedHook(bool visible) {
	if (visible) {
		base::call_delayed(1, this, [=] {
			SkipTaskbar(
				windowHandle(),
				(Global::WorkMode().value() == dbiwmTrayOnly)
					&& trayAvailable());
		});
	}

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	if (_appMenuSupported && _mainMenuExporter) {
		if (visible) {
			RegisterAppMenu(winId(), kMainMenuObjectPath.utf16());
		} else {
			UnregisterAppMenu(winId());
		}
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
}

MainWindow::~MainWindow() {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	if (_private->dbusConnection) {
		if (_sniRegisteredSignalId != 0) {
			_private->dbusConnection->signal_unsubscribe(
				_sniRegisteredSignalId);
		}

		if (_sniWatcherId != 0) {
			_private->dbusConnection->signal_unsubscribe(
				_sniWatcherId);
		}

		if (_appMenuWatcherId != 0) {
			_private->dbusConnection->signal_unsubscribe(
				_appMenuWatcherId);
		}
	}

	delete _sniTrayIcon;

	if (_appMenuSupported) {
		UnregisterAppMenu(winId());
	}

	delete _mainMenuExporter;
	delete psMainMenu;
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
}

} // namespace Platform
