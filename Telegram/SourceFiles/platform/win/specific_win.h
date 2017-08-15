/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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

void psExecUpdater();
void psExecTelegram(const QString &arg = QString());

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
