/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "wallet/wallet_card_tint.h"

#include <QtCore/QRect>
#include <QtGui/QImage>

class QPainter;

namespace Wallet {

class CardBackground final {
public:
	// Degrees clockwise; one render serves every card it has drawn since.
	void paint(QPainter &p, const QRect &card, float64 angle);
	void setTint(std::optional<CardTint> tint);
	void clear();

private:
	QImage _image;
	QImage _field;
	QSize _drawn;
	std::optional<CardTint> _tint;
	std::vector<QRgb> _tintRamp;
	float64 _angle = 0.;
	int _ratio = 0;

};

void PaintCardQrPlate(QPainter &p, const QRect &plate);

// The sweep's average, the colour surfaces next to the card match.
[[nodiscard]] QColor CardReferenceColor();
[[nodiscard]] QColor CardReferenceColor(const CardTint &tint);

} // namespace Wallet
