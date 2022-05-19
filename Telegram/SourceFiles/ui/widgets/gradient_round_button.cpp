/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/gradient_round_button.h"

#include "ui/image/image_prepare.h"

namespace Ui {

GradientButton::GradientButton(QWidget *widget, QGradientStops stops)
: RippleButton(widget, st::defaultRippleAnimation)
, _stops(std::move(stops)) {
}

void GradientButton::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	validateBg();
	p.drawImage(0, 0, _bg);
	const auto ripple = QColor(0, 0, 0, 36);
	paintRipple(p, 0, 0, &ripple);
}

void GradientButton::validateBg() {
	const auto factor = devicePixelRatio();
	if (!_bg.isNull()
		&& (_bg.devicePixelRatio() == factor)
		&& (_bg.size() == size() * factor)) {
		return;
	}
	_bg = QImage(size() * factor, QImage::Format_ARGB32_Premultiplied);
	_bg.setDevicePixelRatio(factor);

	auto p = QPainter(&_bg);
	auto gradient = QLinearGradient(QPointF(0, 0), QPointF(width(), 0));
	gradient.setStops(_stops);
	p.fillRect(rect(), gradient);
	p.end();

	_bg = Images::Round(std::move(_bg), ImageRoundRadius::Large);
}

} // namespace Ui
