/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/torn_edge.h"

#include "ui/painter.h"

#include "styles/style_chat.h"

namespace Ui {
namespace {

[[nodiscard]] QImage GenerateTornEdgeMaskTop(int width) {
	const auto amplitude = st::unsupportedTearAmplitude;
	const auto period = st::unsupportedTearPeriod;
	const auto jitter = period / 3;
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(width, amplitude) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);

	auto seed = uint32(width) * 2654435761U;
	const auto next = [&] {
		seed = seed * 1664525U + 1013904223U;
		return (seed >> 16);
	};
	const auto level = [&] {
		return amplitude * ((next() % 1024) / 1023.);
	};
	auto path = QPainterPath();
	path.moveTo(0, level());
	for (auto x = 0; x < width;) {
		const auto step = std::max(
			period + int(next() % uint32(2 * jitter + 1)) - jitter,
			1);
		x = std::min(x + step, width);
		path.lineTo(x, level());
	}
	path.lineTo(width, amplitude);
	path.lineTo(0, amplitude);
	path.closeSubpath();

	auto p = QPainter(&result);
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	p.drawPath(path);
	p.end();

	return result;
}

} // namespace

void ValidateTornEdges(TornEdgeCache &cache, int width) {
	if (cache.width == width) {
		return;
	}
	cache.width = width;
	cache.maskTop = GenerateTornEdgeMaskTop(width);
	cache.maskBottom = cache.maskTop.mirrored(false, true);
	cache.maskBottom.setDevicePixelRatio(cache.maskTop.devicePixelRatio());
	cache.patternCacheTop = QImage();
	cache.patternCacheBottom = QImage();
	cache.solidCacheTop = QImage();
	cache.solidCacheBottom = QImage();
	cache.solidColorTop = QColor();
	cache.solidColorBottom = QColor();
}

} // namespace Ui
