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
#include <QtCore/QtPlugin>

#ifdef Q_OS_WIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
Q_IMPORT_PLUGIN(QWindowsAudioPlugin)
Q_IMPORT_PLUGIN(AccessibleFactory)
#elif defined Q_OS_MAC
//Q_IMPORT_PLUGIN(AVFServicePlugin)
Q_IMPORT_PLUGIN(AVFMediaPlayerServicePlugin)
Q_IMPORT_PLUGIN(QT7ServicePlugin)
Q_IMPORT_PLUGIN(AudioCaptureServicePlugin)
Q_IMPORT_PLUGIN(CoreAudioPlugin)
Q_IMPORT_PLUGIN(QM3uPlaylistPlugin)
Q_IMPORT_PLUGIN(AccessibleFactory)
Q_IMPORT_PLUGIN(QCoreWlanEnginePlugin)
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
Q_IMPORT_PLUGIN(QPulseAudioPlugin)
#endif
