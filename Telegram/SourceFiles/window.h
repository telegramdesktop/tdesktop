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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "title.h"
#include "pspecific.h"
#include "gui/boxshadow.h"

class MediaView;
class TitleWidget;
class PasscodeWidget;
class IntroWidget;
class MainWidget;
class SettingsWidget;
class BackgroundWidget;
class LayeredWidget;
namespace Local {
	class ClearManager;
}

class ConnectingWidget : public QWidget {
	Q_OBJECT

public:

	ConnectingWidget(QWidget *parent, const QString &text, const QString &reconnect);
	void set(const QString &text, const QString &reconnect);
	void paintEvent(QPaintEvent *e);

public slots:

	void onReconnect();

private:

	BoxShadow _shadow;
	QString _text;
	int32 _textWidth;
	LinkButton _reconnect;

};

class NotifyWindow : public TWidget {
	Q_OBJECT

public:

	NotifyWindow(HistoryItem *item, int32 x, int32 y, int32 fwdCount);

	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void paintEvent(QPaintEvent *e);

	void step_appearance(float64 ms, bool timer);
	void animHide(float64 duration, anim::transition func);
	void startHiding();
	void stopHiding();
	void moveTo(int32 x, int32 y, int32 index = -1);

	void updateNotifyDisplay();
	void updatePeerPhoto();

	void itemRemoved(HistoryItem *del);

	int32 index() const {
		return history ? _index : -1;
	}

	void unlinkHistory(History *hist = 0);

	~NotifyWindow();

public slots:

	void hideByTimer();
	void checkLastInput();

	void unlinkHistoryAndNotify();

private:

#ifdef Q_OS_WIN
	DWORD started;
#endif
	History *history;
	HistoryItem *item;
	int32 fwdCount;
	IconedButton close;
	QPixmap pm;
	float64 alphaDuration, posDuration;
	QTimer hideTimer, inputTimer;
	bool hiding;
	int32 _index;
	anim::fvalue a_opacity;
	anim::transition a_func;
	anim::ivalue a_y;
	Animation _a_appearance;

	ImagePtr peerPhoto;

};

typedef QList<NotifyWindow*> NotifyWindows;

class Window : public PsMainWindow {
	Q_OBJECT

public:
	Window(QWidget *parent = 0);
	~Window();

	void init();
	void firstShow();

	QWidget *filedialogParent();

	bool eventFilter(QObject *obj, QEvent *evt);

	void inactivePress(bool inactive);
	bool inactivePress() const;

