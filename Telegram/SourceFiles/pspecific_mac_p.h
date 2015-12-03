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
 
Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#pragma once

class PsMacWindowData;

class PsMacWindowPrivate {
public:

	PsMacWindowPrivate();

    void setWindowBadge(const QString &str);
    void startBounce();

	void updateDelegate();
    
    void showNotify(uint64 peer, int32 msgId, const QPixmap &pix, const QString &title, const QString &subtitle, const QString &msg, bool withReply);
    void clearNotifies(uint64 peer = 0);
    
    void enableShadow(WId winId);

	bool filterNativeEvent(void *event);

    virtual void activeSpaceChanged() {
    }
	virtual void darkModeChanged() {
	}
    virtual void notifyClicked(unsigned long long peer, int msgid) {
    }
    virtual void notifyReplied(unsigned long long peer, int msgid, const char *str) {
    }
    
	~PsMacWindowPrivate();

    PsMacWindowData *data;
    
};

void objc_holdOnTop(WId winId);
bool objc_darkMode();
void objc_showOverAll(WId winId, bool canFocus = true);
void objc_bringToBack(WId winId);
void objc_activateWnd(WId winId);

void objc_debugShowAlert(const QString &str);
void objc_outputDebugString(const QString &str);
bool objc_idleSupported();
bool objc_idleTime(int64 &idleTime);

bool objc_showOpenWithMenu(int x, int y, const QString &file);

void objc_showInFinder(const QString &file, const QString &path);
void objc_openFile(const QString &file, bool openwith);
void objc_start();
void objc_finish();
bool objc_execUpdater();
void objc_execTelegram();

void objc_registerCustomScheme();

void objc_activateProgram(WId winId);
bool objc_moveFile(const QString &from, const QString &to);
void objc_deleteDir(const QString &dir);

QString objc_appDataPath();
QString objc_downloadPath();
QString objc_currentCountry();
QString objc_currentLang();
QString objc_convertFileUrl(const QString &url);
QByteArray objc_downloadPathBookmark(const QString &path);
QByteArray objc_pathBookmark(const QString &path);
void objc_downloadPathEnableAccess(const QByteArray &bookmark);

class objc_FileBookmark {
public:
	objc_FileBookmark(const QByteArray &bookmark);
	bool valid() const;
	bool enable() const;
	void disable() const;

	const QString &name(const QString &original) const;
	QByteArray bookmark() const;

	~objc_FileBookmark();

};
