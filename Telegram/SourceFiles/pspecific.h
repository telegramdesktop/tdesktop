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
#pragma once

#include <QtWidgets/QMainWindow>
#include <QtNetwork/QNetworkReply>
#include "sysbuttons.h"

#ifdef Q_OS_MAC
#include "pspecific_mac.h"
#endif

#ifdef Q_OS_LINUX
#include "pspecific_linux.h"
#endif

#ifdef Q_OS_WIN
#include "pspecific_wnd.h"
#endif
