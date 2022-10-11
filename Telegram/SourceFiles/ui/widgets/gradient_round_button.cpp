/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/gradient_round_button.h"

#include "ui/image/image_prepare.h"
#include "styles/style_boxes.h"

namespace Ui {

GradientButton::GradientButton(QWidget *widget, QGradientStops stops)
: RippleButton(widget, st::defaultRippleAnimation)
, _stops(std::move(stops)) {
}

void GradientButton::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	validateBg();
	p.drawImage(0, 0, _bg);
	paintGlare(p);

	const auto ripple = QColor(0, 0, 0, 36);
	paintRipple(p, 0, 0, &ripple);
}

void GradientButton::paintGlare(QPainter &p) {
	if (!_glare.glare.birthTime) {
		return;
	}
	const auto progress = _glare.progress(crl::now());
	const auto x = (-_glare.width) + (width() + _glare.width * 2) * progress;
	const auto h = height();

	const auto edgeWidth = _glare.width + st::roundRadiusLarge;
	if (x > edgeWidth && x < (width() - edgeWidth)) {
		p.drawTiledPixmap(x, 0, _glare.width, h, _glare.pixmap, 0, 0);
	} else {
		auto frame = QImage(
			QSize(_glare.width, h) * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		frame.setDevicePixelRatio(style::DevicePixelRatio());
		frame.fill(Qt::transparent);

		{
			auto q = QPainter(&frame);
			q.drawTiledPixmap(0, 0, _glare.width, h, _glare.pixmap, 0, 0);
			q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			q.drawImage(-x, 0, _bg, 0, 0);
		}
		p.drawImage(x, 0, frame);
	}
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

void GradientButton::setGlarePaused(bool paused) {
	_glare.paused = paused;
}

void GradientButton::validateGlare() {
	_glare.validate(
		st::premiumButtonFg->c,
		[=] { update(); },
		st::gradientButtonGlareTimeout,
		st::gradientButtonGlareDuration);
}

void GradientButton::startGlareAnimation() {
	validateGlare();
}

} // namespace Ui
