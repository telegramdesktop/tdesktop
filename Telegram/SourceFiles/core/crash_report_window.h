/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QCheckBox>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QHttpMultiPart>
#include <QtNetwork/QNetworkAccessManager>

namespace MTP {
struct ProxyData;
} // namespace MTP

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
	void closeEvent(QCloseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateControls();

	PreLaunchLabel _label;
	PreLaunchLog _log;
	PreLaunchButton _close;

};

class LastCrashedWindow : public PreLaunchWindow {

public:
	LastCrashedWindow(const QByteArray &crashdump, Fn<void()> launch);

	rpl::producer<MTP::ProxyData> proxyChanges() const;

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

	void saveReport();
	void sendReport();

	void networkSettings();
	void processContinue();

	void checkingFinished();
	void sendingError(QNetworkReply::NetworkError e);
	void sendingFinished();
	void sendingProgress(qint64 uploaded, qint64 total);

	void updateRetry();
	void updateSkip();

protected:
	void closeEvent(QCloseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void proxyUpdated();
	QString minidumpFileName();
	void updateControls();

	void excludeReportUsername();

	QString getReportField(const QLatin1String &name, const QLatin1String &prefix);
	void addReportFieldPart(const QLatin1String &name, const QLatin1String &prefix, QHttpMultiPart *multipart);

	QByteArray _dumpraw;

	PreLaunchLabel _label, _pleaseSendReport, _yourReportName, _minidump;
	PreLaunchLog _report;
	PreLaunchButton _send, _sendSkip, _networkSettings, _continue, _showReport, _saveReport, _getApp;
	PreLaunchCheckbox _includeUsername;

	QString _minidumpName, _minidumpFull, _reportText;
	QString _reportUsername, _reportTextNoUsername;
	QByteArray getCrashReportRaw() const;

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

	QNetworkAccessManager _sendManager;
	QNetworkReply *_checkReply = nullptr;
	QNetworkReply *_sendReply = nullptr;

	enum UpdatingState {
		UpdatingNone,
		UpdatingCheck,
		UpdatingLatest,
		UpdatingDownload,
		UpdatingFail,
		UpdatingReady
	};
	struct UpdaterData {
		UpdaterData(QWidget *buttonParent);

		PreLaunchButton check, skip;
		UpdatingState state;
		QString newVersionDownload;
	};
	const std::unique_ptr<UpdaterData> _updaterData;

	void setUpdatingState(UpdatingState state, bool force = false);
	void setDownloadProgress(qint64 ready, qint64 total);

	Fn<void()> _launch;
	rpl::event_stream<MTP::ProxyData> _proxyChanges;
	rpl::lifetime _lifetime;

};

class NetworkSettingsWindow : public PreLaunchWindow {

public:
	NetworkSettingsWindow(QWidget *parent, QString host, quint32 port, QString username, QString password);

	[[nodiscard]] rpl::producer<MTP::ProxyData> saveRequests() const;
	void save();

protected:
	void closeEvent(QCloseEvent *e);
	void resizeEvent(QResizeEvent *e);

private:
	void updateControls();

	PreLaunchLabel _hostLabel, _portLabel, _usernameLabel, _passwordLabel;
	PreLaunchInput _hostInput, _portInput, _usernameInput, _passwordInput;
	PreLaunchButton _save, _cancel;

	QWidget *_parent;

	rpl::event_stream<MTP::ProxyData> _saveRequests;

};
