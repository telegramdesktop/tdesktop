/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_specific.h"
#include "platform/mac/specific_mac_p.h"

namespace Data {
class LocationPoint;
} // namespace Data

namespace Platform {

[[nodiscard]] bool IsDarkMenuBar();

inline QImage GetClipboardImage() {
	return {};
}

inline bool SetClipboardImage(const QImage &image) {
	return false;
}

inline bool StartSystemMove(QWindow *window) {
	return false;
}

inline bool StartSystemResize(QWindow *window, Qt::Edges edges) {
	return false;
}

inline bool ShowWindowMenu(QWindow *window) {
	return false;
}

inline bool AutostartSupported() {
	return false;
}

inline bool TrayIconSupported() {
	return true;
}

inline bool SkipTaskbarSupported() {
	return false;
}

inline bool SetWindowExtents(QWindow *window, const QMargins &extents) {
	return false;
}

inline bool UnsetWindowExtents(QWindow *window) {
	return false;
}

inline bool WindowsNeedShadow() {
	return false;
}

namespace ThirdParty {

inline void start() {
}

inline void finish() {
}

} // namespace ThirdParty
} // namespace Platform

inline void psCheckLocalSocket(const QString &serverName) {
	QFile address(serverName);
	if (address.exists()) {
		address.remove();
	}
}

void psWriteDump();

void psActivateProcess(uint64 pid = 0);
QString psAppDataPath();
void psAutoStart(bool start, bool silent = false);
void psSendToMenu(bool send, bool silent = false);

QRect psDesktopRect();

int psCleanup();
int psFixPrevious();

void psNewVersion();

void psDownloadPathEnableAccess();
QByteArray psDownloadPathBookmark(const QString &path);
QByteArray psPathBookmark(const QString &path);

QString strNotificationAboutThemeChange();
QString strNotificationAboutScreenLocked();
QString strNotificationAboutScreenUnlocked();
QString strStyleOfInterface();
QString strTitleWrapClass();
QString strTitleClass();

bool psLaunchMaps(const Data::LocationPoint &point);
