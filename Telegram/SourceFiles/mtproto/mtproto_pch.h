/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#include <QtCore/QObject>
#include <QtCore/QThread>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QTcpSocket>

#include <range/v3/all.hpp>

#include <rpl/rpl.h>
#include <crl/crl.h>

#include "base/bytes.h"
#include "base/flat_map.h"
#include "base/flat_set.h"

#include "logs.h"
#include "scheme.h"