	void wStartDrag(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void closeEvent(QCloseEvent *e);

	void paintEvent(QPaintEvent *e);

	void resizeEvent(QResizeEvent *e);
	void updateAdaptiveLayout();
	bool needBackButton();

	void setupPasscode(bool anim);
	void clearPasscode();
	void checkAutoLockIn(int msec);
	void setupIntro(bool anim);
	void setupMain(bool anim, const MTPUser *user = 0);
	void getNotifySetting(const MTPInputNotifyPeer &peer, uint32 msWait = 0);
	void serviceNotification(const QString &msg, const MTPMessageMedia &media = MTP_messageMediaEmpty(), bool force = false);
	void sendServiceHistoryRequest();
	void showDelayedServiceMsgs();

	void mtpStateChanged(int32 dc, int32 state);

	TitleWidget *getTitle();

	HitTestType hitTest(const QPoint &p) const;
	QRect iconRect() const;

	QRect clientRect() const;
	QRect photoRect() const;

	IntroWidget *introWidget();
	MainWidget *mainWidget();
	SettingsWidget *settingsWidget();
	PasscodeWidget *passcodeWidget();

	void showConnecting(const QString &text, const QString &reconnect = QString());
	void hideConnecting();
	bool connectingVisible() const;

	void showPhoto(const PhotoLink *lnk, HistoryItem *item = 0);
	void showPhoto(PhotoData *photo, HistoryItem *item);
	void showPhoto(PhotoData *photo, PeerData *item);
	void showDocument(DocumentData *doc, HistoryItem *item);

	bool historyIsActive() const;

	void activate();

	void noIntro(IntroWidget *was);
	void noSettings(SettingsWidget *was);
	void noMain(MainWidget *was);
	void noBox(BackgroundWidget *was);
	void layerFinishedHide(BackgroundWidget *was);

	void fixOrder();

	enum TempDirState {
		TempDirRemoving,
		TempDirExists,
		TempDirEmpty,
	};
	TempDirState tempDirState();
	TempDirState localStorageState();
	void tempDirDelete(int task);

	void quit();

    void notifySettingGot();
	void notifySchedule(History *history, HistoryItem *item);
	void notifyClear(History *history = 0);
	void notifyClearFast();
	void notifyShowNext(NotifyWindow *remove = 0);
	void notifyItemRemoved(HistoryItem *item);
	void notifyStopHiding();
	void notifyStartHiding();
	void notifyUpdateAll();
	void notifyActivateAll();

    QImage iconLarge() const;

	void sendPaths();

	void mediaOverviewUpdated(PeerData *peer, MediaOverviewType type);
	void documentUpdated(DocumentData *doc);
	void changingMsgId(HistoryItem *row, MsgId newId);

	bool isActive(bool cached = true) const;
	void hideMediaview();

	bool contentOverlapped(const QRect &globalRect);
	bool contentOverlapped(QWidget *w, QPaintEvent *e) {
		return contentOverlapped(QRect(w->mapToGlobal(e->rect().topLeft()), e->rect().size()));
	}
	bool contentOverlapped(QWidget *w, const QRegion &r) {
		return contentOverlapped(QRect(w->mapToGlobal(r.boundingRect().topLeft()), r.boundingRect().size()));
	}

	void ui_showLayer(LayeredWidget *box, ShowLayerOptions options);
	bool ui_isLayerShown();
	bool ui_isMediaViewShown();

public slots:

	void updateIsActive(int timeout = 0);
	void stateChanged(Qt::WindowState state);

	void checkHistoryActivation();
	void updateCounter();

	void checkAutoLock();

	void showSettings();
	void hideSettings(bool fast = false);
	void layerHidden();
	void setInnerFocus();
	void updateTitleStatus();

	void quitFromTray();
	void showFromTray(QSystemTrayIcon::ActivationReason reason = QSystemTrayIcon::Unknown);
	bool minimizeToTray();
	void toggleTray(QSystemTrayIcon::ActivationReason reason = QSystemTrayIcon::Unknown);
	void toggleDisplayNotifyFromTray();

	void onInactiveTimer();

	void onClearFinished(int task, void *manager);
	void onClearFailed(int task, void *manager);

	void notifyFire();
	void updateTrayMenu(bool force = false);

	void onShowAddContact();
	void onShowNewGroup();
	void onShowNewChannel();
	void onLogout();
	void onLogoutSure();
	void updateGlobalMenu(); // for OS X top menu

	QImage iconWithCounter(int size, int count, style::color bg, bool smallIcon);

	void notifyUpdateAllPhotos();

signals:

	void resized(const QSize &size);
	void tempDirCleared(int task);
	void tempDirClearFailed(int task);
	void newAuthorization();

	void imageLoaded();

private:

	QPixmap grabInner();

	void placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color);
	QImage icon16, icon32, icon64, iconbig16, iconbig32, iconbig64;

	QWidget *centralwidget;

	typedef QPair<QString, MTPMessageMedia> DelayedServiceMsg;
	QVector<DelayedServiceMsg> _delayedServiceMsgs;
	mtpRequestId _serviceHistoryRequest;

	TitleWidget *title;
	PasscodeWidget *_passcode;
	IntroWidget *intro;
	MainWidget *main;
	SettingsWidget *settings;
	BackgroundWidget *layerBg;

	QTimer _isActiveTimer;
	bool _isActive;

	ConnectingWidget *_connecting;

	Local::ClearManager *_clearManager;

	void clearWidgets();

	bool dragging;
	QPoint dragStart;

	bool _inactivePress;
	QTimer _inactiveTimer;

	SingleTimer _autoLockTimer;
	uint64 _shouldLockAt;

	typedef QMap<MsgId, uint64> NotifyWhenMap;
	typedef QMap<History*, NotifyWhenMap> NotifyWhenMaps;
	NotifyWhenMaps notifyWhenMaps;
	struct NotifyWaiter {
		NotifyWaiter(MsgId msg, uint64 when, PeerData *notifyByFrom) : msg(msg), when(when), notifyByFrom(notifyByFrom) {
		}
		MsgId msg;
		uint64 when;
		PeerData *notifyByFrom;
	};
	typedef QMap<History*, NotifyWaiter> NotifyWaiters;
	NotifyWaiters notifyWaiters;
	NotifyWaiters notifySettingWaiters;
	SingleTimer notifyWaitTimer;

	typedef QMap<uint64, PeerData*> NotifyWhenAlert;
	typedef QMap<History*, NotifyWhenAlert> NotifyWhenAlerts;
	NotifyWhenAlerts notifyWhenAlerts;

	NotifyWindows notifyWindows;

	MediaView *_mediaView;
};

class PreLaunchWindow : public TWidget {
public:

