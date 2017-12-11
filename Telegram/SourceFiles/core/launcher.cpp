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
#include "application.h"

namespace Core {
namespace {

QStringList ReadArguments(int argc, char *argv[]) {
	auto result = QStringList();
	result.reserve(argc);
	for (auto i = 0; i != argc; ++i) {
		result.push_back(fromUtf8Safe(argv[i]));
	}
	return result;
}

} // namespace

std::unique_ptr<Launcher> Launcher::Create(int argc, char *argv[]) {
	return std::make_unique<Platform::Launcher>(argc, argv);
}

Launcher::Launcher(int argc, char *argv[])
: _argc(argc)
, _argv(argv)
, _arguments(ReadArguments(argc, argv)) {
	InitFromCommandLine(argc, argv);
}

void Launcher::init() {
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

	auto result = 0;
	{
		Application app(this, _argc, _argv);
		result = app.exec();
	}

	DEBUG_LOG(("Telegram finished, result: %1").arg(result));

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (cRestartingUpdate()) {
		DEBUG_LOG(("Application Info: executing updater to install update..."));
		psExecUpdater();
	} else
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
	if (cRestarting()) {
		DEBUG_LOG(("Application Info: executing Telegram, because of restart..."));
		psExecTelegram();
	}

	CrashReports::Finish();
	Platform::finish();
	Logs::finish();

	return result;
}

QString Launcher::argumentsString() const {
	return _arguments.join(' ');
}

} // namespace Core
