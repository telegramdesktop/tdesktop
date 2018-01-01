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
#include "core/launcher.h"

#include "platform/platform_launcher.h"
#include "platform/platform_specific.h"
#include "core/crash_reports.h"
#include "core/main_queue_processor.h"
#include "application.h"

namespace Core {

std::unique_ptr<Launcher> Launcher::Create(int argc, char *argv[]) {
	return std::make_unique<Platform::Launcher>(argc, argv);
}

Launcher::Launcher(int argc, char *argv[])
: _argc(argc)
, _argv(argv) {
}

void Launcher::init() {
	_arguments = readArguments(_argc, _argv);

	prepareSettings();

	QCoreApplication::setApplicationName(qsl("TelegramDesktop"));

#ifndef OS_MAC_OLD
	QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling, true);
#endif // OS_MAC_OLD

	initHook();
}

int Launcher::exec() {
	init();

	if (cLaunchMode() == LaunchModeFixPrevious) {
		return psFixPrevious();
	} else if (cLaunchMode() == LaunchModeCleanup) {
		return psCleanup();
	}

	// both are finished in Application::closeApplication
	Logs::start(this); // must be started before Platform is started
	Platform::start(); // must be started before QApplication is created

	auto result = executeApplication();

	DEBUG_LOG(("Telegram finished, result: %1").arg(result));

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (cRestartingUpdate()) {
		DEBUG_LOG(("Application Info: executing updater to install update..."));
		if (!launchUpdater(UpdaterLaunch::PerformUpdate)) {
			psDeleteDir(cWorkingDir() + qsl("tupdates/temp"));
		}
	} else
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
	if (cRestarting()) {
		DEBUG_LOG(("Application Info: executing Telegram, because of restart..."));
		launchUpdater(UpdaterLaunch::JustRelaunch);
	}

	CrashReports::Finish();
	Platform::finish();
	Logs::finish();

	return result;
}

QStringList Launcher::readArguments(int argc, char *argv[]) const {
	Expects(argc >= 0);

	if (const auto native = readArgumentsHook(argc, argv)) {
		return *native;
	}

	auto result = QStringList();
	result.reserve(argc);
	for (auto i = 0; i != argc; ++i) {
		result.push_back(fromUtf8Safe(argv[i]));
	}
	return result;
}

QString Launcher::argumentsString() const {
	return _arguments.join(' ');
}

void Launcher::prepareSettings() {
#ifdef Q_OS_MAC
#ifndef OS_MAC_OLD
	if (QSysInfo::macVersion() >= QSysInfo::MV_10_11) {
		gIsElCapitan = true;
	}
#else // OS_MAC_OLD
	if (QSysInfo::macVersion() < QSysInfo::MV_10_7) {
		gIsSnowLeopard = true;
	}
#endif // OS_MAC_OLD
#endif // Q_OS_MAC

	switch (cPlatform()) {
	case dbipWindows:
		gUpdateURL = QUrl(qsl("http://tdesktop.com/win/tupdates/current"));
#ifndef OS_WIN_STORE
		gPlatformString = qsl("Windows");
#else // OS_WIN_STORE
		gPlatformString = qsl("WinStore");
#endif // OS_WIN_STORE
	break;
	case dbipMac:
		gUpdateURL = QUrl(qsl("http://tdesktop.com/mac/tupdates/current"));
#ifndef OS_MAC_STORE
		gPlatformString = qsl("MacOS");
#else // OS_MAC_STORE
		gPlatformString = qsl("MacAppStore");
#endif // OS_MAC_STORE
	break;
	case dbipMacOld:
		gUpdateURL = QUrl(qsl("http://tdesktop.com/mac32/tupdates/current"));
		gPlatformString = qsl("MacOSold");
	break;
	case dbipLinux64:
		gUpdateURL = QUrl(qsl("http://tdesktop.com/linux/tupdates/current"));
		gPlatformString = qsl("Linux64bit");
	break;
	case dbipLinux32:
		gUpdateURL = QUrl(qsl("http://tdesktop.com/linux32/tupdates/current"));
		gPlatformString = qsl("Linux32bit");
	break;
	}

	auto path = Platform::CurrentExecutablePath(_argc, _argv);
	LOG(("Executable path before check: %1").arg(path));
	if (!path.isEmpty()) {
		auto info = QFileInfo(path);
		if (info.isSymLink()) {
			info = info.symLinkTarget();
		}
		if (info.exists()) {
			gExeDir = info.absoluteDir().absolutePath() + '/';
			gExeName = info.fileName();
		}
	}
	if (cExeName().isEmpty()) {
		LOG(("WARNING: Could not compute executable path, some features will be disabled."));
	}

	processArguments();
}

