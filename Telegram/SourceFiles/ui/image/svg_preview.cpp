/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/image/svg_preview.h"

#include "base/debug_log.h"
#include "ui/image/svg_safety.h"

#include <QtGui/QPainter>
#include <QtSvg/QSvgRenderer>

namespace Ui {
namespace {

constexpr auto kMaxSvgSize = 1 * 1024 * 1024;
constexpr auto kMaxDefaultRenderSize = 4096;

} // namespace

int SvgPreviewBytesLimit() {
	return kMaxSvgSize;
}

QImage RenderSvgPreview(const QByteArray &bytes, QSize maxSize) {
	if (bytes.isEmpty()) {
		return {};
	}
	if (bytes.size() > kMaxSvgSize) {
		LOG(("Svg Error: File too large (%1 bytes).").arg(bytes.size()));
		return {};
	}
	const auto sanitized = Images::SanitizeSvg(bytes);
	if (sanitized.isEmpty()) {
		return {};
	}
	auto renderer = QSvgRenderer();
	if (!renderer.load(sanitized) || !renderer.isValid()) {
		LOG(("Svg Error: Invalid data."));
		return {};
	}
	auto size = renderer.defaultSize();
	if (!maxSize.isEmpty()) {
		size = size.scaled(maxSize, Qt::KeepAspectRatio);
	} else if ((size.width() > kMaxDefaultRenderSize)
		|| (size.height() > kMaxDefaultRenderSize)) {
		size = size.scaled(
			kMaxDefaultRenderSize,
			kMaxDefaultRenderSize,
			Qt::KeepAspectRatio);
	}
	if ((size.width() <= 0) || (size.height() <= 0)) {
		LOG(("Svg Error: Bad size %1x%2."
			).arg(renderer.defaultSize().width()
			).arg(renderer.defaultSize().height()));
		return {};
	}
	auto rendered = QImage(size, QImage::Format_ARGB32_Premultiplied);
	rendered.fill(Qt::transparent);
	{
		QPainter p(&rendered);
		renderer.render(&p, QRect(QPoint(), size));
	}
	return rendered;
}

} // namespace Ui
