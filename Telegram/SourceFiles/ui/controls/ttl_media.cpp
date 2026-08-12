/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/ttl_media.h"

#include "base/unixtime.h"
#include "ui/arc_angles.h"
#include "ui/painter.h"
#include "ui/rect.h"

#include <QSvgRenderer>

namespace Ui {

TtlCountdown::TtlCountdown(Fn<void()> repaint)
: animation(std::move(repaint)) {
}

std::unique_ptr<TtlCountdown> MakeTtlCountdown(
		TimeId destroyAt,
		crl::time ttl,
		Fn<void()> repaint) {
	if (destroyAt <= 0) {
		return nullptr;
	}
	auto result = std::make_unique<TtlCountdown>(std::move(repaint));
	result->deadline = crl::now()
		+ (destroyAt - base::unixtime::now()) * crl::time(1000);
	result->total = std::max(ttl, crl::time(1)) * crl::time(1000);
	result->animation.start();
	return result;
}

void PaintTtlCountdown(
		QPainter &p,
		QRect inner,
		int line,
		not_null<TtlCountdown*> countdown,
		const style::color &color,
		bool paused) {
	const auto left = countdown->deadline - crl::now();
	if (left <= 0) {
		countdown->animation.stop();
		return;
	}
	const auto length = int(base::SafeRound(std::clamp(
		left / float64(countdown->total),
		0.,
		1.) * arc::kFullLength));
	if (!length) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	auto pen = color->p;
	pen.setWidthF(line);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.drawArc(inner, arc::kQuarterLength, length);

	if (paused) {
		return;
	}
	const auto angle = ((arc::kQuarterLength + length) / 16.)
		* M_PI / 180.;
	const auto cosa = std::cos(angle);
	const auto sina = std::sin(angle);
	const auto rect = QRectF(inner);
	const auto radius = rect.width() / 2.;
	const auto center = rect.center();
	countdown->particles.paint(
		p,
		QPointF(center.x() + radius * cosa, center.y() - radius * sina),
		QPointF(-sina, -cosa),
		color->c,
		1.);
}

void PaintTtlFireIcon(QPainter &p, QRect inner, QImage &cache) {
	const auto ratio = style::DevicePixelRatio();
	if (cache.size() != inner.size() * ratio) {
		cache = QImage(
			inner.size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(ratio);
		cache.fill(Qt::transparent);
		auto q = QPainter(&cache);
		auto svg = QSvgRenderer(u":/gui/ttl/video_message_icon.svg"_q);
		svg.render(&q, Rect(inner.size()) - Margins(inner.width() / 4));
	}
	p.drawImage(inner.topLeft(), cache);
}

} // namespace Ui
