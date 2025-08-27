/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#include <QtCore/QString>
#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtCore/QSize>

#include <QtGui/QColor>
#include <QtGui/QPen>
#include <QtGui/QBrush>
#include <QtGui/QCursor>
#include <QtGui/QFont>
#include <QtGui/QFontMetrics>
#include <QtGui/QPainter>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtGui/QtEvents>

#include <QtWidgets/QWidget>

#include <rpl/rpl.h>
#include <range/v3/all.hpp>
#include <crl/crl_time.h>
#include <crl/crl_on_main.h>

#include "base/algorithm.h"
#include "base/basic_types.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "base/weak_qptr.h"

#include "core/credits_amount.h"

#include "ui/arc_angles.h"
#include "ui/text/text.h"
#include "ui/effects/animations.h"
#include "ui/qt_object_factory.h"
#include "styles/palette.h"
