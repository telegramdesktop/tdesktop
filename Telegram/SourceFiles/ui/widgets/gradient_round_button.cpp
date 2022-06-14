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
namespace {

constexpr auto kMaxGlareOpaque = 0.5;

} // namespace

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
	const auto progress = (crl::now() - _glare.glare.birthTime)
		/ float64(_glare.glare.deathTime - _glare.glare.birthTime);
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
			Painter q(&frame);
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
	if (anim::Disabled()) {
		return;
	}
	_glare.width = st::gradientButtonGlareWidth;
	_glare.animation.init([=](crl::time now) {
		if (const auto diff = (now - _glare.glare.deathTime); diff > 0) {
			if (diff > st::gradientButtonGlareTimeout && !_glare.paused) {
				_glare.glare = Glare{
					.birthTime = now,
					.deathTime = now + st::gradientButtonGlareDuration,
				};
				update();
			}
		} else {
			update();
		}
	});
	_glare.animation.start();
	{
		auto pixmap = QPixmap(QSize(_glare.width, 1)
			* style::DevicePixelRatio());
		pixmap.setDevicePixelRatio(style::DevicePixelRatio());
		pixmap.fill(Qt::transparent);
		{
			Painter p(&pixmap);
			auto gradient = QLinearGradient(
				QPointF(0, 0),
				QPointF(_glare.width, 0));

			auto color = st::premiumButtonFg->c;
			color.setAlphaF(0);
			const auto edge = color;
			color.setAlphaF(kMaxGlareOpaque);
			const auto middle = color;
			gradient.setStops({
				{ 0., edge },
				{ .5, middle },
				{ 1., edge },
			});
			p.fillRect(pixmap.rect(), QBrush(gradient));
		}
		_glare.pixmap = std::move(pixmap);
	}
}

void GradientButton::startGlareAnimation() {
	validateGlare();
}

} // namespace Ui