	PreLaunchWindow(QString title = QString());
	void activate();
	float64 basicSize() const {
		return _size;
	}
	~PreLaunchWindow();

	static PreLaunchWindow *instance();

protected:

	float64 _size;

};

class PreLaunchLabel : public QLabel {
public:
	PreLaunchLabel(QWidget *parent);
	void setText(const QString &text);
};

class PreLaunchInput : public QLineEdit {
public:
	PreLaunchInput(QWidget *parent, bool password = false);
};

class PreLaunchLog : public QTextEdit {
public:
	PreLaunchLog(QWidget *parent);
};

class PreLaunchButton : public QPushButton {
public:
	PreLaunchButton(QWidget *parent, bool confirm = true);
	void setText(const QString &text);
};

class NotStartedWindow : public PreLaunchWindow {
public:

	NotStartedWindow();

protected:

	void closeEvent(QCloseEvent *e);
	void resizeEvent(QResizeEvent *e);

private:

	void updateControls();

	PreLaunchLabel _label;
	PreLaunchLog _log;
	PreLaunchButton _close;

};

class LastCrashedWindow : public PreLaunchWindow {
	 Q_OBJECT

public:

	LastCrashedWindow();

public slots:

	void onViewReport();
	void onSaveReport();
	void onSendReport();
	void onGetApp();

	void onNetworkSettings();
	void onNetworkSettingsSaved(QString host, quint32 port, QString username, QString password);
	void onContinue();

	void onCheckingFinished();
	void onSendingError(QNetworkReply::NetworkError e);
	void onSendingFinished();
	void onSendingProgress(qint64 uploaded, qint64 total);

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	void onUpdateRetry();
	void onUpdateSkip();

	void onUpdateChecking();
	void onUpdateLatest();
	void onUpdateDownloading(qint64 ready, qint64 total);
	void onUpdateReady();
	void onUpdateFailed();
#endif

protected:

	void closeEvent(QCloseEvent *e);
	void resizeEvent(QResizeEvent *e);

private:

	void updateControls();

	QString _host, _username, _password;
	quint32 _port;

	PreLaunchLabel _label, _pleaseSendReport, _minidump;
	PreLaunchLog _report;
	PreLaunchButton _send, _sendSkip, _networkSettings, _continue, _showReport, _saveReport, _getApp;

	QString _minidumpName, _minidumpFull, _reportText;
	bool _reportShown, _reportSaved;

	enum SendingState {
		SendingNoReport,
		SendingUpdateCheck,
		SendingNone,
		SendingTooOld,
		SendingTooMany,
		SendingUnofficial,
		SendingProgress,
		SendingUploading,
		SendingFail,
		SendingDone,
	};
	SendingState _sendingState;

	PreLaunchLabel _updating;
	qint64 _sendingProgress, _sendingTotal;

	QNetworkAccessManager _sendManager;
	QNetworkReply *_checkReply, *_sendReply;

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	PreLaunchButton _updatingCheck, _updatingSkip;
	enum UpdatingState {
		UpdatingNone,
		UpdatingCheck,
		UpdatingLatest,
		UpdatingDownload,
		UpdatingFail,
		UpdatingReady
	};
	UpdatingState _updatingState;
	QString _newVersionDownload;

	void setUpdatingState(UpdatingState state, bool force = false);
	void setDownloadProgress(qint64 ready, qint64 total);
#endif

	QString getReportField(const QLatin1String &name, const QLatin1String &prefix);
	void addReportFieldPart(const QLatin1String &name, const QLatin1String &prefix, QHttpMultiPart *multipart);

};

class NetworkSettingsWindow : public PreLaunchWindow {
	Q_OBJECT

public:

	NetworkSettingsWindow(QWidget *parent, QString host, quint32 port, QString username, QString password);

signals:

	void saved(QString host, quint32 port, QString username, QString password);

public slots:

	void onSave();

protected:

	void closeEvent(QCloseEvent *e);
	void resizeEvent(QResizeEvent *e);

private:

	void updateControls();

	PreLaunchLabel _hostLabel, _portLabel, _usernameLabel, _passwordLabel;
	PreLaunchInput _hostInput, _portInput, _usernameInput, _passwordInput;
	PreLaunchButton _save, _cancel;

	QWidget *_parent;

};

class ShowCrashReportWindow : public PreLaunchWindow {
public:

	ShowCrashReportWindow(const QString &text);

protected:

	void resizeEvent(QResizeEvent *e);
    void closeEvent(QCloseEvent *e);

private:

	PreLaunchLog _log;

};

int showCrashReportWindow(const QString &crashdump);
