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
#define __HUGE

//#define Q_NO_TEMPLATE_FRIENDS // fix some compiler difference issues

#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/aes.h>
#include <openssl/evp.h>

#include <QtWidgets/QtWidgets>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkProxy>

#ifdef Q_OS_WIN // use Lzma SDK for win
#include <LzmaLib.h>
#else
#include <lzma.h>
#endif

#if defined Q_OS_WIN
#define _NEED_WIN_GENERATE_DUMP
#endif

#include "types.h"
#include "config.h"

#include "mtproto/mtp.h"

#include "gui/twidget.h"

#include "gui/style_core.h"
#include "gui/animation.h"
#include "gui/flatinput.h"
#include "gui/flattextarea.h"
#include "gui/flatbutton.h"
#include "gui/scrollarea.h"
#include "gui/images.h"
#include "gui/text.h"
#include "gui/flatlabel.h"

#include "app.h"
