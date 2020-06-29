/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_proxy_data.h"

#include <QtWidgets/QApplication>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>
#include <QtCore/QAbstractNativeEventFilter>

namespace Core {

class Launcher;
class UpdateChecker;
class Application;

class Sandbox final
	: public QApplication
	, private QAbstractNativeEventFilter {
private:
	auto createEventNestingLevel() {
		incrementEventNestingLevel();
		return gsl::finally([=] { decrementEventNestingLevel(); });
	}

public:
	Sandbox(not_null<Launcher*> launcher, int &argc, char **argv);

	Sandbox(const Sandbox &other) = delete;
	Sandbox &operator=(const Sandbox &other) = delete;

	int start();

	void refreshGlobalProxy();
	uint64 installationTag() const;

	void postponeCall(FnMut<void()> &&callable);
	bool notify(QObject *receiver, QEvent *e) override;

	template <typename Callable>
	auto customEnterFromEventLoop(Callable &&callable) {
		registerEnterFromEventLoop();
		const auto wrap = createEventNestingLevel();
		return callable();
	}

	rpl::producer<> widgetUpdateRequests() const;

	MTP::ProxyData sandboxProxy() const;

	static Sandbox &Instance() {
		Expects(QCoreApplication::instance() != nullptr);

		return *static_cast<Sandbox*>(QCoreApplication::instance());
	}

	~Sandbox();

protected:
	bool event(QEvent *e) override;

private:
	typedef QPair<QLocalSocket*, QByteArray> LocalClient;
	typedef QList<LocalClient> LocalClients;

	struct PostponedCall {
		int loopNestingLevel = 0;
		FnMut<void()> callable;
	};

	bool notifyOrInvoke(QObject *receiver, QEvent *e);

	void closeApplication(); // will be done in aboutToQuit()
	void checkForQuit(); // will be done in exec()
	void checkForEmptyLoopNestingLevel();
	void registerEnterFromEventLoop();
	void incrementEventNestingLevel();
	void decrementEventNestingLevel();
	bool nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		long *result) override;
	void processPostponedCalls(int level);
	void singleInstanceChecked();
	void launchApplication();
	void setupScreenScale();
	void execExternal(const QString &cmd);

	// Single instance application
	void socketConnected();
	void socketError(QLocalSocket::LocalSocketError e);
	void socketDisconnected();
	void socketWritten(qint64 bytes);
	void socketReading();
	void newInstanceConnected();

	void readClients();
	void removeClients();

	const Qt::HANDLE _mainThreadId = nullptr;
	int _eventNestingLevel = 0;
	int _loopNestingLevel = 0;
	std::vector<int> _previousLoopNestingLevels;
	std::vector<PostponedCall> _postponedCalls;
	SingleQueuedInvokation _handleObservables;

	not_null<Launcher*> _launcher;
	std::unique_ptr<Application> _application;

	QString _localServerName, _localSocketReadData;
	QLocalServer _localServer;
	QLocalSocket _localSocket;
	LocalClients _localClients;
	bool _secondInstance = false;

	std::unique_ptr<UpdateChecker> _updateChecker;

	QByteArray _lastCrashDump;
	MTP::ProxyData _sandboxProxy;

	rpl::event_stream<> _widgetUpdateRequests;

};

} // namespace Core
