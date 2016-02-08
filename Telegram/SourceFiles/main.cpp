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
	int result = 0;

	settingsParseArgs(argc, argv);
	if (cLaunchMode() == LaunchModeFixPrevious) {
		return psFixPrevious();
	} else if (cLaunchMode() == LaunchModeCleanup) {
		return psCleanup();
	} else if (cLaunchMode() == LaunchModeShowCrash) {
		return showCrashReportWindow(QFileInfo(cStartUrl()).absoluteFilePath());
	}

	Logs::Initializer _logs;
	{
		PlatformSpecific::Initializer _ps;

		QByteArray args[] = { "-style=0" }; // prepare fake args to disable QT_STYLE_OVERRIDE env variable
		static const int a_cnt = sizeof(args) / sizeof(args[0]);
		int a_argc = a_cnt + 1;
		char *a_argv[a_cnt + 1] = { argv[0], args[0].data() };

		Application app(a_argc, a_argv);
		if (!App::quiting()) {
			result = app.exec();
		}
	}

	DEBUG_LOG(("Telegram finished, result: %1").arg(result));

	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (cRestartingUpdate()) {
		DEBUG_LOG(("Application Info: executing updater to install update.."));
		psExecUpdater();
	} else
	#endif
	if (cRestarting()) {
		DEBUG_LOG(("Application Info: executing Telegram, because of restart.."));
		psExecTelegram();
	}

	SignalHandlers::finish();
	return result;
}
