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

bool InternalPassportLink(const QString &url);
bool StartUrlRequiresActivate(const QString &url);

class Application : public QApplication, private QAbstractNativeEventFilter {
	Q_OBJECT

public:
	Application(not_null<Core::Launcher*> launcher, int &argc, char **argv);

	int execute();

	void createMessenger();
	void refreshGlobalProxy();

	void postponeCall(FnMut<void()> &&callable);
	bool notify(QObject *receiver, QEvent *e) override;

	void activateWindowDelayed(not_null<QWidget*> widget);
	void pauseDelayedWindowActivations();
	void resumeDelayedWindowActivations();

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

protected:
	bool event(QEvent *e) override;

private:
	typedef QPair<QLocalSocket*, QByteArray> LocalClient;
	typedef QList<LocalClient> LocalClients;

	struct PostponedCall {
		int loopNestingLevel = 0;
		FnMut<void()> callable;
	};

	bool nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		long *result) override;
	void processPostponedCalls(int level);

	const Qt::HANDLE _mainThreadId = nullptr;
	int _eventNestingLevel = 0;
	int _loopNestingLevel = 0;
	std::vector<int> _previousLoopNestingLevels;
	std::vector<PostponedCall> _postponedCalls;

	QPointer<QWidget> _windowForDelayedActivation;
	bool _delayedActivationsPaused = false;

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

namespace Core {

inline Application &App() {
	Expects(QCoreApplication::instance() != nullptr);

	return *static_cast<Application*>(QCoreApplication::instance());
}

} // namespace Core

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
