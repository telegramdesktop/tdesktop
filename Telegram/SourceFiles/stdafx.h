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

#define NOMINMAX // no min() and max() macro declarations

#ifdef TDESKTOP_WINRT

#include <wrl.h>
#include <wrl/client.h>

#else // TDESKTOP_WINRT

#define __HUGE
#define PSAPI_VERSION 1 // fix WinXP

#define __STDC_FORMAT_MACROS // fix breakpad for mac

#endif // else of TDESKTOP_WINRT

#ifdef __cplusplus

#include <numeric>

#include <QtCore/QtCore>
#include <QtWidgets/QtWidgets>
#include <QtNetwork/QtNetwork>

#ifdef Q_OS_WIN // use Lzma SDK for win
#include <LzmaLib.h>
#else // Q_OS_WIN
#include <lzma.h>
#endif // else of Q_OS_WIN

extern "C" {

#endif

#include "zip.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#ifdef __cplusplus

}

#include "types.h"
#include "config.h"

#include "mtproto/facade.h"

#include "gui/style_core.h"
#include "gui/twidget.h"
#include "gui/animation.h"
#include "gui/flatinput.h"
#include "gui/flattextarea.h"
#include "gui/flatbutton.h"
#include "gui/boxshadow.h"
#include "gui/popupmenu.h"
#include "gui/scrollarea.h"
#include "gui/images.h"
#include "gui/text.h"
#include "gui/flatlabel.h"

#include "app.h"

#endif
