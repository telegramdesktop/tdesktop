/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UpdateChecker;

namespace Core {
class Launcher;
} // namespace Core

class Application : public QApplication {
	Q_OBJECT

public:
	Application(not_null<Core::Launcher*> launcher, int &argc, char **argv);

	bool event(QEvent *e) override;

	void createMessenger();

	~Application();

signals:
	void adjustSingleTimers();

// Single instance application
public slots:
	void socketConnected();
	void socketError(QLocalSocket::LocalSocketError e);
	void socketDisconnected();
	void socketWritten(qint64 bytes);
	void socketReading();
	void newInstanceConnected();

	void readClients();
	void removeClients();

	void startApplication(); // will be done in exec()
	void closeApplication(); // will be done in aboutToQuit()

private:
	typedef QPair<QLocalSocket*, QByteArray> LocalClient;
	typedef QList<LocalClient> LocalClients;

	not_null<Core::Launcher*> _launcher;
	std::unique_ptr<Messenger> _messengerInstance;

	QString _localServerName, _localSocketReadData;
	QLocalServer _localServer;
	QLocalSocket _localSocket;
	LocalClients _localClients;
	bool _secondInstance = false;

	void singleInstanceChecked();

#ifndef TDESKTOP_DISABLE_AUTOUPDATE

// Autoupdating
public:
	void startUpdateCheck(bool forceWait);
	void stopUpdate();

	enum UpdatingState {
		UpdatingNone,
		UpdatingDownload,
		UpdatingReady,
	};
	UpdatingState updatingState();
	int32 updatingSize();
	int32 updatingReady();

signals:
	void updateChecking();
	void updateLatest();
	void updateProgress(qint64 ready, qint64 total);
	void updateReady();
	void updateFailed();

public slots:
	void updateCheck();

	void updateGotCurrent();
	void updateFailedCurrent(QNetworkReply::NetworkError e);

	void onUpdateReady();
	void onUpdateFailed();

private:
	object_ptr<SingleTimer> _updateCheckTimer = { nullptr };
	QNetworkReply *_updateReply = nullptr;
	QNetworkAccessManager _updateManager;
	QThread *_updateThread = nullptr;
	UpdateChecker *_updateChecker = nullptr;

#endif // !TDESKTOP_DISABLE_AUTOUPDATE
};

namespace Sandbox {

QRect availableGeometry();
QRect screenGeometry(const QPoint &p);
void setActiveWindow(QWidget *window);
bool isSavingSession();

void execExternal(const QString &cmd);

void adjustSingleTimers();

#ifndef TDESKTOP_DISABLE_AUTOUPDATE

void startUpdateCheck();
void stopUpdate();

Application::UpdatingState updatingState();
int32 updatingSize();
int32 updatingReady();

void updateChecking();
void updateLatest();
void updateProgress(qint64 ready, qint64 total);
void updateFailed();
void updateReady();

#endif // !TDESKTOP_DISABLE_AUTOUPDATE

void connect(const char *signal, QObject *object, const char *method);

void launch();

} // namespace Sandbox
