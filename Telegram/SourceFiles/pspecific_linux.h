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
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

inline QString psServerPrefix() {
    return qsl("/tmp/");
}
inline void psCheckLocalSocket(const QString &serverName) {
    QFile address(serverName);
	if (address.exists()) {
		address.remove();
	}
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
    void psPlatformNotify(HistoryItem *item);

	void psUpdateCounter();

	virtual QImage iconWithCounter(int size, int count, style::color bg, bool smallIcon) = 0;

	~PsMainWindow();

public slots:

	void psUpdateDelegate();
	void psSavePosition(Qt::WindowState state = Qt::WindowActive);
	void psShowTrayMenu();

    void psStatusIconCheck();
    void psUpdateIndicator();

protected:

    bool psHasTrayIcon() const;

	bool posInited;
    QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;
    QImage icon256, iconbig256;
	QIcon wndIcon;

    void psTrayMenuUpdated();
    void psSetupTrayIcon();

    QTimer psUpdatedPositionTimer;

private:
    void psCreateTrayIcon();

    QTimer _psCheckStatusIconTimer;
    int _psCheckStatusIconLeft;

    QTimer _psUpdateIndicatorTimer;
    uint64 _psLastIndicatorUpdate;
};


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

void psUserActionDone();
uint64 psIdleTime();
	
QStringList psInitLogs();
void psClearInitLogs();

void psActivateProcess(uint64 pid = 0);
QString psLocalServerPrefix();
QString psCurrentCountry();
QString psCurrentLanguage();
QString psAppDataPath();
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

bool psCheckReadyUpdate();
void psExecUpdater();
void psExecTelegram();

bool psShowOpenWithMenu(int x, int y, const QString &file);

void psPostprocessFile(const QString &name);
void psOpenFile(const QString &name, bool openWith = false);
void psShowInFolder(const QString &name);
void psStart();
void psFinish();

void psRegisterCustomScheme();

void psUpdateOverlayed(QWidget *widget);
inline QString psConvertFileUrl(const QString &url) {
	return url;
}
