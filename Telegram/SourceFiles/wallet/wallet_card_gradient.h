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

[[nodiscard]] QColor CardDarkBlue();

void PaintCardBackground(QPainter &p, const QRect &card);
void PaintCardQrPlate(QPainter &p, const QRect &plate);

} // namespace Wallet
