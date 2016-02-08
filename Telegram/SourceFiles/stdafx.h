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
#define __HUGE
#define PSAPI_VERSION 1 // fix WinXP
//#define Q_NO_TEMPLATE_FRIENDS // fix some compiler difference issues

#define __STDC_FORMAT_MACROS // fix breakpad for mac

#ifdef __cplusplus

#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/md5.h>

#include <QtWidgets/QtWidgets>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QLocalSocket>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QHttpMultiPart>

#ifdef Q_OS_WIN // use Lzma SDK for win
#include <LzmaLib.h>
#else
#include <lzma.h>
#endif

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

#include "mtproto/mtp.h"

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
