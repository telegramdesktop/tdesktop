/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_animated.h"

#include "ui/painter.h"

#include <QtMath>

namespace Editor {

ItemAnimated *ItemAnimated::asAnimated() {
	return this;
}

VideoTrim ItemAnimated::trim() const {
	return {};
}

Media::Encode::AnimatedEntity ItemAnimated::animatedEntity(
		const QTransform &sceneToCanvas) const {
	const auto composed = QTransform().scale(flipped() ? -1. : 1., 1.)
		* sceneTransform()
		* sceneToCanvas;
	const auto inner = entityRect();
	const auto m11 = composed.m11();
	const auto m12 = composed.m12();
	const auto m21 = composed.m21();
	const auto m22 = composed.m22();
	const auto scale = std::hypot(m11, m12);
	const auto mirrored = ((m11 * m22 - m12 * m21) < 0);
	const auto rotation = mirrored
		? (std::atan2(-m12, m22) * 180. / M_PI)
		: (std::atan2(m12, m11) * 180. / M_PI);
	const auto size = inner.size() * scale;
	const auto center = composed.map(inner.center());
	const auto segment = trim();
	return {
		.kind = entityKind(),
		.bytes = content(),
		.geometry = QRectF(
			center - QPointF(size.width() / 2., size.height() / 2.),
			size),
		.rotation = rotation,
		.flipped = mirrored,
		.from = segment.from,
		.till = segment.till,
	};
}

QRectF ItemAnimated::entityRect() const {
	return contentRect();
}

QRectF ItemAnimated::visibleRect() const {
	return fittedRect(_paintedFrameSize);
}

void ItemAnimated::paintFrame(
		QPainter *p,
		const QImage &frame,
		bool live,
		bool mirror) {
	if (frame.isNull()) {
		return;
	}
	_paintedFrameSize = frame.size();
	const auto ratio = style::DevicePixelRatio();
	const auto resultRect = visibleRect();
	if (live) {
		p->save();
		p->setRenderHint(QPainter::SmoothPixmapTransform);
		if (mirror) {
			p->translate(resultRect.center().x(), 0);
			p->scale(-1., 1.);
			p->translate(-resultRect.center().x(), 0);
		}
		p->drawImage(resultRect, frame);
		p->restore();
		return;
	}
	auto pixelSize = (resultRect.size() * ratio).toSize();
	if (pixelSize.width() > frame.width()) {
		pixelSize = frame.size();
	}
	if ((_preview.key != frame.cacheKey())
		|| (_preview.size != pixelSize)
		|| (_preview.flipped != mirror)) {
		_preview.image = frame.scaled(
			pixelSize,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		if (mirror) {
			_preview.image = _preview.image.mirrored(true, false);
		}
		_preview.image.setDevicePixelRatio(ratio);
		_preview.key = frame.cacheKey();
		_preview.size = pixelSize;
		_preview.flipped = mirror;
	}
	p->drawImage(resultRect, _preview.image);
}

} // namespace Editor
