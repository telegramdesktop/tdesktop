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

void RemoveQuarantine(const QString &path);

inline std::optional<bool> IsDarkMode() {
	return std::nullopt;
}

inline void FallbackFontConfigCheckBegin() {
}

inline void FallbackFontConfigCheckEnd() {
}

inline QImage GetImageFromClipboard() {
	return {};
}

inline bool StartSystemMove(QWindow *window) {
	return false;
}

inline bool StartSystemResize(QWindow *window, Qt::Edges edges) {
	return false;
}

inline bool AutostartSupported() {
	return false;
}

inline bool TrayIconSupported() {
	return true;
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

void psDeleteDir(const QString &dir);

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

bool psShowOpenWithMenu(int x, int y, const QString &file);

void psNewVersion();

void psDownloadPathEnableAccess();
QByteArray psDownloadPathBookmark(const QString &path);
QByteArray psPathBookmark(const QString &path);

class PsFileBookmark {
public:
	PsFileBookmark(const QByteArray &bookmark) : _inner(bookmark) {
	}
	bool check() const {
		return _inner.valid();
	}
	bool enable() const {
		return _inner.enable();
	}
	void disable() const {
		return _inner.disable();
	}
	const QString &name(const QString &original) const {
		return _inner.name(original);
	}
	QByteArray bookmark() const {
		return _inner.bookmark();
	}

private:
	objc_FileBookmark _inner;

};

QString strNotificationAboutThemeChange();
QString strNotificationAboutScreenLocked();
QString strNotificationAboutScreenUnlocked();
QString strStyleOfInterface();
QString strTitleWrapClass();
QString strTitleClass();

bool psLaunchMaps(const Data::LocationPoint &point);
