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
#include <QtCore/QtPlugin>

#ifdef Q_OS_WIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
Q_IMPORT_PLUGIN(QWebpPlugin)
#elif defined Q_OS_MAC
Q_IMPORT_PLUGIN(QGenericEnginePlugin)
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
Q_IMPORT_PLUGIN(QDDSPlugin)
Q_IMPORT_PLUGIN(QICNSPlugin)
Q_IMPORT_PLUGIN(QICOPlugin)
Q_IMPORT_PLUGIN(QJp2Plugin)
Q_IMPORT_PLUGIN(QMngPlugin)
Q_IMPORT_PLUGIN(QTgaPlugin)
Q_IMPORT_PLUGIN(QTiffPlugin)
Q_IMPORT_PLUGIN(QWbmpPlugin)
Q_IMPORT_PLUGIN(QWebpPlugin)
#elif defined Q_OS_LINUX
Q_IMPORT_PLUGIN(QComposePlatformInputContextPlugin)
Q_IMPORT_PLUGIN(QIbusPlatformInputContextPlugin)
Q_IMPORT_PLUGIN(QFcitxPlatformInputContextPlugin)
Q_IMPORT_PLUGIN(QWebpPlugin)
#endif
