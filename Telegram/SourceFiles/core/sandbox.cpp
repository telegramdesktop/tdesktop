/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/sandbox.h"

#include "base/platform/base_platform_info.h"
#include "platform/platform_specific.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "window/notifications_manager.h"
#include "core/crash_reports.h"
#include "core/crash_report_window.h"
#include "core/application.h"
#include "core/launcher.h"
#include "core/local_url_handlers.h"
#include "core/update_checker.h"
#include "base/timer.h"
#include "base/concurrent_timer.h"
#include "base/invoke_queued.h"
#include "base/qthelp_url.h"
#include "base/qthelp_regex.h"
#include "base/qt_adapters.h"
#include "ui/effects/animations.h"
#include "facades.h"
#include "app.h"

#include <QtGui/QSessionManager>
#include <QtGui/QScreen>

namespace Core {
namespace {

constexpr auto kEmptyPidForCommandResponse = 0ULL;

QChar _toHex(ushort v) {
	v = v & 0x000F;
	return QChar::fromLatin1((v >= 10) ? ('a' + (v - 10)) : ('0' + v));
}
ushort _fromHex(QChar c) {
	return ((c.unicode() >= uchar('a')) ? (c.unicode() - uchar('a') + 10) : (c.unicode() - uchar('0'))) & 0x000F;
}

QString _escapeTo7bit(const QString &str) {
	QString result;
	result.reserve(str.size() * 2);
	for (int i = 0, l = str.size(); i != l; ++i) {
		QChar ch(str.at(i));
		ushort uch(ch.unicode());
		if (uch < 32 || uch > 127 || uch == ushort(uchar('%'))) {
			result.append('%').append(_toHex(uch >> 12)).append(_toHex(uch >> 8)).append(_toHex(uch >> 4)).append(_toHex(uch));
		} else {
			result.append(ch);
		}
	}
	return result;
}

QString _escapeFrom7bit(const QString &str) {
	QString result;
	result.reserve(str.size());
	for (int i = 0, l = str.size(); i != l; ++i) {
		QChar ch(str.at(i));
		if (ch == QChar::fromLatin1('%') && i + 4 < l) {
			result.append(QChar(ushort((_fromHex(str.at(i + 1)) << 12) | (_fromHex(str.at(i + 2)) << 8) | (_fromHex(str.at(i + 3)) << 4) | _fromHex(str.at(i + 4)))));
			i += 4;
		} else {
			result.append(ch);
		}
	}
	return result;
}

} // namespace

Sandbox::Sandbox(
	not_null<Core::Launcher*> launcher,
	int &argc,
	char **argv)
: QApplication(argc, argv)
, _mainThreadId(QThread::currentThreadId())
, _handleObservables([=] {
	if (_application) {
		_application->call_handleObservables();
	}
})
, _launcher(launcher) {
	setQuitOnLastWindowClosed(false);
}

int Sandbox::start() {
	if (!Core::UpdaterDisabled()) {
		_updateChecker = std::make_unique<Core::UpdateChecker>();
	}
	const auto d = QFile::encodeName(QDir(cWorkingDir()).absolutePath());
	char h[33] = { 0 };
	hashMd5Hex(d.constData(), d.size(), h);
	_localServerName = Platform::SingleInstanceLocalServerName(h);

	connect(
		&_localSocket,
		&QLocalSocket::connected,
		[=] { socketConnected(); });
	connect(
		&_localSocket,
		&QLocalSocket::disconnected,
		[=] { socketDisconnected(); });
	connect(
		&_localSocket,
		base::QLocalSocket_error,
		[=](QLocalSocket::LocalSocketError error) { socketError(error); });
	connect(
		&_localSocket,
		&QLocalSocket::bytesWritten,
		[=](qint64 bytes) { socketWritten(bytes); });
	connect(
		&_localSocket,
		&QLocalSocket::readyRead,
		[=] { socketReading(); });
	connect(
		&_localServer,
		&QLocalServer::newConnection,
		[=] { newInstanceConnected(); });

	crl::on_main(this, [=] { checkForQuit(); });
	connect(this, &QCoreApplication::aboutToQuit, [=] {
		customEnterFromEventLoop([&] {
			closeApplication();
		});
	});

	// https://github.com/telegramdesktop/tdesktop/issues/948
	// and https://github.com/telegramdesktop/tdesktop/issues/5022
	const auto restartHint = [](QSessionManager &manager) {
		manager.setRestartHint(QSessionManager::RestartNever);
	};

	connect(
		this,
		&QGuiApplication::saveStateRequest,
		this,
		restartHint,
		Qt::DirectConnection);

	if (cManyInstance()) {
		LOG(("Many instance allowed, starting..."));
		singleInstanceChecked();
	} else {
		LOG(("Connecting local socket to %1...").arg(_localServerName));
		_localSocket.connectToServer(_localServerName);
	}

	return exec();
}

void Sandbox::launchApplication() {
	InvokeQueued(this, [=] {
		if (App::quitting()) {
			quit();
		} else if (_application) {
			return;
		}
		setupScreenScale();

		base::InitObservables([] {
			Instance()._handleObservables.call();
		});

		_application = std::make_unique<Application>(_launcher);

		// Ideally this should go to constructor.
		// But we want to catch all native events and Application installs
		// its own filter that can filter out some of them. So we install
		// our filter after the Application constructor installs his.
		installNativeEventFilter(this);

		_application->run();
	});
}

void Sandbox::setupScreenScale() {
	const auto dpi = Sandbox::primaryScreen()->logicalDotsPerInch();
	LOG(("Primary screen DPI: %1").arg(dpi));
	if (dpi <= 108) {
		cSetScreenScale(100); // 100%:  96 DPI (0-108)
	} else if (dpi <= 132) {
		cSetScreenScale(125); // 125%: 120 DPI (108-132)
	} else if (dpi <= 168) {
		cSetScreenScale(150); // 150%: 144 DPI (132-168)
	} else if (dpi <= 216) {
		cSetScreenScale(200); // 200%: 192 DPI (168-216)
	} else if (dpi <= 264) {
		cSetScreenScale(250); // 250%: 240 DPI (216-264)
	} else {
		cSetScreenScale(300); // 300%: 288 DPI (264-inf)
	}

	const auto ratio = devicePixelRatio();
	if (ratio > 1.) {
		if (!Platform::IsMac() || (ratio != 2.)) {
			LOG(("Found non-trivial Device Pixel Ratio: %1").arg(ratio));
			LOG(("Environmental variables: QT_DEVICE_PIXEL_RATIO='%1'").arg(qEnvironmentVariable("QT_DEVICE_PIXEL_RATIO")));
			LOG(("Environmental variables: QT_SCALE_FACTOR='%1'").arg(qEnvironmentVariable("QT_SCALE_FACTOR")));
			LOG(("Environmental variables: QT_AUTO_SCREEN_SCALE_FACTOR='%1'").arg(qEnvironmentVariable("QT_AUTO_SCREEN_SCALE_FACTOR")));
			LOG(("Environmental variables: QT_SCREEN_SCALE_FACTORS='%1'").arg(qEnvironmentVariable("QT_SCREEN_SCALE_FACTORS")));
		}
		style::SetDevicePixelRatio(int(ratio));
		if (Platform::IsMac() && ratio == 2.) {
			cSetScreenScale(110); // 110% for Retina screens by default.
		} else {
			cSetScreenScale(style::kScaleDefault);
		}
	}
}

Sandbox::~Sandbox() = default;

bool Sandbox::event(QEvent *e) {
	if (e->type() == QEvent::Close || e->type() == QEvent::Quit) {
		App::quit();
	}
	return QApplication::event(e);
}

void Sandbox::socketConnected() {
	LOG(("Socket connected, this is not the first application instance, sending show command..."));
	_secondInstance = true;

	QString commands;
	const QStringList &lst(cSendPaths());
	for (QStringList::const_iterator i = lst.cbegin(), e = lst.cend(); i != e; ++i) {
		commands += qsl("SEND:") + _escapeTo7bit(*i) + ';';
	}
	if (!cStartUrl().isEmpty()) {
		commands += qsl("OPEN:") + _escapeTo7bit(cStartUrl()) + ';';
	} else {
		commands += qsl("CMD:show;");
	}

	DEBUG_LOG(("Sandbox Info: writing commands %1").arg(commands));
	_localSocket.write(commands.toLatin1());
}

void Sandbox::socketWritten(qint64/* bytes*/) {
	if (_localSocket.state() != QLocalSocket::ConnectedState) {
		LOG(("Socket is not connected %1").arg(_localSocket.state()));
		return;
	}
	if (_localSocket.bytesToWrite()) {
		return;
	}
	LOG(("Show command written, waiting response..."));
}

void Sandbox::socketReading() {
	if (_localSocket.state() != QLocalSocket::ConnectedState) {
		LOG(("Socket is not connected %1").arg(_localSocket.state()));
		return;
	}
	_localSocketReadData.append(_localSocket.readAll());
	if (QRegularExpression("RES:(\\d+);").match(_localSocketReadData).hasMatch()) {
		uint64 pid = _localSocketReadData.midRef(4, _localSocketReadData.length() - 5).toULongLong();
		if (pid != kEmptyPidForCommandResponse) {
			psActivateProcess(pid);
		}
		LOG(("Show command response received, pid = %1, activating and quitting...").arg(pid));
		return App::quit();
	}
}

void Sandbox::socketError(QLocalSocket::LocalSocketError e) {
	if (App::quitting()) return;

	if (_secondInstance) {
		LOG(("Could not write show command, error %1, quitting...").arg(e));
		return App::quit();
	}

	if (e == QLocalSocket::ServerNotFoundError) {
		LOG(("This is the only instance of Telegram, starting server and app..."));
	} else {
		LOG(("Socket connect error %1, starting server and app...").arg(e));
	}
	_localSocket.close();

	// Local server does not work in WinRT build.
#ifndef Q_OS_WINRT
	psCheckLocalSocket(_localServerName);

	if (!_localServer.listen(_localServerName)) {
		LOG(("Failed to start listening to %1 server: %2").arg(_localServerName, _localServer.errorString()));
		return App::quit();
	}
#endif // !Q_OS_WINRT

	if (!Core::UpdaterDisabled()
		&& !cNoStartUpdate()
		&& Core::checkReadyUpdate()) {
		cSetRestartingUpdate(true);
		DEBUG_LOG(("Sandbox Info: installing update instead of starting app..."));
		return App::quit();
	}

	singleInstanceChecked();
}

void Sandbox::singleInstanceChecked() {
	if (cManyInstance()) {
		Logs::multipleInstances();
	}

	refreshGlobalProxy();
	if (!Logs::started() || (!cManyInstance() && !Logs::instanceChecked())) {
		new NotStartedWindow();
		return;
	}
	const auto result = CrashReports::Start();
	v::match(result, [&](CrashReports::Status status) {
		if (status == CrashReports::CantOpen) {
			new NotStartedWindow();
		} else {
			launchApplication();
		}
	}, [&](const QByteArray &crashdump) {
		// If crash dump is empty with that status it means that we
		// didn't close the application properly. Just ignore for now.
		if (crashdump.isEmpty()) {
			if (CrashReports::Restart() == CrashReports::CantOpen) {
				new NotStartedWindow();
			} else {
				launchApplication();
			}
			return;
		}
		_lastCrashDump = crashdump;
		auto window = new LastCrashedWindow(
			_launcher,
			_lastCrashDump,
			[=] { launchApplication(); });
		window->proxyChanges(
		) | rpl::start_with_next([=](MTP::ProxyData &&proxy) {
			_sandboxProxy = std::move(proxy);
			refreshGlobalProxy();
		}, window->lifetime());
	});
}

void Sandbox::socketDisconnected() {
	if (_secondInstance) {
		DEBUG_LOG(("Sandbox Error: socket disconnected before command response received, quitting..."));
		return App::quit();
	}
}

void Sandbox::newInstanceConnected() {
	DEBUG_LOG(("Sandbox Info: new local socket connected"));
	for (auto client = _localServer.nextPendingConnection(); client; client = _localServer.nextPendingConnection()) {
		_localClients.push_back(LocalClient(client, QByteArray()));
		connect(
			client,
			&QLocalSocket::readyRead,
			[=] { readClients(); });
		connect(
			client,
			&QLocalSocket::disconnected,
			[=] { removeClients(); });
	}
}

void Sandbox::readClients() {
	// This method can be called before Application is constructed.
	QString startUrl;
	QStringList toSend;
	for (LocalClients::iterator i = _localClients.begin(), e = _localClients.end(); i != e; ++i) {
		i->second.append(i->first->readAll());
		if (i->second.size()) {
			QString cmds(QString::fromLatin1(i->second));
			int32 from = 0, l = cmds.length();
			for (int32 to = cmds.indexOf(QChar(';'), from); to >= from; to = (from < l) ? cmds.indexOf(QChar(';'), from) : -1) {
				QStringRef cmd(&cmds, from, to - from);
				if (cmd.startsWith(qsl("CMD:"))) {
					execExternal(cmds.mid(from + 4, to - from - 4));
					const auto response = qsl("RES:%1;").arg(QApplication::applicationPid()).toLatin1();
					i->first->write(response.data(), response.size());
				} else if (cmd.startsWith(qsl("SEND:"))) {
					if (cSendPaths().isEmpty()) {
						toSend.append(_escapeFrom7bit(cmds.mid(from + 5, to - from - 5)));
					}
				} else if (cmd.startsWith(qsl("OPEN:"))) {
					startUrl = _escapeFrom7bit(cmds.mid(from + 5, to - from - 5)).mid(0, 8192);
					auto activateRequired = StartUrlRequiresActivate(startUrl);
					if (activateRequired) {
						execExternal("show");
					}
					const auto responsePid = activateRequired
						? QApplication::applicationPid()
						: kEmptyPidForCommandResponse;
					const auto response = qsl("RES:%1;").arg(responsePid).toLatin1();
					i->first->write(response.data(), response.size());
				} else {
					LOG(("Sandbox Error: unknown command %1 passed in local socket").arg(QString(cmd.constData(), cmd.length())));
				}
				from = to + 1;
			}
			if (from > 0) {
				i->second = i->second.mid(from);
			}
		}
	}
	if (!toSend.isEmpty()) {
		QStringList paths(cSendPaths());
		paths.append(toSend);
		cSetSendPaths(paths);
	}
	if (!cSendPaths().isEmpty()) {
		if (App::wnd()) {
			App::wnd()->sendPaths();
		}
	}
	if (!startUrl.isEmpty()) {
		cSetStartUrl(startUrl);
	}
	if (_application) {
		_application->checkStartUrl();
	}
}

void Sandbox::removeClients() {
	DEBUG_LOG(("Sandbox Info: remove clients slot called, clients %1"
		).arg(_localClients.size()));
	for (auto i = _localClients.begin(), e = _localClients.end(); i != e;) {
		if (i->first->state() != QLocalSocket::ConnectedState) {
			DEBUG_LOG(("Sandbox Info: removing client"));
			i = _localClients.erase(i);
			e = _localClients.end();
		} else {
			++i;
		}
	}
}

void Sandbox::checkForQuit() {
	if (App::quitting()) {
		quit();
	}
}

void Sandbox::refreshGlobalProxy() {
	const auto proxy = !Global::started()
		? _sandboxProxy
		: (Global::ProxySettings() == MTP::ProxyData::Settings::Enabled)
		? Global::SelectedProxy()
		: MTP::ProxyData();
	if (proxy.type == MTP::ProxyData::Type::Socks5
		|| proxy.type == MTP::ProxyData::Type::Http) {
		QNetworkProxy::setApplicationProxy(
			MTP::ToNetworkProxy(MTP::ToDirectIpProxy(proxy)));
	} else if (!Global::started()
		|| Global::ProxySettings() == MTP::ProxyData::Settings::System) {
		QNetworkProxyFactory::setUseSystemConfiguration(true);
	} else {
		QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
	}
}

uint64 Sandbox::installationTag() const {
	return _launcher->installationTag();
}

void Sandbox::checkForEmptyLoopNestingLevel() {
	// _loopNestingLevel == _eventNestingLevel means that we had a
	// native event in a nesting loop that didn't get a notify() call
	// after. That means we already have exited the nesting loop and
	// there must not be any postponed calls with that nesting level.
	if (_loopNestingLevel == _eventNestingLevel) {
		Assert(_postponedCalls.empty()
			|| _postponedCalls.back().loopNestingLevel < _loopNestingLevel);
		Assert(!_previousLoopNestingLevels.empty());

		_loopNestingLevel = _previousLoopNestingLevels.back();
		_previousLoopNestingLevels.pop_back();
	}
}

void Sandbox::postponeCall(FnMut<void()> &&callable) {
	Expects(callable != nullptr);
	Expects(_eventNestingLevel >= _loopNestingLevel);

	checkForEmptyLoopNestingLevel();
	_postponedCalls.push_back({
		_loopNestingLevel,
		std::move(callable)
	});
}

void Sandbox::incrementEventNestingLevel() {
	++_eventNestingLevel;
}

void Sandbox::decrementEventNestingLevel() {
	Expects(_eventNestingLevel >= _loopNestingLevel);

	if (_eventNestingLevel == _loopNestingLevel) {
		_loopNestingLevel = _previousLoopNestingLevels.back();
		_previousLoopNestingLevels.pop_back();
	}
	const auto processTillLevel = _eventNestingLevel - 1;
	processPostponedCalls(processTillLevel);
	checkForEmptyLoopNestingLevel();
	_eventNestingLevel = processTillLevel;

	Ensures(_eventNestingLevel >= _loopNestingLevel);
}

void Sandbox::registerEnterFromEventLoop() {
	Expects(_eventNestingLevel >= _loopNestingLevel);

	if (_eventNestingLevel > _loopNestingLevel) {
		_previousLoopNestingLevels.push_back(_loopNestingLevel);
		_loopNestingLevel = _eventNestingLevel;
	}
}

bool Sandbox::notifyOrInvoke(QObject *receiver, QEvent *e) {
	if (e->type() == base::InvokeQueuedEvent::kType) {
		static_cast<base::InvokeQueuedEvent*>(e)->invoke();
		return true;
	}
	return QApplication::notify(receiver, e);
}

bool Sandbox::notify(QObject *receiver, QEvent *e) {
	if (QThread::currentThreadId() != _mainThreadId) {
		return notifyOrInvoke(receiver, e);
	}

	const auto wrap = createEventNestingLevel();
	if (e->type() == QEvent::UpdateRequest) {
		const auto weak = QPointer<QObject>(receiver);
		_widgetUpdateRequests.fire({});
		if (!weak) {
			return true;
		}
	}
	return notifyOrInvoke(receiver, e);
}

void Sandbox::processPostponedCalls(int level) {
	while (!_postponedCalls.empty()) {
		auto &last = _postponedCalls.back();
		if (last.loopNestingLevel != level) {
			break;
		}
		auto taken = std::move(last);
		_postponedCalls.pop_back();
		taken.callable();
	}
}

bool Sandbox::nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		long *result) {
	registerEnterFromEventLoop();
	return false;
}

