/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PreLaunchWindow : public QWidget {
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
