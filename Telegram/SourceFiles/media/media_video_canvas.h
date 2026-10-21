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

void PaintCanvasBackground(
	QPainter &p,
	const QRect &rect,
	const CanvasBackground &background);

} // namespace Media::Encode