void Launcher::processArguments() {
		enum class KeyFormat {
		NoValues,
		OneValue,
		AllLeftValues,
	};
	auto parseMap = std::map<QByteArray, KeyFormat> {
		{ "-testmode"   , KeyFormat::NoValues },
		{ "-debug"      , KeyFormat::NoValues },
		{ "-many"       , KeyFormat::NoValues },
		{ "-key"        , KeyFormat::OneValue },
		{ "-autostart"  , KeyFormat::NoValues },
		{ "-fixprevious", KeyFormat::NoValues },
		{ "-cleanup"    , KeyFormat::NoValues },
		{ "-noupdate"   , KeyFormat::NoValues },
		{ "-tosettings" , KeyFormat::NoValues },
		{ "-startintray", KeyFormat::NoValues },
		{ "-sendpath"   , KeyFormat::AllLeftValues },
		{ "-workdir"    , KeyFormat::OneValue },
		{ "--"          , KeyFormat::OneValue },
	};
	auto parseResult = QMap<QByteArray, QStringList>();
	auto parsingKey = QByteArray();
	auto parsingFormat = KeyFormat::NoValues;
	for (const auto &argument : _arguments) {
		switch (parsingFormat) {
		case KeyFormat::OneValue: {
			parseResult[parsingKey] = QStringList(argument.mid(0, 8192));
			parsingFormat = KeyFormat::NoValues;
		} break;
		case KeyFormat::AllLeftValues: {
			parseResult[parsingKey].push_back(argument.mid(0, 8192));
		} break;
		case KeyFormat::NoValues: {
			parsingKey = argument.toLatin1();
			auto it = parseMap.find(parsingKey);
			if (it != parseMap.end()) {
				parsingFormat = it->second;
				parseResult[parsingKey] = QStringList();
			}
		} break;
		}
	}

	gTestMode = parseResult.contains("-testmode");
	gDebug = parseResult.contains("-debug");
	gManyInstance = parseResult.contains("-many");
	gKeyFile = parseResult.value("-key", QStringList()).join(QString());
	gLaunchMode = parseResult.contains("-autostart") ? LaunchModeAutoStart
		: parseResult.contains("-fixprevious") ? LaunchModeFixPrevious
		: parseResult.contains("-cleanup") ? LaunchModeCleanup
		: LaunchModeNormal;
	gNoStartUpdate = parseResult.contains("-noupdate");
	gStartToSettings = parseResult.contains("-tosettings");
	gStartInTray = parseResult.contains("-startintray");
	gSendPaths = parseResult.value("-sendpath", QStringList());
	gWorkingDir = parseResult.value("-workdir", QStringList()).join(QString());
	if (!gWorkingDir.isEmpty()) {
		if (QDir().exists(gWorkingDir)) {
			_customWorkingDir = true;
		} else {
			gWorkingDir = QString();
		}
	}
	gStartUrl = parseResult.value("--", QStringList()).join(QString());
}

int Launcher::executeApplication() {
	MainQueueProcessor processor;
	Application app(this, _argc, _argv);
	return app.exec();
}

} // namespace Core
