/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <QtCore/QObject>
#include <QtCore/QThread>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QTcpSocket>

#include <rpl/rpl.h>
#include <crl/crl.h>

#include "base/bytes.h"

#include "logs.h"
#include "scheme.h"
