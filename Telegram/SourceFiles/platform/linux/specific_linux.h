/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_specific.h"

namespace Data {
class LocationPoint;
} // namespace Data

namespace Platform {

inline void IgnoreApplicationActivationRightNow() {
}

inline void WriteCrashDumpDetails() {
}

inline void AutostartRequestStateFromSystem(Fn<void(bool)> callback) {
}

inline bool PreventsQuit(Core::QuitReason reason) {
	return false;
}

inline void ActivateThisProcess() {
}

inline uint64 ActivationWindowId(not_null<QWidget*> window) {
	return 1;
}

inline void ActivateOtherProcess(uint64 processId, uint64 windowId) {
}

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

inline QByteArray psDownloadPathBookmark(const QString &path) {
	return QByteArray();
}
inline void psDownloadPathEnableAccess() {
}

bool linuxMoveFile(const char *from, const char *to);

bool psLaunchMaps(const Data::LocationPoint &point);
