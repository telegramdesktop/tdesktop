/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_stars_colored.h"

#include "ui/effects/premium_graphics.h" // GiftGradientStops.
#include "ui/rp_widget.h"

namespace Ui {
namespace Premium {

ColoredMiniStars::ColoredMiniStars(not_null<Ui::RpWidget*> parent)
: _ministars([=](const QRect &r) {
	parent->update(r.translated(_position));
}, true) {
}

void ColoredMiniStars::setSize(const QSize &size) {
	_frame = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	_frame.setDevicePixelRatio(style::DevicePixelRatio());

	_mask = _frame;
	_mask.fill(Qt::transparent);
	{
		auto p = QPainter(&_mask);
		if (_colorOverride) {
			p.fillRect(0, 0, size.width(), size.height(), *_colorOverride);
		} else {
			auto gradient = QLinearGradient(0, 0, size.width(), 0);
			gradient.setStops(Ui::Premium::GiftGradientStops());
			p.setPen(Qt::NoPen);
			p.setBrush(gradient);
			p.drawRect(0, 0, size.width(), size.height());
		}
	}

	_size = size;

	{
		const auto s = _size / Ui::Premium::MiniStars::kSizeFactor;
		const auto margins = QMarginsF(
			s.width() / 2.,
			s.height() / 2.,
			s.width() / 2.,
			s.height() / 2.);
		_ministarsRect = QRectF(QPointF(), _size) - margins;
	}
}

void ColoredMiniStars::setPosition(QPoint position) {
	_position = std::move(position);
}

void ColoredMiniStars::setColorOverride(std::optional<QColor> color) {
	_colorOverride = color;
}

void ColoredMiniStars::paint(QPainter &p) {
	_frame.fill(Qt::transparent);
	{
		auto q = QPainter(&_frame);
		_ministars.paint(q, _ministarsRect);
		q.setCompositionMode(QPainter::CompositionMode_SourceIn);
		q.drawImage(0, 0, _mask);
	}

	p.drawImage(_position, _frame);
}

void ColoredMiniStars::setPaused(bool paused) {
	_ministars.setPaused(paused);
}

void ColoredMiniStars::setCenter(const QRect &rect) {
	const auto center = rect.center();
	const auto size = QSize(
		rect.width() * Ui::Premium::MiniStars::kSizeFactor,
		rect.height());
	const auto ministarsRect = QRect(
		QPoint(center.x() - size.width(), center.y() - size.height()),
		QPoint(center.x() + size.width(), center.y() + size.height()));
	setPosition(ministarsRect.topLeft());
	setSize(ministarsRect.size());
}

} // namespace Premium
} // namespace Ui
