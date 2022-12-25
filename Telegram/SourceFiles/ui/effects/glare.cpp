/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/glare.h"

#include "styles/style_boxes.h"

namespace Ui {
namespace {

constexpr auto kMaxGlareOpaque = 0.5;

} // namespace

float64 GlareEffect::progress(crl::time now) const {
	return (now - glare.birthTime)
		/ float64(glare.deathTime - glare.birthTime);
}

void GlareEffect::validate(
		const QColor &color,
		Fn<void()> updateCallback,
		crl::time timeout,
		crl::time duration) {
	if (anim::Disabled()) {
		return;
	}
	if (!width) {
		width = st::gradientButtonGlareWidth;
	}
	animation.init([=](crl::time now) {
		if (const auto diff = (now - glare.deathTime); diff > 0) {
			if (diff > timeout && !paused) {
				glare = {
					.birthTime = now,
					.deathTime = now + duration,
				};
				updateCallback();
			}
		} else {
			updateCallback();
		}
	});
	animation.start();
	{
		auto newPixmap = QPixmap(QSize(width, 1)
			* style::DevicePixelRatio());
		newPixmap.setDevicePixelRatio(style::DevicePixelRatio());
		newPixmap.fill(Qt::transparent);
		{
			auto p = QPainter(&newPixmap);
			auto gradient = QLinearGradient(
				QPointF(0, 0),
				QPointF(width, 0));

			auto tempColor = color;
			tempColor.setAlphaF(0);
			const auto edge = tempColor;
			tempColor.setAlphaF(kMaxGlareOpaque);
			const auto middle = tempColor;
			gradient.setStops({
				{ 0., edge },
				{ .5, middle },
				{ 1., edge },
			});
			p.fillRect(newPixmap.rect(), QBrush(gradient));
		}
		pixmap = std::move(newPixmap);
	}
}

} // namespace Ui
