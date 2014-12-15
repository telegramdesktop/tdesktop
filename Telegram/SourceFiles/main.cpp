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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "application.h"
#include "pspecific.h"

const char *FeaturesNotify = "\
Telegram Desktop was updated to version %1\n\
\n\
 \xe2\x80\x94 Single column layout for smaller window sizes.\n\
 \xe2\x80\x94 Photos are sent up to 1280x1280 size.\n\
 \xe2\x80\x94 New device login notification added.\n\
 \xe2\x80\x94 Improved image viewer for large documents.\n\
\n\
Full versions log is available here:\n\
https://desktop.telegram.org/#changelog";

int main(int argc, char *argv[]) {
#ifdef _NEED_WIN_GENERATE_DUMP
	_oldWndExceptionFilter = SetUnhandledExceptionFilter(_exceptionFilter);
#endif

	settingsParseArgs(argc, argv);
	for (int32 i = 0; i < argc; ++i) {
		if (string("-fixprevious") == argv[i]) {
			return psFixPrevious();
		} else if (string("-cleanup") == argv[i]) {
			return psCleanup();
		}
	}
	logsInit();

	App::readConfig();
	if (cFromAutoStart() && !cAutoStart()) {
		psAutoStart(false, true);
		return 0;
	}

	DEBUG_LOG(("Application Info: Telegram started, test mode: %1, exe dir: %2").arg(logBool(cTestMode())).arg(cExeDir()));
	if (cDebug()) {
		LOG(("Application Info: Telegram started in debug mode"));
		for (int32 i = 0; i < argc; ++i) {
			LOG(("Argument: %1").arg(QString::fromLocal8Bit(argv[i])));
		}
	}

	DEBUG_LOG(("Application Info: ideal thread count: %1, using %2 connections per session").arg(QThread::idealThreadCount()).arg(cConnectionsInSession()));

	psStart();
	int result = 0;
	{
		Application app(argc, argv);
		if (!App::quiting()) {
			result = app.exec();
		}
	}
    psFinish();

	DEBUG_LOG(("Application Info: Telegram done, result: %1").arg(result));

	if (cRestartingUpdate()) {
		DEBUG_LOG(("Application Info: executing updater to install update.."));
		psExecUpdater();
	} else if (cRestarting()) {
		DEBUG_LOG(("Application Info: executing Telegram, because of restart.."));
		psExecTelegram();
	}

	logsClose();
	return result;
}
