/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/main_window_linux.h"

#include "styles/style_window.h"
#include "platform/linux/linux_libs.h"
#include "platform/linux/specific_linux.h"
#include "platform/linux/linux_desktop_environment.h"
#include "platform/platform_notifications_manager.h"
#include "history/history.h"
#include "mainwindow.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "facades.h"
#include "app.h"

#include <QtCore/QSize>

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
#include <QtDBus>
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

namespace Platform {
namespace {

constexpr auto kDisableTrayCounter = "TDESKTOP_DISABLE_TRAY_COUNTER"_cs;
constexpr auto kTrayIconName = "telegram"_cs;
constexpr auto kPanelTrayIconName = "telegram-panel"_cs;
constexpr auto kMutePanelTrayIconName = "telegram-mute-panel"_cs;
constexpr auto kAttentionPanelTrayIconName = "telegram-attention-panel"_cs;
constexpr auto kSNIWatcherService = "org.kde.StatusNotifierWatcher"_cs;
constexpr auto kTrayIconFilename = "tdesktop-trayicon-XXXXXX.png"_cs;

bool TrayIconMuted = true;
int32 TrayIconCount = 0;
base::flat_map<int, QImage> TrayIconImageBack;
QIcon TrayIcon;
QString TrayIconThemeName, TrayIconName;

QString GetPanelIconName() {
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();

	return (counter > 0)
		? (muted
			? kMutePanelTrayIconName.utf16()
			: kAttentionPanelTrayIconName.utf16())
		: kPanelTrayIconName.utf16();
}

QString GetTrayIconName() {
	const auto panelIconName = GetPanelIconName();

	if (QIcon::hasThemeIcon(panelIconName)) {
		return panelIconName;
	} else if (InSandbox()) {
		const auto launcherBasename = GetLauncherBasename();

		if (QIcon::hasThemeIcon(launcherBasename)) {
			return launcherBasename;
		}
	} else if (QIcon::hasThemeIcon(kTrayIconName.utf16())) {
		return kTrayIconName.utf16();
	}

	return QString();
}

QIcon TrayIconGen() {
	const auto iconThemeName = QIcon::themeName();
	const auto iconName = GetTrayIconName();

	if (qEnvironmentVariableIsSet(kDisableTrayCounter.utf8())
		&& !iconName.isEmpty()) {
		if (TrayIcon.isNull()
			|| iconThemeName != TrayIconThemeName
			|| iconName != TrayIconName) {
			TrayIcon = QIcon::fromTheme(iconName);
			TrayIconThemeName = iconThemeName;
			TrayIconName = iconName;
		}

		return TrayIcon;
	}

	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();
	const auto counterSlice = (counter >= 1000)
		? (1000 + (counter % 100))
		: counter;

	if (TrayIcon.isNull()
		|| iconThemeName != TrayIconThemeName
		|| iconName != TrayIconName
		|| muted != TrayIconMuted
		|| counterSlice != TrayIconCount) {
		QIcon result;
		QIcon systemIcon;

		const auto iconSizes = {
			16,
			22,
			24,
			32,
			48,
			64
		};

		for (const auto iconSize : iconSizes) {
			auto &currentImageBack = TrayIconImageBack[iconSize];
			const auto desiredSize = QSize(iconSize, iconSize);

			if (currentImageBack.isNull()
				|| iconThemeName != TrayIconThemeName
				|| iconName != TrayIconName) {
				if (!iconName.isEmpty()) {
					if(systemIcon.isNull()) {
						systemIcon = QIcon::fromTheme(iconName);
					}

					if (systemIcon.actualSize(desiredSize) == desiredSize) {
						currentImageBack = systemIcon
							.pixmap(desiredSize)
							.toImage();
					} else {
						const auto availableSizes = systemIcon
							.availableSizes();

						const auto biggestSize = ranges::max_element(
							availableSizes,
							std::less<>(),
							&QSize::width);

						currentImageBack = systemIcon
							.pixmap(*biggestSize)
							.toImage();
					}
				} else {
					currentImageBack = Core::App().logo();
				}

				if (currentImageBack.size() != desiredSize) {
					currentImageBack = currentImageBack.scaled(
						desiredSize,
						Qt::IgnoreAspectRatio,
						Qt::SmoothTransformation);
				}
			}

			auto iconImage = currentImageBack;
			TrayIconMuted = muted;
			TrayIconCount = counterSlice;
			TrayIconThemeName = iconThemeName;
			TrayIconName = iconName;

			if (!qEnvironmentVariableIsSet(kDisableTrayCounter.utf8())
				&& counter > 0) {
				QPainter p(&iconImage);
				int32 layerSize = -16;

				if (iconSize >= 48) {
					layerSize = -32;
				} else if (iconSize >= 36) {
					layerSize = -24;
				} else if (iconSize >= 32) {
					layerSize = -20;
				}

				auto &bg = muted
					? st::trayCounterBgMute
					: st::trayCounterBg;

				auto &fg = st::trayCounterFg;

				auto layer = App::wnd()->iconWithCounter(
					layerSize,
					counter,
					bg,
					fg,
					false);

				p.drawImage(
					iconImage.width() - layer.width() - 1,
					iconImage.height() - layer.height() - 1,
					layer);
			}

			result.addPixmap(App::pixmapFromImageInPlace(
				std::move(iconImage)));
		}

		TrayIcon = result;
	}

	return TrayIcon;
}

bool IsAppIndicator() {
#ifdef TDESKTOP_DISABLE_DBUS_INTEGRATION
	static const auto AppIndicator = false;
#else // TDESKTOP_DISABLE_DBUS_INTEGRATION
	static const auto AppIndicator = QDBusInterface(
		qsl("com.canonical.indicator.application"),
		qsl("/com/canonical/indicator/application/service"),
		qsl("com.canonical.indicator.application.service")).isValid()
			|| QDBusInterface(
				qsl("org.ayatana.indicator.application"),
				qsl("/org/ayatana/indicator/application/service"),
				qsl("org.ayatana.indicator.application.service")).isValid();
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	return AppIndicator;
}

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
static bool NeedTrayIconFile() {
	// Hack for indicator-application, which doesn't handle icons sent across D-Bus:
	// save the icon to a temp file and set the icon name to that filename.
	static const auto TrayIconFileNeeded = IsAppIndicator();
	return TrayIconFileNeeded;
}

static inline QString TrayIconFileTemplate() {
	static const auto TempFileTemplate = AppRuntimeDirectory()
		+ kTrayIconFilename.utf16();
	return TempFileTemplate;
}

std::unique_ptr<QTemporaryFile> TrayIconFile(
		const QIcon &icon,
		int size,
		QObject *parent) {
	auto ret = std::make_unique<QTemporaryFile>(
		TrayIconFileTemplate(),
		parent);
	ret->open();
	icon.pixmap(size).save(ret.get());
	ret->close();
	return ret;
}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

bool IsSNIAvailable() {
	static const auto SNIAvailable = [&] {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
		QDBusInterface systrayHost(
			kSNIWatcherService.utf16(),
			qsl("/StatusNotifierWatcher"),
			kSNIWatcherService.utf16());

		return systrayHost.isValid()
			&& systrayHost
				.property("IsStatusNotifierHostRegistered")
				.toBool();
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

		return false;
	}();

	return SNIAvailable;
}

bool UseUnityCounter() {
#ifdef TDESKTOP_DISABLE_DBUS_INTEGRATION
	static const auto UnityCounter = false;
#else // TDESKTOP_DISABLE_DBUS_INTEGRATION
	static const auto UnityCounter = QDBusInterface(
		"com.canonical.Unity",
		"/").isValid();
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	return UnityCounter;
}

quint32 djbStringHash(QString string) {
	quint32 hash = 5381;
	QByteArray chars = string.toLatin1();
	for(int i = 0; i < chars.length(); i++){
		hash = (hash << 5) + hash + chars[i];
	}
	return hash;
}

} // namespace

MainWindow::MainWindow(not_null<Window::Controller*> controller)
: Window::MainWindow(controller) {
}

bool MainWindow::hasTrayIcon() const {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	return trayIcon || _sniTrayIcon;
#else
	return trayIcon;
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
}

void MainWindow::psShowTrayMenu() {
	if (!IsSNIAvailable()) {
		_trayIconMenuXEmbed->popup(QCursor::pos());
	}
}

void MainWindow::psTrayMenuUpdated() {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	if (IsSNIAvailable()) {
		if (_sniTrayIcon && trayIconMenu) {
			_sniTrayIcon->setContextMenu(trayIconMenu);
		}
	}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
}

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
void MainWindow::setSNITrayIcon(const QIcon &icon) {
	const auto iconName = GetTrayIconName();

	if (qEnvironmentVariableIsSet(kDisableTrayCounter.utf8())
		&& !iconName.isEmpty()) {
		_sniTrayIcon->setIconByName(iconName);
		_sniTrayIcon->setToolTipIconByName(iconName);
	} else if (NeedTrayIconFile()) {
		_trayIconFile = TrayIconFile(icon, 22, this);
		_trayToolTipIconFile = TrayIconFile(icon, 48, this);

		if (_trayIconFile) {
			_sniTrayIcon->setIconByName(_trayIconFile->fileName());
			_sniTrayIcon->setToolTipIconByName(
				_trayToolTipIconFile->fileName());
		}
	} else {
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
	updateTrayMenu();
}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

void MainWindow::psSetupTrayIcon() {
	if (IsSNIAvailable()) {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
		LOG(("Using SNI tray icon."));
		if (!_sniTrayIcon) {
			_sniTrayIcon = new StatusNotifierItem(
				QCoreApplication::applicationName(),
				this);

			_sniTrayIcon->setTitle(AppName.utf16());
			setSNITrayIcon(TrayIconGen());

			attachToSNITrayIcon();
		}
		updateIconCounters();
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
	} else {
		LOG(("Using Qt tray icon."));

		if (!_trayIconMenuXEmbed) {
			_trayIconMenuXEmbed = new Ui::PopupMenu(nullptr, trayIconMenu);
			_trayIconMenuXEmbed->deleteOnHide(false);
		}

		if (!trayIcon) {
			trayIcon = new QSystemTrayIcon(this);
			trayIcon->setIcon(TrayIconGen());

			attachToTrayIcon(trayIcon);
		}
		updateIconCounters();

		trayIcon->show();
	}
}

void MainWindow::workmodeUpdated(DBIWorkMode mode) {
	if (!cSupportTray()) return;

	if (mode == dbiwmWindowOnly) {
		if (IsSNIAvailable()) {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
			if (_sniTrayIcon) {
				_sniTrayIcon->setContextMenu(0);
				_sniTrayIcon->deleteLater();
			}
			_sniTrayIcon = 0;
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
		} else {
			if (trayIcon) {
				trayIcon->setContextMenu(0);
				trayIcon->deleteLater();
			}
			trayIcon = 0;
		}
	} else {
		psSetupTrayIcon();
	}
}

void MainWindow::unreadCounterChangedHook() {
	setWindowTitle(titleText());
	updateIconCounters();
}

void MainWindow::updateIconCounters() {
	updateWindowIcon();

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	if (UseUnityCounter()) {
		const auto counter = Core::App().unreadBadge();
		const auto launcherUrl = "application://" + GetLauncherFilename();
		QVariantMap dbusUnityProperties;
		if (counter > 0) {
			// Gnome requires that count is a 64bit integer
			dbusUnityProperties.insert(
				"count",
				(qint64) ((counter > 9999)
					? 9999
					: counter));
			dbusUnityProperties.insert("count-visible", true);
		} else {
			dbusUnityProperties.insert("count-visible", false);
		}
		QDBusMessage signal = QDBusMessage::createSignal(
			"/com/canonical/unity/launcherentry/"
				+ QString::number(djbStringHash(launcherUrl)),
			"com.canonical.Unity.LauncherEntry",
			"Update");
		signal << launcherUrl;
		signal << dbusUnityProperties;
		QDBusConnection::sessionBus().send(signal);
	}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	if (IsSNIAvailable()) {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
		if (_sniTrayIcon) {
			setSNITrayIcon(TrayIconGen());
		}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
	} else if (trayIcon) {
		trayIcon->setIcon(TrayIconGen());
	}
}

void MainWindow::LibsLoaded() {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	qDBusRegisterMetaType<ToolTip>();
	qDBusRegisterMetaType<IconPixmap>();
	qDBusRegisterMetaType<IconPixmapList>();
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
}

void MainWindow::initTrayMenuHook() {
	const auto trayAvailable = IsSNIAvailable()
		|| QSystemTrayIcon::isSystemTrayAvailable();

	LOG(("System tray available: %1").arg(Logs::b(trayAvailable)));
	cSetSupportTray(trayAvailable);

	if (UseUnityCounter()) {
		LOG(("Using Unity launcher counter."));
	} else {
		LOG(("Not using Unity launcher counter."));
	}
}

MainWindow::~MainWindow() {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	delete _sniTrayIcon;
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	delete _trayIconMenuXEmbed;
}

} // namespace Platform
