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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "application.h"
#include "pspecific.h"

#include "localstorage.h"

int main(int argc, char *argv[]) {
	settingsParseArgs(argc, argv);
	if (cLaunchMode() == LaunchModeFixPrevious) {
		return psFixPrevious();
	} else if (cLaunchMode() == LaunchModeCleanup) {
		return psCleanup();
#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
	} else if (cLaunchMode() == LaunchModeShowCrash) {
		return showCrashReportWindow(QFileInfo(cStartUrl()).absoluteFilePath());
#endif // !TDESKTOP_DISABLE_CRASH_REPORTS
	}

	// both are finished in Application::closeApplication
	Logs::start(); // must be started before PlatformSpecific is started
	PlatformSpecific::start(); // must be started before QApplication is created

	// prepare fake args to disable QT_STYLE_OVERRIDE env variable
	// currently this is required in some desktop environments, including Xubuntu 15.10
	// when we don't default style to "none" Qt dynamically loads GTK somehow internally and
	// our own GTK dynamic load and usage leads GTK errors and freeze of the current main thread
	// we can't disable our own GTK loading because it is required by libappindicator, which
	// provides the tray icon for this system, because Qt tray icon is broken there
	// see https://github.com/telegramdesktop/tdesktop/issues/1774
	QByteArray args[] = { "-style=0" };
	static const int a_cnt = sizeof(args) / sizeof(args[0]);
	int a_argc = a_cnt + 1;
	char *a_argv[a_cnt + 1] = { argv[0], args[0].data() };

	int result = 0;
	{
		Application app(a_argc, a_argv);
		result = app.exec();
	}

	DEBUG_LOG(("Telegram finished, result: %1").arg(result));

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (cRestartingUpdate()) {
		DEBUG_LOG(("Application Info: executing updater to install update..."));
		psExecUpdater();
	} else
#endif
	if (cRestarting()) {
		DEBUG_LOG(("Application Info: executing Telegram, because of restart..."));
		psExecTelegram();
	}

	SignalHandlers::finish();
	PlatformSpecific::finish();
	Logs::finish();

	return result;
}
