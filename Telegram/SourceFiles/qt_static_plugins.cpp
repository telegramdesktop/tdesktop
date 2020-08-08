/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <QtCore/QtPlugin>

#ifndef DESKTOP_APP_USE_PACKAGED
Q_IMPORT_PLUGIN(QWebpPlugin)

#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
Q_IMPORT_PLUGIN(QJpegPlugin)
Q_IMPORT_PLUGIN(QGifPlugin)
#endif // Qt 5.8.0

#ifdef Q_OS_WIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#elif defined Q_OS_MAC // Q_OS_WIN
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
Q_IMPORT_PLUGIN(QGenericEnginePlugin)
#elif defined Q_OS_UNIX // Q_OS_WIN | Q_OS_MAC
Q_IMPORT_PLUGIN(ShmServerBufferPlugin)
Q_IMPORT_PLUGIN(DmaBufServerBufferPlugin)
Q_IMPORT_PLUGIN(DrmEglServerBufferPlugin)
Q_IMPORT_PLUGIN(QWaylandEglClientBufferPlugin)
Q_IMPORT_PLUGIN(QWaylandIviShellIntegrationPlugin)
Q_IMPORT_PLUGIN(QWaylandWlShellIntegrationPlugin)
Q_IMPORT_PLUGIN(QWaylandXdgShellV5IntegrationPlugin)
Q_IMPORT_PLUGIN(QWaylandXdgShellV6IntegrationPlugin)
Q_IMPORT_PLUGIN(QWaylandXdgShellIntegrationPlugin)
Q_IMPORT_PLUGIN(QWaylandBradientDecorationPlugin)
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
Q_IMPORT_PLUGIN(QWaylandIntegrationPlugin)
Q_IMPORT_PLUGIN(QWaylandEglPlatformIntegrationPlugin)
Q_IMPORT_PLUGIN(QGenericEnginePlugin)
Q_IMPORT_PLUGIN(QComposePlatformInputContextPlugin)
Q_IMPORT_PLUGIN(QSvgPlugin)
Q_IMPORT_PLUGIN(QSvgIconPlugin)
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
Q_IMPORT_PLUGIN(QConnmanEnginePlugin)
Q_IMPORT_PLUGIN(QNetworkManagerEnginePlugin)
Q_IMPORT_PLUGIN(QIbusPlatformInputContextPlugin)
Q_IMPORT_PLUGIN(QXdgDesktopPortalThemePlugin)
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#endif // Q_OS_WIN | Q_OS_MAC | Q_OS_UNIX
#endif // !DESKTOP_APP_USE_PACKAGED

#if defined Q_OS_UNIX && !defined Q_OS_MAC
#if !defined DESKTOP_APP_USE_PACKAGED || defined DESKTOP_APP_USE_PACKAGED_LAZY
Q_IMPORT_PLUGIN(QWaylandMaterialDecorationPlugin)
Q_IMPORT_PLUGIN(NimfInputContextPlugin)
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
Q_IMPORT_PLUGIN(QFcitxPlatformInputContextPlugin)
Q_IMPORT_PLUGIN(QFcitx5PlatformInputContextPlugin)
Q_IMPORT_PLUGIN(QHimePlatformInputContextPlugin)
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#endif // !DESKTOP_APP_USE_PACKAGED || DESKTOP_APP_USE_PACKAGED_LAZY

#if !defined DESKTOP_APP_USE_PACKAGED || defined DESKTOP_APP_USE_PACKAGED_LAZY_PLATFORMTHEMES
Q_IMPORT_PLUGIN(Qt5CTPlatformThemePlugin)
Q_IMPORT_PLUGIN(Qt5CTStylePlugin)
#endif // !DESKTOP_APP_USE_PACKAGED || DESKTOP_APP_USE_PACKAGED_LAZY_PLATFORMTHEMES

// conflicts with Qt static link
#ifdef DESKTOP_APP_USE_PACKAGED_LAZY_PLATFORMTHEMES
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
Q_IMPORT_PLUGIN(LXQtPlatformThemePlugin)
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#endif // DESKTOP_APP_USE_PACKAGED_LAZY_PLATFORMTHEMES
#endif // Q_OS_UNIX && !Q_OS_MAC
