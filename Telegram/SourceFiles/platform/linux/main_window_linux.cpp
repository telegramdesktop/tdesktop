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

int32 _trayIconSize = 48;
bool _trayIconMuted = true;
int32 _trayIconCount = 0;
QImage _trayIconImageBack, _trayIconImage;
QString _trayIconThemeName, _trayIconName;

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
bool UseUnityCount = false;
QString UnityCountDesktopFile;
QString UnityCountDBusPath = "/";
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

#define QT_RED 0
#define QT_GREEN 1
#define QT_BLUE 2
#define QT_ALPHA 3

QString GetTrayIconName() {
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();

	return (counter > 0)
		? (muted
			? kMutePanelTrayIconName.utf16()
			: kAttentionPanelTrayIconName.utf16())
		: kPanelTrayIconName.utf16();
}

QImage TrayIconImageGen() {
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();
	const auto counterSlice = (counter >= 1000)
		? (1000 + (counter % 100))
		: counter;

	const auto iconThemeName = QIcon::themeName();
	const auto iconName = GetTrayIconName();
	const auto desiredSize = QSize(_trayIconSize, _trayIconSize);

	if (_trayIconImage.isNull()
		|| _trayIconImage.size() != desiredSize
		|| iconThemeName != _trayIconThemeName
		|| iconName != _trayIconName
		|| muted != _trayIconMuted
		|| counterSlice != _trayIconCount) {
		if (_trayIconImageBack.isNull()
			|| _trayIconImageBack.size() != desiredSize
			|| iconThemeName != _trayIconThemeName
			|| iconName != _trayIconName) {
			const auto hasPanelIcon = QIcon::hasThemeIcon(iconName);

			if (hasPanelIcon || QIcon::hasThemeIcon(kTrayIconName.utf16())) {
				QIcon systemIcon;

				if (hasPanelIcon) {
					systemIcon = QIcon::fromTheme(iconName);
				} else {
					systemIcon = QIcon::fromTheme(kTrayIconName.utf16());
				}

				if (systemIcon.actualSize(desiredSize) == desiredSize) {
					_trayIconImageBack = systemIcon
						.pixmap(desiredSize)
						.toImage();
				} else {
					const auto biggestSize = systemIcon
						.availableSizes()
						.last();

					_trayIconImageBack = systemIcon
						.pixmap(biggestSize)
						.toImage();
				}
			} else {
				_trayIconImageBack = Core::App().logo();
			}

			if (_trayIconImageBack.size() != desiredSize) {
				_trayIconImageBack = _trayIconImageBack.scaled(
					desiredSize,
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);
			}

			_trayIconImageBack = _trayIconImageBack.convertToFormat(
				QImage::Format_ARGB32);

			const auto w = _trayIconImageBack.width();
			const auto h = _trayIconImageBack.height();
			const auto perline = _trayIconImageBack.bytesPerLine();
			auto *bytes = _trayIconImageBack.bits();

			for (int32 y = 0; y < h; ++y) {
				for (int32 x = 0; x < w; ++x) {
					int32 srcoff = y * perline + x * 4;
					bytes[srcoff + QT_RED  ] = qMax(
						bytes[srcoff + QT_RED  ],
						uchar(224));
					bytes[srcoff + QT_GREEN] = qMax(
						bytes[srcoff + QT_GREEN],
						uchar(165));
					bytes[srcoff + QT_BLUE ] = qMax(
						bytes[srcoff + QT_BLUE ],
						uchar(44));
				}
			}
		}

		_trayIconImage = _trayIconImageBack;
		_trayIconMuted = muted;
		_trayIconCount = counterSlice;
		_trayIconThemeName = iconThemeName;
		_trayIconName = iconName;

		if (counter > 0) {
			QPainter p(&_trayIconImage);
			int32 layerSize = -16;

			if (_trayIconSize >= 48) {
				layerSize = -32;
			} else if (_trayIconSize >= 36) {
				layerSize = -24;
			} else if (_trayIconSize >= 32) {
				layerSize = -20;
			}

			auto &bg = (muted ? st::trayCounterBgMute : st::trayCounterBg);
			auto &fg = st::trayCounterFg;

			auto layer = App::wnd()->iconWithCounter(
				layerSize,
				counter,
				bg,
				fg,
				false);

			p.drawImage(
				_trayIconImage.width() - layer.width() - 1,
				_trayIconImage.height() - layer.height() - 1,
				layer);
		}
	}

	return _trayIconImage;
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
	static const auto TrayIconFileNeeded = IsAppIndicator()
		// Ubuntu's tray extension doesn't zoom image data, but zooms image file
		|| DesktopEnvironment::IsGnome();

	return TrayIconFileNeeded;
}

