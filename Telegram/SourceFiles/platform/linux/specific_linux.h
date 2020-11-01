/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_specific.h"

#include <signal.h>

namespace Data {
class LocationPoint;
} // namespace Data

namespace Platform {

inline void SetWatchingMediaKeys(bool watching) {
}

bool InFlatpak();
bool InSnap();
bool IsStaticBinary();
bool UseGtkIntegration();
bool IsGtkIntegrationForced();
bool AreQtPluginsBundled();

bool IsXDGDesktopPortalPresent();
bool UseXDGDesktopPortal();
bool CanOpenDirectoryWithPortal();

QString AppRuntimeDirectory();

QString GetLauncherBasename();
QString GetLauncherFilename();

QString GetIconName();

inline void IgnoreApplicationActivationRightNow() {
}

void InstallLauncher(bool force = false);

} // namespace Platform

inline void psCheckLocalSocket(const QString &serverName) {
	QFile address(serverName);
	if (address.exists()) {
		address.remove();
	}
}

void psWriteDump();

QStringList psInitLogs();
void psClearInitLogs();

void psActivateProcess(uint64 pid = 0);
QString psLocalServerPrefix();
QString psAppDataPath();
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

bool linuxMoveFile(const char *from, const char *to);

bool psLaunchMaps(const Data::LocationPoint &point);
