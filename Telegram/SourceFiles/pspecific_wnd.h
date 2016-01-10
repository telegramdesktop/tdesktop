
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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#pragma once

inline QString psServerPrefix() {
	return qsl("Global\\");
}
inline void psCheckLocalSocket(const QString &) {
}

class NotifyWindow;

class PsMainWindow : public QMainWindow {
	Q_OBJECT

public:
	PsMainWindow(QWidget *parent = 0);

	int32 psResizeRowWidth() const {
		return 0;//st::wndResizeAreaWidth;
	}

	void psInitFrameless();
	void psInitSize();
	HWND psHwnd() const;
	HMENU psMenu() const;

	void psFirstShow();
	void psInitSysMenu();
	void psUpdateSysMenu(Qt::WindowState state);
	void psUpdateMargins();
	void psUpdatedPosition();

	bool psHandleTitle();

	void psFlash();
	void psNotifySettingGot();

	void psUpdateWorkmode();

	void psRefreshTaskbarIcon();

	bool psPosInited() const {
		return posInited;
	}

	void psActivateNotify(NotifyWindow *w);
	void psClearNotifies(PeerId peerId = 0);
	void psNotifyShown(NotifyWindow *w);
	void psPlatformNotify(HistoryItem *item, int32 fwdCount);

	void psUpdateCounter();

	bool psHasNativeNotifications();
	void psCleanNotifyPhotosIn(int32 dt);

	virtual QImage iconWithCounter(int size, int count, style::color bg, bool smallIcon) = 0;

	~PsMainWindow();

public slots:

	void psUpdateDelegate();
	void psSavePosition(Qt::WindowState state = Qt::WindowActive);
    void psShowTrayMenu();

	void psCleanNotifyPhotos();

protected:

    bool psHasTrayIcon() const {
        return trayIcon;
    }

	bool posInited;
	QSystemTrayIcon *trayIcon;
	PopupMenu *trayIconMenu;
	QImage icon256, iconbig256;
	QIcon wndIcon;

    void psTrayMenuUpdated();
    void psSetupTrayIcon();

	QTimer psUpdatedPositionTimer;

private:
	HWND ps_hWnd;
	HWND ps_tbHider_hWnd;
	HMENU ps_menu;
	HICON ps_iconBig, ps_iconSmall, ps_iconOverlay;

	SingleTimer ps_cleanNotifyPhotosTimer;

	void psDestroyIcons();
};

extern LPTOP_LEVEL_EXCEPTION_FILTER _oldWndExceptionFilter;
LONG CALLBACK _exceptionFilter(EXCEPTION_POINTERS* pExceptionPointers);
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI RedirectedSetUnhandledExceptionFilter(_In_opt_ LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);

class PsApplication : public QApplication {
	Q_OBJECT

public:

	PsApplication(int &argc, char **argv);
	void psInstallEventFilter();
	~PsApplication();

};

void psDeleteDir(const QString &dir);

void psUserActionDone();
bool psIdleSupported();
uint64 psIdleTime();

bool psSkipAudioNotify();
bool psSkipDesktopNotify();

QStringList psInitLogs();
void psClearInitLogs();

void psActivateProcess(uint64 pid = 0);
QString psLocalServerPrefix();
QString psCurrentCountry();
QString psCurrentLanguage();
QString psAppDataPath();
QString psAppDataPathOld();
QString psDownloadPath();
QString psCurrentExeDirectory(int argc, char *argv[]);
QString psCurrentExeName(int argc, char *argv[]);
void psAutoStart(bool start, bool silent = false);
void psSendToMenu(bool send, bool silent = false);

QRect psDesktopRect();
void psShowOverAll(QWidget *w, bool canFocus = true);
void psBringToBack(QWidget *w);

int psCleanup();
int psFixPrevious();

void psExecUpdater();
void psExecTelegram();

bool psShowOpenWithMenu(int x, int y, const QString &file);

void psPostprocessFile(const QString &name);
void psOpenFile(const QString &name, bool openWith = false);
void psShowInFolder(const QString &name);
void psStart();
void psFinish();

void psNewVersion();

void psUpdateOverlayed(TWidget *widget);
inline QString psConvertFileUrl(const QString &url) {
	return url;
}
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
