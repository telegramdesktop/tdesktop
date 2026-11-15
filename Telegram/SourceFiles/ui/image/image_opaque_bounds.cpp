/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/image/image_opaque_bounds.h"

namespace Ui {

QRect OpaqueBounds(const QImage &image) {
	Expects(image.format() == QImage::Format_ARGB32_Premultiplied);

	const auto width = image.width();
	const auto height = image.height();
	const auto rowHasContent = [&](int y) {
		const auto row = reinterpret_cast<const uint32*>(
			image.constScanLine(y));
		for (auto x = 0; x != width; ++x) {
			if (row[x]) {
				return true;
			}
		}
		return false;
	};
	auto top = 0;
	while (top != height && !rowHasContent(top)) {
		++top;
	}
	if (top == height) {
		return QRect();
	}
	auto bottom = height - 1;
	while (bottom > top && !rowHasContent(bottom)) {
		--bottom;
	}
	auto left = width;
	auto right = -1;
	for (auto y = top; y <= bottom; ++y) {
		const auto row = reinterpret_cast<const uint32*>(
			image.constScanLine(y));
		auto x = 0;
		while (x < left && !row[x]) {
			++x;
		}
		left = std::min(left, x);
		x = width - 1;
		while (x > right && !row[x]) {
			--x;
		}
		right = std::max(right, x);
	}
	return QRect(left, top, right - left + 1, bottom - top + 1);
}

} // namespace Ui
