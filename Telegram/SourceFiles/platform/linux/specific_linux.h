/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_specific.h"

#include <execinfo.h>
#include <signal.h>

namespace Data {
class LocationPoint;
} // namespace Data

namespace Platform {

inline void SetWatchingMediaKeys(bool watching) {
}

bool IsApplicationActive();

inline void StartTranslucentPaint(QPainter &p, QPaintEvent *e) {
}

inline void InitOnTopPanel(QWidget *panel) {
}

inline void DeInitOnTopPanel(QWidget *panel) {
}

inline void ReInitOnTopPanel(QWidget *panel) {
}

QString CurrentExecutablePath(int argc, char *argv[]);

inline std::optional<crl::time> LastUserInputTime() {
	return std::nullopt;
}

inline constexpr bool UseMainQueueGeneric() {
	return true;
}

} // namespace Platform

inline QString psServerPrefix() {
    return qsl("/tmp/");
}
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
void psShowOverAll(QWidget *w, bool canFocus = true);
void psBringToBack(QWidget *w);

int psCleanup();
int psFixPrevious();

void psNewVersion();

void psUpdateOverlayed(QWidget *widget);
inline QByteArray psDownloadPathBookmark(const QString &path) {
	return QByteArray();
}
inline QByteArray psPathBookmark(const QString &path) {
	return QByteArray();
}
inline void psDownloadPathEnableAccess() {
}

class PsFileBookmark {
public:
	PsFileBookmark(const QByteArray &bookmark) {
	}
	bool check() const {
		return true;
	}
	bool enable() const {
		return true;
	}
	void disable() const {
	}
	const QString &name(const QString &original) const {
		return original;
	}
	QByteArray bookmark() const {
		return QByteArray();
	}

};

bool linuxMoveFile(const char *from, const char *to);

bool psLaunchMaps(const Data::LocationPoint &point);
