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

inline bool AutostartSupported() {
	return false;
}

inline void AutostartRequestStateFromSystem(Fn<void(bool)> callback) {
}

inline bool TrayIconSupported() {
	return true;
}

inline bool SkipTaskbarSupported() {
	return false;
}

void ActivateThisProcess();

inline uint64 ActivationWindowId(not_null<QWidget*> window) {
	return 1;
}

inline void ActivateOtherProcess(uint64 processId, uint64 windowId) {
}

inline QString ExecutablePathForShortcuts() {
	return cExeDir() + cExeName();
}

namespace ThirdParty {

inline void start() {
}

} // namespace ThirdParty
} // namespace Platform

inline void psCheckLocalSocket(const QString &serverName) {
	QFile address(serverName);
	if (address.exists()) {
		address.remove();
	}
}

QString psAppDataPath();
void psSendToMenu(bool send, bool silent = false);

int psCleanup();
int psFixPrevious();

void psDownloadPathEnableAccess();
QByteArray psDownloadPathBookmark(const QString &path);
QByteArray psPathBookmark(const QString &path);

bool psLaunchMaps(const Data::LocationPoint &point);
