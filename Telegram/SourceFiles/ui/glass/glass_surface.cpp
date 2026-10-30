/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/glass/glass_surface.h"

#include "ui/glass/glass_policy.h"
#include "ui/glass/glass_tokens.h"

#include <QtCore/QMargins>
#include <QtCore/QRect>
#include <QtCore/QRectF>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

namespace Glass {

bool Painting() {
	return (Current().quality != Quality::Off);
}

void PaintSurface(QPainter &p, const QRect &available) {
	if (!Painting()) {
		return;
	}
	const auto spacing = Spacing();
	const auto material = CurrentMaterial();
	auto rect = available.marginsRemoved(QMargins(
		spacing.small,
		spacing.small,
		spacing.small,
		spacing.small));
	if (rect.isEmpty()) {
		return;
	}
	const auto width = material.strokeWidth;
	const auto radius = Radii().medium;
	auto path = QPainterPath();
	path.addRoundedRect(QRectF(rect), radius, radius);
	p.fillPath(path, material.bg);
	if (width > 0) {
		p.strokePath(path, QPen(material.stroke, width));
	}
	if (material.highlightWidth > 0 && radius > width) {
		auto inner = QPainterPath();
		inner.addRoundedRect(
			QRectF(rect.marginsRemoved(QMargins(width, width, width, width))),
			radius - width,
			radius - width);
		p.strokePath(inner, QPen(material.highlight, material.highlightWidth));
	}
}

} // namespace Glass
