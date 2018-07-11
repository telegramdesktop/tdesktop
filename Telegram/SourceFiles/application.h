/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Core {
class Launcher;
class UpdateChecker;
} // namespace Core

bool StartUrlRequiresActivate(const QString &url);

class Application : public QApplication {
	Q_OBJECT

public:
	Application(not_null<Core::Launcher*> launcher, int &argc, char **argv);

	bool event(QEvent *e) override;

	void createMessenger();
	void refreshGlobalProxy();

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

private:
	std::unique_ptr<Core::UpdateChecker> _updateChecker;

};

namespace Sandbox {

QRect availableGeometry();
QRect screenGeometry(const QPoint &p);
void setActiveWindow(QWidget *window);
bool isSavingSession();

void execExternal(const QString &cmd);

void adjustSingleTimers();

void refreshGlobalProxy();

void connect(const char *signal, QObject *object, const char *method);

void launch();

} // namespace Sandbox
