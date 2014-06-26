/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "application.h"
#include "pspecific.h"

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
	if (cDebug()) LOG(("Application Info: Telegram started in debug mode"));

	DEBUG_LOG(("Application Info: ideal thread count: %1, using %2 connections per session").arg(QThread::idealThreadCount()).arg(cConnectionsInSession()));

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
