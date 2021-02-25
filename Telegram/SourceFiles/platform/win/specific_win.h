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

inline QImage GetImageFromClipboard() {
	return {};
}

inline bool TrayIconSupported() {
	return true;
}

inline bool SkipTaskbarSupported() {
	return true;
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

int psCleanup();
int psFixPrevious();

void psNewVersion();

inline QByteArray psDownloadPathBookmark(const QString &path) {
	return QByteArray();
}
inline void psDownloadPathEnableAccess() {
}

bool psLaunchMaps(const Data::LocationPoint &point);
