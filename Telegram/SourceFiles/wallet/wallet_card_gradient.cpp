/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_gradient.h"

#include "ui/painter.h"

#include <QtCore/QtMath>
#include <QtGui/QConicalGradient>
#include <QtGui/QLinearGradient>

#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kCardConicFrom = 20.99; // CSS angle, clockwise from 12.
constexpr auto kQrPlateAngle = 119.72;
constexpr auto kQrPlateFrom = 0.0799;
constexpr auto kQrPlateTill = 0.922;
constexpr auto kQrPlateAlpha = 224; // 0.88 * 255, rounded.

[[nodiscard]] QColor CardDarkBlue() {
	return QColor(0x00, 0x79, 0xff);
}

[[nodiscard]] QColor CardLightBlue() {
	return QColor(0x1f, 0xad, 0xff);
}

} // namespace

void PaintCardBackground(QPainter &p, const QRect &card) {
	auto hq = PainterHighQualityEnabler(p);
	auto gradient = QConicalGradient(
		QRectF(card).center(),
		90. - kCardConicFrom);
	gradient.setColorAt(0., CardDarkBlue());
	gradient.setColorAt(0.25, CardLightBlue());
	gradient.setColorAt(0.5, CardDarkBlue());
	gradient.setColorAt(0.75, CardLightBlue());
	gradient.setColorAt(1., CardDarkBlue());
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawRoundedRect(card, st::walletCardRadius, st::walletCardRadius);
}

void PaintCardQrPlate(QPainter &p, const QRect &plate) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radians = kQrPlateAngle * M_PI / 180.;
	const auto direction = QPointF(std::sin(radians), -std::cos(radians));
	const auto rect = QRectF(plate);
	const auto length = std::abs(rect.width() * direction.x())
		+ std::abs(rect.height() * direction.y());
	const auto half = direction * (length / 2.);
	auto gradient = QLinearGradient(
		rect.center() - half,
		rect.center() + half);
	gradient.setColorAt(kQrPlateFrom, QColor(255, 255, 255, kQrPlateAlpha));
	gradient.setColorAt(kQrPlateTill, QColor(187, 189, 192, kQrPlateAlpha));
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawRoundedRect(
		rect,
		st::walletCardQrRadius,
		st::walletCardQrRadius);
}

} // namespace Wallet
