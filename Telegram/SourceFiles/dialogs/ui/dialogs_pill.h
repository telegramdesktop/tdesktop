/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QRect>
#include <QtGui/QColor>

class QPainter;

namespace Dialogs {

void PaintPillOutline(QPainter &p, const QRect &pill, int radius);
void PaintTopFade(QPainter &p, int outerWidth, int fadeHeight, QColor bg);
void PaintBottomFade(QPainter &p, int outerWidth, int fadeHeight, QColor bg);

} // namespace Dialogs
