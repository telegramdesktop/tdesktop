/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

inline QString psServerPrefix() {
	return qsl("Global\\");
}
inline void psCheckLocalSocket(const QString &) {
}

class PsNotifyWindow : public QWidget, public Animated {
	Q_OBJECT

public:

	PsNotifyWindow(HistoryItem *item, int32 x, int32 y);

	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void paintEvent(QPaintEvent *e);

	bool animStep(float64 ms);
	void animHide(float64 duration, anim::transition func);
	void startHiding();
	void stopHiding();
	void moveTo(int32 x, int32 y, int32 index = -1);

	void updatePeerPhoto();

	int32 index() const {
		return history ? _index : -1;
	}

	~PsNotifyWindow();

public slots:

	void hideByTimer();
	void checkLastInput();

	void unlinkHistory(History *hist = 0);

private:

	DWORD started;

	History *history;
	IconedButton close;
	QPixmap pm;
	float64 alphaDuration, posDuration;
	QTimer hideTimer, inputTimer;
	bool hiding;
	int32 _index;
	anim::fvalue aOpacity;
	anim::transition aOpacityFunc;
	anim::ivalue aY;
	ImagePtr peerPhoto;

};

typedef QList<PsNotifyWindow*> PsNotifyWindows;

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

	bool psIsActive(int state = -1) const;
	bool psIsOnline(int windowState) const;

	void psUpdateWorkmode();

	void psRefreshTaskbarIcon();
	virtual bool minimizeToTray() {
		return false;
	}

	void psNotify(History *history, MsgId msgId);
	void psClearNotify(History *history = 0);
	void psClearNotifyFast();
	void psShowNextNotify(PsNotifyWindow *remove = 0);
	void psStopHiding();
	void psStartHiding();
	void psUpdateNotifies();

	bool psPosInited() const {
		return posInited;
	}

	~PsMainWindow();

public slots:

	void psStateChanged(Qt::WindowState state);
	void psUpdateCounter();
	void psSavePosition(Qt::WindowState state = Qt::WindowActive);
	void psIdleTimeout();
	void psNotifyFire();

protected:

	void psNotIdle() const;

	bool posInited;
	QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;
	QImage icon16, icon32, icon256;
	virtual void setupTrayIcon() {
	}

	typedef QMap<MsgId, uint64> NotifyWhenMap;
	typedef QMap<History*, NotifyWhenMap> NotifyWhenMaps;
	NotifyWhenMaps notifyWhenMaps;
	struct NotifyWaiter {
		NotifyWaiter(MsgId msg, uint64 when) : msg(msg), when(when) {
		}
		MsgId msg;
		uint64 when;
	};
	typedef QMap<History*, NotifyWaiter> NotifyWaiters;
	NotifyWaiters notifyWaiters;
	NotifyWaiters notifySettingWaiters;
	QTimer notifyWaitTimer;

	typedef QSet<uint64> NotifyWhenAlert;
	typedef QMap<History*, NotifyWhenAlert> NotifyWhenAlerts;
	NotifyWhenAlerts notifyWhenAlerts;

	PsNotifyWindows notifyWindows;

	QTimer psUpdatedPositionTimer;

private:
	HWND ps_hWnd;
	HWND ps_tbHider_hWnd;
	HMENU ps_menu;
	HICON ps_iconBig, ps_iconSmall, ps_iconOverlay;

	mutable bool psIdle;
	mutable QTimer psIdleTimer;

	void psDestroyIcons();
};

#ifdef _NEED_WIN_GENERATE_DUMP
extern LPTOP_LEVEL_EXCEPTION_FILTER _oldWndExceptionFilter;
LONG CALLBACK _exceptionFilter(EXCEPTION_POINTERS* pExceptionPointers);
#endif _NEED_WIN_GENERATE_DUMP

class PsApplication : public QApplication {
	Q_OBJECT

public:

	PsApplication(int &argc, char **argv);
	void psInstallEventFilter();
	~PsApplication();

signals:

	void updateChecking();
	void updateLatest();
	void updateDownloading(qint64 ready, qint64 total);
	void updateReady();
	void updateFailed();

};

class PsUpdateDownloader : public QObject {
	Q_OBJECT

public:
	PsUpdateDownloader(QThread *thread, const MTPDhelp_appUpdate &update);
	PsUpdateDownloader(QThread *thread, const QString &url);

	void unpackUpdate();

	int32 ready();
	int32 size();

	static void deleteDir(const QString &dir);
	static void clearAll();

	~PsUpdateDownloader();

public slots:

	void start();
	void partMetaGot();
	void partFinished(qint64 got, qint64 total);
	void partFailed(QNetworkReply::NetworkError e);
	void sendRequest();

private:
	void initOutput();

	void fatalFail();

	QString updateUrl;
	QNetworkAccessManager manager;
	QNetworkReply *reply;
	int32 already, full;
	QFile outputFile;

	QMutex mutex;

};

void psActivateProcess(uint64 pid);
QString psLocalServerPrefix();
QString psCurrentCountry();
QString psCurrentLanguage();
QString psAppDataPath();
QString psCurrentExeDirectory(int argc, char *argv[]);
void psAutoStart(bool start, bool silent = false);

int psCleanup();
int psFixPrevious();

bool psCheckReadyUpdate();
void psExecUpdater();
void psExecTelegram();

void psPostprocessFile(const QString &name);
void psOpenFile(const QString &name, bool openWith = false);
void psShowInFolder(const QString &name);
void psFinish();
