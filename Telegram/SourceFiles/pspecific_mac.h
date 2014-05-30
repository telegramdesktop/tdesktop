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

/*
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

typedef QList<PsNotifyWindow*> PsNotifyWindows;*/

class PsMainWindow : public QMainWindow {
	Q_OBJECT

public:
	PsMainWindow(QWidget *parent = 0);

	int32 psResizeRowWidth() const {
		return 0;//st::wndResizeAreaWidth;
	}

	void psInitFrameless();
	void psInitSize();
    //HWND psHwnd() const;
    //HMENU psMenu() const;

	void psFirstShow();
	void psInitSysMenu();
	void psUpdateSysMenu(Qt::WindowState state);
	void psUpdateMargins();
	void psUpdatedPosition();

	bool psHandleTitle();

	void psFlash();

	bool psIsActive() const;
	bool psIsOnline(int windowState) const;

	void psUpdateWorkmode();

	void psRefreshTaskbarIcon();
	virtual bool minimizeToTray() {
		return false;
	}

    void psNotify(History *history);
	void psClearNotify(History *history = 0);
	void psClearNotifyFast();
    //void psShowNextNotify(PsNotifyWindow *remove = 0);
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

protected:

	bool posInited;
    QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;
    QImage icon16, icon32, icon256;
    virtual void setupTrayIcon() {
    }
/*
	typedef QSet<History*> NotifyHistories;
	NotifyHistories notifyHistories;
	PsNotifyWindows notifyWindows;

    QTimer psUpdatedPositionTimer;*/

/*private:
	HWND ps_hWnd;
	HWND ps_tbHider_hWnd;
	HMENU ps_menu;
	HICON ps_iconBig, ps_iconSmall, ps_iconOverlay;

	mutable bool psIdle;
	mutable QTimer psIdleTimer;

    void psDestroyIcons();*/
};


class PsApplication : public QApplication {
	Q_OBJECT

public:

	PsApplication(int argc, char *argv[]);
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
QString psCurrentExeDirectory();
void psAutoStart(bool start, bool silent = false);

int psCleanup();
int psFixPrevious();

bool psCheckReadyUpdate();
void psExecUpdater();
void psExecTelegram();

void psPostprocessFile(const QString &name);
void psOpenFile(const QString &name, bool openWith = false);
void psShowInFolder(const QString &name);
