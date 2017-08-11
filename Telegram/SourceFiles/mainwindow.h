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

#include "platform/platform_specific.h"
#include "platform/platform_main_window.h"
#include "core/single_timer.h"

class PasscodeWidget;
class MainWidget;
class LayerStackWidget;
class BoxContent;

namespace Intro {
class Widget;
} // namespace Intro

namespace Local {
class ClearManager;
} // namespace Local

namespace Window {
namespace Theme {
struct BackgroundUpdate;
class WarningWidget;
} // namespace Theme
} // namespace Window

namespace Ui {
class LinkButton;
} // namespace Ui

class ConnectingWidget : public TWidget {
	Q_OBJECT

public:
	ConnectingWidget(QWidget *parent, const QString &text, const QString &reconnect);
	void set(const QString &text, const QString &reconnect);

protected:
	void paintEvent(QPaintEvent *e) override;

public slots:
	void onReconnect();

private:
	QString _text;
	int _textWidth = 0;
	object_ptr<Ui::LinkButton> _reconnect;

};

class MediaPreviewWidget;

class MainWindow : public Platform::MainWindow {
	Q_OBJECT

public:
	MainWindow();
	~MainWindow();

	void firstShow();

	void setupPasscode();
	void clearPasscode();
	void setupIntro();
	void setupMain(const MTPUser *user = nullptr);
	void serviceNotification(const TextWithEntities &message, const MTPMessageMedia &media = MTP_messageMediaEmpty(), int32 date = 0, bool force = false);
	void sendServiceHistoryRequest();
	void showDelayedServiceMsgs();

	void mtpStateChanged(int32 dc, int32 state);

	MainWidget *mainWidget();
	PasscodeWidget *passcodeWidget();

	bool doWeReadServerHistory();
	bool doWeReadMentions();

	void activate();

	void noIntro(Intro::Widget *was);
	void noLayerStack(LayerStackWidget *was);
	void layerFinishedHide(LayerStackWidget *was);

	void checkHistoryActivation();

	void fixOrder();

	enum TempDirState {
		TempDirRemoving,
		TempDirExists,
		TempDirEmpty,
	};
	TempDirState tempDirState();
	TempDirState localStorageState();
	void tempDirDelete(int task);

	void sendPaths();

	QImage iconWithCounter(int size, int count, style::color bg, style::color fg, bool smallIcon) override;

	bool contentOverlapped(const QRect &globalRect);
	bool contentOverlapped(QWidget *w, QPaintEvent *e) {
		return contentOverlapped(QRect(w->mapToGlobal(e->rect().topLeft()), e->rect().size()));
	}
	bool contentOverlapped(QWidget *w, const QRegion &r) {
		return contentOverlapped(QRect(w->mapToGlobal(r.boundingRect().topLeft()), r.boundingRect().size()));
	}

	void showMainMenu();
	void updateTrayMenu(bool force = false) override;

	void showSpecialLayer(object_ptr<LayerWidget> layer);

	void ui_showBox(object_ptr<BoxContent> box, ShowLayerOptions options);
	void ui_hideSettingsAndLayer(ShowLayerOptions options);
	bool ui_isLayerShown();
	void ui_showMediaPreview(DocumentData *document);
	void ui_showMediaPreview(PhotoData *photo);
	void ui_hideMediaPreview();

protected:
	bool eventFilter(QObject *o, QEvent *e) override;
	void closeEvent(QCloseEvent *e) override;

	void initHook() override;
	void updateIsActiveHook() override;
	void clearWidgetsHook() override;

	void updateControlsGeometry() override;

public slots:
	void showSettings();
	void setInnerFocus();
	void updateConnectingStatus();

	void quitFromTray();
	void showFromTray(QSystemTrayIcon::ActivationReason reason = QSystemTrayIcon::Unknown);
	void toggleTray(QSystemTrayIcon::ActivationReason reason = QSystemTrayIcon::Unknown);
	void toggleDisplayNotifyFromTray();

	void onClearFinished(int task, void *manager);
	void onClearFailed(int task, void *manager);

	void onShowAddContact();
	void onShowNewGroup();
	void onShowNewChannel();
	void onLogout();

	void app_activateClickHandler(ClickHandlerPtr handler, Qt::MouseButton button);

signals:
	void tempDirCleared(int task);
	void tempDirClearFailed(int task);
	void checkNewAuthorization();

private:
	void showConnecting(const QString &text, const QString &reconnect = QString());
	void hideConnecting();

	void ensureLayerCreated();
	void destroyLayerDelayed();

	void themeUpdated(const Window::Theme::BackgroundUpdate &data);

	QPixmap grabInner();

	void placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color) override;
	QImage icon16, icon32, icon64, iconbig16, iconbig32, iconbig64;

	struct DelayedServiceMsg {
		DelayedServiceMsg(const TextWithEntities &message, const MTPMessageMedia &media, int32 date) : message(message), media(media), date(date) {
		}
		TextWithEntities message;
		MTPMessageMedia media;
		int32 date;
	};
	QList<DelayedServiceMsg> _delayedServiceMsgs;
	mtpRequestId _serviceHistoryRequest = 0;

	object_ptr<PasscodeWidget> _passcode = { nullptr };
	object_ptr<Intro::Widget> _intro = { nullptr };
	object_ptr<MainWidget> _main = { nullptr };
	object_ptr<LayerStackWidget> _layerBg = { nullptr };
	object_ptr<MediaPreviewWidget> _mediaPreview = { nullptr };

	object_ptr<ConnectingWidget> _connecting = { nullptr };
	object_ptr<Window::Theme::WarningWidget> _testingThemeWarning = { nullptr };

	Local::ClearManager *_clearManager = nullptr;

};

class PreLaunchWindow : public TWidget {
public:

	PreLaunchWindow(QString title = QString());
	void activate();
	int basicSize() const {
		return _size;
	}
	~PreLaunchWindow();

	static PreLaunchWindow *instance();

protected:

	int _size;

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

class PreLaunchCheckbox : public QCheckBox {
public:
	PreLaunchCheckbox(QWidget *parent);
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
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

protected:

	void closeEvent(QCloseEvent *e);
	void resizeEvent(QResizeEvent *e);

private:

	QString minidumpFileName();
	void updateControls();

	QString _host, _username, _password;
	quint32 _port;

	PreLaunchLabel _label, _pleaseSendReport, _yourReportName, _minidump;
	PreLaunchLog _report;
	PreLaunchButton _send, _sendSkip, _networkSettings, _continue, _showReport, _saveReport, _getApp;
	PreLaunchCheckbox _includeUsername;

	QString _minidumpName, _minidumpFull, _reportText;
	QString _reportUsername, _reportTextNoUsername;
	QByteArray getCrashReportRaw() const;

	bool _reportShown, _reportSaved;

	void excludeReportUsername();

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
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

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
