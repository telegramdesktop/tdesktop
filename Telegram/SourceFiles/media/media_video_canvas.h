/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media::Encode {

struct CanvasBackground {
	QColor top;
	QColor bottom;

	[[nodiscard]] bool valid() const {
		return top.isValid() && bottom.isValid();
	}
};

[[nodiscard]] CanvasBackground DominantCanvasBackground(const QImage &image);

[[nodiscard]] QRect CanvasPlacement(QSize image, QSize canvas);

void PaintCanvasBackground(
	QPainter &p,
	const QRect &rect,
	const CanvasBackground &background);

[[nodiscard]] QRect RoundedCanvasRect(const QRectF &rect);

[[nodiscard]] QImage FitOnCanvas(
	const QImage &image,
	QSize canvas,
	const CanvasBackground &background,
	QRect placement = QRect());

} // namespace Media::Encode
