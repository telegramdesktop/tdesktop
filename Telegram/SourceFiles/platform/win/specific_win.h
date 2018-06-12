/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <windows.h>

namespace Platform {

inline void SetWatchingMediaKeys(bool watching) {
}

inline bool TranslucentWindowsSupported(QPoint globalPosition) {
	return true;
}

inline void StartTranslucentPaint(QPainter &p, QPaintEvent *e) {
}

inline void InitOnTopPanel(QWidget *panel) {
}

inline void DeInitOnTopPanel(QWidget *panel) {
}

inline void ReInitOnTopPanel(QWidget *panel) {
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
	return qsl("Global\\");
}
inline void psCheckLocalSocket(const QString &) {
}

void psWriteDump();
void psWriteStackTrace();
QString psPrepareCrashDump(const QByteArray &crashdump, QString dumpfile);

void psDeleteDir(const QString &dir);

void psUserActionDone();
bool psIdleSupported();
TimeMs psIdleTime();

QStringList psInitLogs();
void psClearInitLogs();

void psActivateProcess(uint64 pid = 0);
QString psLocalServerPrefix();
QString psAppDataPath();
QString psAppDataPathOld();
QString psDownloadPath();
void psAutoStart(bool start, bool silent = false);
void psSendToMenu(bool send, bool silent = false);

QRect psDesktopRect();
void psShowOverAll(QWidget *w, bool canFocus = true);
void psBringToBack(QWidget *w);

int psCleanup();
int psFixPrevious();

QAbstractNativeEventFilter *psNativeEventFilter();

void psNewVersion();

void psUpdateOverlayed(TWidget *widget);
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

bool psLaunchMaps(const LocationCoords &coords);