rpl::producer<> Sandbox::widgetUpdateRequests() const {
	return _widgetUpdateRequests.events();
}

MTP::ProxyData Sandbox::sandboxProxy() const {
	return _sandboxProxy;
}

void Sandbox::closeApplication() {
	if (App::launchState() == App::QuitProcessed) {
		return;
	}
	App::setLaunchState(App::QuitProcessed);

	_application = nullptr;

	_localServer.close();
	for (const auto &localClient : base::take(_localClients)) {
		localClient.first->close();
	}
	_localClients.clear();

	_localSocket.close();

	_updateChecker = nullptr;
}

void Sandbox::execExternal(const QString &cmd) {
	DEBUG_LOG(("Sandbox Info: executing external command '%1'").arg(cmd));
	if (cmd == "show") {
		if (App::wnd()) {
			App::wnd()->activate();
		} else if (PreLaunchWindow::instance()) {
			PreLaunchWindow::instance()->activate();
		}
	}
}

} // namespace Core

namespace crl {

rpl::producer<> on_main_update_requests() {
	return Core::Sandbox::Instance().widgetUpdateRequests();
}

} // namespace crl

namespace base {

void EnterFromEventLoop(FnMut<void()> &&method) {
	Core::Sandbox::Instance().customEnterFromEventLoop(std::move(method));
}

} // namespace base
