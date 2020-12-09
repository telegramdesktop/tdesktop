/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_specific.h"
#include "base/platform/win/base_windows_h.h"

namespace Data {
class LocationPoint;
} // namespace Data

namespace Platform {

inline void SetWatchingMediaKeys(bool watching) {
}

inline void IgnoreApplicationActivationRightNow() {
}

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

inline bool TrayIconSupported() {
	return true;
}

inline bool SkipTaskbarSupported() {
	return true;
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

void start();

inline void finish() {
}

} // namespace ThirdParty
} // namespace Platform

inline void psCheckLocalSocket(const QString &) {
}

void psWriteDump();

void psActivateProcess(uint64 pid = 0);
QString psAppDataPath();
QString psAppDataPathOld();
void psAutoStart(bool start, bool silent = false);
void psSendToMenu(bool send, bool silent = false);

QRect psDesktopRect();

int psCleanup();
int psFixPrevious();

void psNewVersion();

inline QByteArray psDownloadPathBookmark(const QString &path) {
	return QByteArray();
}
inline void psDownloadPathEnableAccess() {
}

bool psLaunchMaps(const Data::LocationPoint &point);
