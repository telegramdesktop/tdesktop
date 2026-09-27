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

namespace Wallet {

void PaintCardBackground(QPainter &p, const QRect &card);
void PaintCardQrPlate(QPainter &p, const QRect &plate);

// The sweep's average, the colour surfaces next to the card match.
[[nodiscard]] QColor CardReferenceColor();

} // namespace Wallet
