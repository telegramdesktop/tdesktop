/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_pill.h"

#include "ui/painter.h"

#include "styles/style_dialogs.h"

namespace Dialogs {

void PaintPillOutline(QPainter &p, const QRect &pill, int radius) {
	if (pill.isEmpty()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto light = (st::dialogsBg->c.lightness() >= 128);
	const auto width = light ? (0.6 * st::lineWidth) : double(st::lineWidth);
	if (light) {
		p.setPen(QPen(QColor(0, 0, 0, 20), width));
	} else {
		auto grad = QLinearGradient(0, pill.top(), 0, pill.bottom());
		grad.setColorAt(0., QColor(255, 255, 255, 40));
		grad.setColorAt(0.5, QColor(255, 255, 255, 0));
		grad.setColorAt(1., QColor(255, 255, 255, 20));
		p.setPen(QPen(QBrush(grad), width));
	}
	p.setBrush(Qt::NoBrush);
	const auto half = 0.5 * width;
	const auto stroke = light
		? QRectF(pill).marginsAdded(QMarginsF(half, half, half, half))
		: QRectF(pill).adjusted(half, half, -half, -half);
	const auto strokeRadius = light ? (radius + half) : (radius - half);
	p.drawRoundedRect(stroke, strokeRadius, strokeRadius);
}

void PaintTopFade(QPainter &p, int outerWidth, int fadeHeight, QColor bg) {
	if (fadeHeight <= 0) {
		return;
	}
	auto transparent = bg;
	transparent.setAlpha(0);
	auto grad = QLinearGradient(0, 0, 0, fadeHeight);
	grad.setColorAt(0, bg);
	grad.setColorAt(1, transparent);
	p.fillRect(QRect(0, 0, outerWidth, fadeHeight), grad);
}

void PaintBottomFade(QPainter &p, int outerWidth, int fadeHeight, QColor bg) {
	if (fadeHeight <= 0) {
		return;
	}
	auto transparent = bg;
	transparent.setAlpha(0);
	auto grad = QLinearGradient(0, 0, 0, fadeHeight);
	grad.setColorAt(0, transparent);
	grad.setColorAt(1, bg);
	p.fillRect(QRect(0, 0, outerWidth, fadeHeight), grad);
}

} // namespace Dialogs
