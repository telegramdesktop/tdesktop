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
#include "window/window_controller.h"
#include "core/crash_reports.h"
#include "core/crash_report_window.h"
#include "core/application.h"
#include "core/launcher.h"
#include "core/local_url_handlers.h"
#include "core/update_checker.h"
#include "core/deadlock_detector.h"
#include "base/timer.h"
#include "base/concurrent_timer.h"
#include "base/invoke_queued.h"
#include "base/qthelp_url.h"
#include "base/qthelp_regex.h"
#include "ui/ui_utility.h"
#include "ui/effects/animations.h"

#include <QtCore/QLockFile>
#include <QtGui/QSessionManager>
#include <QtGui/QScreen>
#include <QtGui/qpa/qplatformscreen.h>
#include <ksandbox.h>

namespace Core {
namespace {

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

bool Sandbox::QuitOnStartRequested = false;

Sandbox::Sandbox(int &argc, char **argv)
: QApplication(argc, argv)
, _mainThreadId(QThread::currentThreadId()) {
	setQuitOnLastWindowClosed(false);
}

int Sandbox::start() {
	if (!Core::UpdaterDisabled()) {
		_updateChecker = std::make_unique<Core::UpdateChecker>();
	}

	{
		const auto d = QFile::encodeName(QDir(cWorkingDir()).absolutePath());
		char h[33] = { 0 };
		hashMd5Hex(d.constData(), d.size(), h);
		_localServerName = Platform::SingleInstanceLocalServerName(h);
	}

	{
		const auto d = QFile::encodeName(cExeDir() + cExeName());
		QByteArray h;
		h.resize(32);
		hashMd5Hex(d.constData(), d.size(), h.data());
		_lockFile = std::make_unique<QLockFile>(QDir::tempPath() + '/' + h + '-' + cGUIDStr());
		_lockFile->setStaleLockTime(0);
		if (!_lockFile->tryLock()
			&& Launcher::Instance().customWorkingDir()) {
			// On Windows, QLockFile has problems detecting a stale lock
			// if the machine's hostname contains characters outside the US-ASCII character set.
			if constexpr (Platform::IsWindows()) {
				// QLockFile::removeStaleLockFile returns false on Windows,
				// when the application owning the lock is still running.
				if (!_lockFile->removeStaleLockFile()) {
					gManyInstance = true;
				}
			} else {
				gManyInstance = true;
			}
		}
	}

#if defined Q_OS_LINUX && QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
	_localServer.setSocketOptions(QLocalServer::AbstractNamespaceOption);
	_localSocket.setSocketOptions(QLocalSocket::AbstractNamespaceOption);
#endif // Q_OS_LINUX && Qt >= 6.2.0

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
		&QLocalSocket::errorOccurred,
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
	connect(this, &QGuiApplication::saveStateRequest, [](auto &manager) {
		manager.setRestartHint(QSessionManager::RestartNever);
	});

	LOG(("Connecting local socket to %1...").arg(_localServerName));
	_localSocket.connectToServer(_localServerName);

	if (QuitOnStartRequested) {
		closeApplication();
		return 0;
	}
	_started = true;
	return exec();
}

void Sandbox::QuitWhenStarted() {
	if (!QApplication::instance() || !Instance()._started) {
		QuitOnStartRequested = true;
	} else {
		quit();
	}
}

void Sandbox::launchApplication() {
	InvokeQueued(this, [=] {
		if (Quitting()) {
			quit();
		} else if (_application) {
			return;
		}
		setupScreenScale();

#ifndef _DEBUG
		if (Logs::DebugEnabled()) {
			using DeadlockDetector::PingThread;
			_deadlockDetector = std::make_unique<PingThread>(this);
		}
#endif // !_DEBUG

		_application = std::make_unique<Application>();

		// Ideally this should go to constructor.
		// But we want to catch all native events and Application installs
		// its own filter that can filter out some of them. So we install
		// our filter after the Application constructor installs his.
		installNativeEventFilter(this);

		_application->run();
	});
}

void Sandbox::setupScreenScale() {
	const auto ratio = devicePixelRatio();
	LOG(("Global devicePixelRatio: %1").arg(ratio));
	const auto logEnv = [](const char *name) {
		const auto value = qEnvironmentVariable(name);
		if (!value.isEmpty()) {
			LOG(("%1: %2").arg(name, value));
		}
	};
	logEnv("QT_DEVICE_PIXEL_RATIO");
	logEnv("QT_AUTO_SCREEN_SCALE_FACTOR");
	logEnv("QT_ENABLE_HIGHDPI_SCALING");
	logEnv("QT_SCALE_FACTOR");
	logEnv("QT_SCREEN_SCALE_FACTORS");
	logEnv("QT_SCALE_FACTOR_ROUNDING_POLICY");
	logEnv("QT_DPI_ADJUSTMENT_POLICY");
	logEnv("QT_USE_PHYSICAL_DPI");
	logEnv("QT_FONT_DPI");

	const auto useRatio = std::clamp(qCeil(ratio), 1, 3);
	style::SetDevicePixelRatio(useRatio);

	const auto screen = Sandbox::primaryScreen();
	const auto dpi = screen->logicalDotsPerInch();
	const auto basePair = screen->handle()->logicalBaseDpi();
	const auto base = (basePair.first + basePair.second) * 0.5;
	const auto screenScaleExact = dpi / base;
	const auto screenScale = int(base::SafeRound(screenScaleExact * 20)) * 5;
	LOG(("Primary screen DPI: %1, Base: %2.").arg(dpi).arg(base));
	LOG(("Computed screen scale: %1").arg(screenScale));
	if (Platform::IsMac()) {
		// 110% for Retina screens by default.
		cSetScreenScale((useRatio == 2) ? 110 : style::kScaleDefault);
	} else {
		cSetScreenScale(std::clamp(
			screenScale,
			style::kScaleMin,
			style::MaxScaleForRatio(useRatio)));
	}
	LOG(("DevicePixelRatio: %1").arg(useRatio));
	LOG(("ScreenScale: %1").arg(cScreenScale()));
}

Sandbox::~Sandbox() = default;

bool Sandbox::event(QEvent *e) {
	if (e->type() == QEvent::Quit && !Quitting()) {
		Quit(QuitReason::QtQuitEvent);
		e->ignore();
		return false;
	} else if (e->type() == QEvent::Close) {
		Quit();
	} else if (e->type() == DeadlockDetector::PingPongEvent::Type()) {
		postEvent(
			static_cast<DeadlockDetector::PingPongEvent*>(e)->sender(),
			new DeadlockDetector::PingPongEvent(this));
	}
	return QApplication::event(e);
}

void Sandbox::socketConnected() {
	LOG(("Socket connected, this is not the first application instance, sending show command..."));
	_secondInstance = true;

	QString commands;
	const QStringList &lst(cSendPaths());
	for (QStringList::const_iterator i = lst.cbegin(), e = lst.cend(); i != e; ++i) {
		commands += u"SEND:"_q + _escapeTo7bit(*i) + ';';
	}
	if (qEnvironmentVariableIsSet("XDG_ACTIVATION_TOKEN")) {
		commands += u"XDG_ACTIVATION_TOKEN:"_q + _escapeTo7bit(qEnvironmentVariable("XDG_ACTIVATION_TOKEN")) + ';';
	}
	if (!cStartUrl().isEmpty()) {
		commands += u"OPEN:"_q + _escapeTo7bit(cStartUrl()) + ';';
	} else if (cQuit()) {
		commands += u"CMD:quit;"_q;
	} else {
		commands += u"CMD:show;"_q;
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
	const auto m = QRegularExpression(u"RES:(\\d+)_(\\d+);"_q).match(
		_localSocketReadData);
	if (!m.hasMatch()) {
		return;
	}
	const auto processId = m.capturedView(1).toULongLong();
	const auto windowId = m.capturedView(2).toULongLong();
	if (windowId) {
		Platform::ActivateOtherProcess(processId, windowId);
	}
	LOG(("Show command response received, processId = %1, windowId = %2, "
		"activating and quitting..."
		).arg(processId
		).arg(windowId));
	return Quit();
}

void Sandbox::socketError(QLocalSocket::LocalSocketError e) {
	if (Quitting()) return;

	if (_secondInstance) {
		LOG(("Could not write show command, error %1, quitting...").arg(e));
		return Quit();
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
		return Quit();
	}
#endif // !Q_OS_WINRT

	if (!Core::UpdaterDisabled()
		&& !cNoStartUpdate()
		&& Core::checkReadyUpdate()) {
		cSetRestartingUpdate(true);
		DEBUG_LOG(("Sandbox Info: installing update instead of starting app..."));
		return Quit();
	}

	if (cQuit()) {
		return Quit();
	}

	singleInstanceChecked();
}

void Sandbox::singleInstanceChecked() {
	if (cManyInstance()) {
		LOG(("App Info: Detected another instance"));
	}

	refreshGlobalProxy();
	if (!Logs::started() || !Logs::instanceChecked()) {
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
		return Quit();
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
				auto cmd = base::StringViewMid(cmds, from, to - from);
				if (cmd.startsWith(u"CMD:"_q)) {
					const auto processId = QApplication::applicationPid();
					const auto windowId = execExternal(cmds.mid(from + 4, to - from - 4));
					const auto response = u"RES:%1_%2;"_q.arg(processId).arg(windowId).toLatin1();
					i->first->write(response.data(), response.size());
				} else if (cmd.startsWith(u"SEND:"_q)) {
					if (cSendPaths().isEmpty()) {
						toSend.append(_escapeFrom7bit(cmds.mid(from + 5, to - from - 5)));
					}
				} else if (cmd.startsWith(u"XDG_ACTIVATION_TOKEN:"_q)) {
					qputenv("XDG_ACTIVATION_TOKEN", _escapeFrom7bit(cmds.mid(from + 21, to - from - 21)).toUtf8());
				} else if (cmd.startsWith(u"OPEN:"_q)) {
					startUrl = _escapeFrom7bit(cmds.mid(from + 5, to - from - 5)).mid(0, 8192);
					const auto activationRequired = StartUrlRequiresActivate(startUrl);
					const auto processId = QApplication::applicationPid();
					const auto windowId = activationRequired
						? execExternal("show")
						: 0;
					const auto response = u"RES:%1_%2;"_q.arg(processId).arg(windowId).toLatin1();
					i->first->write(response.data(), response.size());
				} else {
					LOG(("Sandbox Error: unknown command %1 passed in local socket").arg(cmd.toString()));
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
	if (_application) {
		_application->checkSendPaths();
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
	if (Quitting()) {
		quit();
	}
}

void Sandbox::refreshGlobalProxy() {
	const auto proxy = !Core::IsAppLaunched()
		? _sandboxProxy
		: Core::App().settings().proxy().isEnabled()
		? Core::App().settings().proxy().selected()
		: MTP::ProxyData();
	if (proxy.type == MTP::ProxyData::Type::Socks5
		|| proxy.type == MTP::ProxyData::Type::Http) {
		QNetworkProxy::setApplicationProxy(
			MTP::ToNetworkProxy(MTP::ToDirectIpProxy(proxy)));
	} else if ((!Core::IsAppLaunched()
		|| Core::App().settings().proxy().isSystem())
		// this works stable only in sandboxed environment where it works through portal
		&& (!Platform::IsLinux() || KSandbox::isInside() || cDebugMode())) {
		QNetworkProxyFactory::setUseSystemConfiguration(true);
	} else {
		QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
	}
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
	if (e->type() == base::InvokeQueuedEvent::Type()) {
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
		base::NativeEventResult *result) {
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
	if (CurrentLaunchState() == LaunchState::QuitProcessed) {
		return;
	}
	SetLaunchState(LaunchState::QuitProcessed);

	_application = nullptr;

	_localServer.close();
	for (const auto &localClient : base::take(_localClients)) {
		localClient.first->close();
	}
	_localClients.clear();

	_localSocket.close();

	_updateChecker = nullptr;
}

uint64 Sandbox::execExternal(const QString &cmd) {
	DEBUG_LOG(("Sandbox Info: executing external command '%1'").arg(cmd));
	if (cmd == "show") {
		if (Core::IsAppLaunched() && Core::App().activePrimaryWindow()) {
			const auto window = Core::App().activePrimaryWindow();
			window->activate();
			return Platform::ActivationWindowId(window->widget());
		} else if (const auto window = PreLaunchWindow::instance()) {
			window->activate();
			return Platform::ActivationWindowId(window);
		}
	} else if (cmd == "quit") {
		Quit();
	}
	return 0;
}

} // namespace Core

namespace crl {

rpl::producer<> on_main_update_requests() {
	return Core::Sandbox::Instance().widgetUpdateRequests();
}

} // namespace crl
