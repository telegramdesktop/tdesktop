/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/mac/specific_mac_p.h"

namespace Platform {

inline bool TranslucentWindowsSupported(QPoint globalPosition) {
	return true;
}

QString CurrentExecutablePath(int argc, char *argv[]);

namespace ThirdParty {

inline void start() {
}

inline void finish() {
}

} // namespace ThirdParty
} // namespace Platform

inline QString psServerPrefix() {
#ifndef OS_MAC_STORE
    return qsl("/tmp/");
#else // OS_MAC_STORE
	return objc_documentsPath();
#endif // OS_MAC_STORE
}
inline void psCheckLocalSocket(const QString &serverName) {
    QFile address(serverName);
	if (address.exists()) {
		address.remove();
	}
}

void psWriteDump();

void psDeleteDir(const QString &dir);

void psUserActionDone();
bool psIdleSupported();
TimeMs psIdleTime();

QStringList psInitLogs();
void psClearInitLogs();

void psActivateProcess(uint64 pid = 0);
QString psLocalServerPrefix();
QString psAppDataPath();
QString psDownloadPath();
void psAutoStart(bool start, bool silent = false);
void psSendToMenu(bool send, bool silent = false);

QRect psDesktopRect();
void psShowOverAll(QWidget *w, bool canFocus = true);
void psBringToBack(QWidget *w);

int psCleanup();
int psFixPrevious();

bool psShowOpenWithMenu(int x, int y, const QString &file);

QAbstractNativeEventFilter *psNativeEventFilter();

void psNewVersion();

void psUpdateOverlayed(QWidget *widget);

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

bool psLaunchMaps(const LocationCoords &coords);
