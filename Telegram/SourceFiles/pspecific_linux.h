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

	bool psIsActive(int state = -1) const;
	bool psIsOnline(int state) const;

	void psUpdateWorkmode();

	void psRefreshTaskbarIcon();
	virtual bool minimizeToTray() {
		return false;
	}

	bool psPosInited() const {
		return posInited;
	}

    void psActivateNotify(NotifyWindow *w);
    void psClearNotifies(PeerId peerId = 0);
    void psNotifyShown(NotifyWindow *w);
    void psPlatformNotify(HistoryItem *item);

	~PsMainWindow();

public slots:

	void psStateChanged(Qt::WindowState state);
	void psUpdateCounter();
	void psSavePosition(Qt::WindowState state = Qt::WindowActive);
	void psIdleTimeout();

protected:

	void psNotIdle() const;

	bool posInited;
    QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;
    QImage icon256;
    virtual void setupTrayIcon() {
    }

    QTimer psUpdatedPositionTimer;

private:
	mutable bool psIdle;
	mutable QTimer psIdleTimer;
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

void psActivateProcess(uint64 pid);
QString psLocalServerPrefix();
QString psCurrentCountry();
QString psCurrentLanguage();
QString psAppDataPath();
QString psCurrentExeDirectory(int argc, char *argv[]);
void psAutoStart(bool start, bool silent = false);

QRect psDesktopRect();

int psCleanup();
int psFixPrevious();

bool psCheckReadyUpdate();
void psExecUpdater();
void psExecTelegram();

void psPostprocessFile(const QString &name);
void psOpenFile(const QString &name, bool openWith = false);
void psShowInFolder(const QString &name);
void psFinish();