static inline QString TrayIconFileTemplate() {
	static const auto TempFileTemplate = AppRuntimeDirectory()
		+ kTrayIconFilename.utf16();
	return TempFileTemplate;
}

std::unique_ptr<QTemporaryFile> TrayIconFile(
		const QImage &icon, QObject *parent) {
	auto ret = std::make_unique<QTemporaryFile>(
		TrayIconFileTemplate(),
		parent);
	ret->open();
	icon.save(ret.get());
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
void MainWindow::setSNITrayIcon(
		const QIcon &icon, const QImage &iconImage) {
	if (!NeedTrayIconFile()) {
		_sniTrayIcon->setIconByPixmap(icon);
		_sniTrayIcon->setToolTipIconByPixmap(icon);
	}

	if (qEnvironmentVariableIsSet(kDisableTrayCounter.utf8())) {
		const auto iconName = GetTrayIconName();
		_sniTrayIcon->setIconByName(iconName);
		_sniTrayIcon->setToolTipIconByName(iconName);
	} else if (NeedTrayIconFile()) {
		_trayIconFile = TrayIconFile(iconImage, this);

		if (_trayIconFile) {
			_sniTrayIcon->setIconByName(_trayIconFile->fileName());
			_sniTrayIcon->setToolTipIconByName(_trayIconFile->fileName());
		}
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
	const auto iconImage = TrayIconImageGen();
	const auto icon = QIcon(QPixmap::fromImage(iconImage));

	if (IsSNIAvailable()) {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
		LOG(("Using SNI tray icon."));
		if (!_sniTrayIcon) {
			_sniTrayIcon = new StatusNotifierItem(
				QCoreApplication::applicationName(),
				this);

			_sniTrayIcon->setTitle(AppName.utf16());
			setSNITrayIcon(icon, iconImage);

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
			trayIcon->setIcon(icon);

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
	if (UseUnityCount) {
		const auto counter = Core::App().unreadBadge();
		QVariantMap dbusUnityProperties;
		if (counter > 0) {
			// Gnome requires that count is a 64bit integer
			dbusUnityProperties.insert(
				"count",
				(qint64) ((counter > 9999)
					? 9999
					: (counter)));
			dbusUnityProperties.insert("count-visible", true);
		} else {
			dbusUnityProperties.insert("count-visible", false);
		}
		QDBusMessage signal = QDBusMessage::createSignal(
			UnityCountDBusPath,
			"com.canonical.Unity.LauncherEntry",
			"Update");
		signal << "application://" + UnityCountDesktopFile;
		signal << dbusUnityProperties;
		QDBusConnection::sessionBus().send(signal);
	}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	const auto iconImage = TrayIconImageGen();
	const auto icon = QIcon(QPixmap::fromImage(iconImage));

	if (IsSNIAvailable()) {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
		if (_sniTrayIcon) {
			setSNITrayIcon(icon, iconImage);
		}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
	} else if (trayIcon) {
		trayIcon->setIcon(icon);
	}
}

void MainWindow::LibsLoaded() {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	qDBusRegisterMetaType<ToolTip>();
	qDBusRegisterMetaType<IconPixmap>();
	qDBusRegisterMetaType<IconPixmapList>();
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	if (!IsSNIAvailable() || IsAppIndicator()) {
		_trayIconSize = 22;
	}
}

void MainWindow::initTrayMenuHook() {
	const auto trayAvailable = IsSNIAvailable()
		|| QSystemTrayIcon::isSystemTrayAvailable();

	LOG(("System tray available: %1").arg(Logs::b(trayAvailable)));
	cSetSupportTray(trayAvailable);

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	if (QDBusInterface("com.canonical.Unity", "/").isValid()) {
		const std::vector<QString> possibleDesktopFiles = {
			GetLauncherFilename(),
			"Telegram.desktop"
		};

		for (auto it = possibleDesktopFiles.begin();
			it != possibleDesktopFiles.end(); it++) {
			if (!QStandardPaths::locate(
				QStandardPaths::ApplicationsLocation, *it).isEmpty()) {
				UnityCountDesktopFile = *it;
				LOG(("Found Unity Launcher entry %1!")
					.arg(UnityCountDesktopFile));
				UseUnityCount = true;
				break;
			}
		}
		if (!UseUnityCount) {
			LOG(("Could not get Unity Launcher entry!"));
		}
		UnityCountDBusPath = "/com/canonical/unity/launcherentry/"
			+ QString::number(
				djbStringHash("application://" + UnityCountDesktopFile));
	} else {
		LOG(("Not using Unity Launcher count."));
	}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
}

MainWindow::~MainWindow() {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	delete _sniTrayIcon;
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	delete _trayIconMenuXEmbed;
}

} // namespace Platform
