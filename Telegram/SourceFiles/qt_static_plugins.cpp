/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <QtCore/QtPlugin>

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
#elif defined Q_OS_LINUX // Q_OS_WIN | Q_OS_MAC
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
Q_IMPORT_PLUGIN(QGenericEnginePlugin)
Q_IMPORT_PLUGIN(QComposePlatformInputContextPlugin)
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
Q_IMPORT_PLUGIN(QConnmanEnginePlugin)
Q_IMPORT_PLUGIN(QNetworkManagerEnginePlugin)
Q_IMPORT_PLUGIN(QIbusPlatformInputContextPlugin)
Q_IMPORT_PLUGIN(QFcitxPlatformInputContextPlugin)
Q_IMPORT_PLUGIN(QHimePlatformInputContextPlugin)
Q_IMPORT_PLUGIN(NimfInputContextPlugin)
Q_IMPORT_PLUGIN(QXdgDesktopPortalThemePlugin)
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
#endif // Q_OS_WIN | Q_OS_MAC | Q_OS_LINUX
