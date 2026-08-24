/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/flat_map.h"

namespace Ui {

class RowsScrollCache final {
public:
	explicit RowsScrollCache(Fn<void()> stopped);

	void markScrolling();
	[[nodiscard]] bool scrolling() const {
		return _scrolling;
	}
	[[nodiscard]] bool hasFresh(uint64 rowId, QSize physicalSize) const {
		const auto i = _images.find(rowId);
		return (i != end(_images)) && (i->second.size() == physicalSize);
	}

	template <typename PaintToImage>
	void paintRow(
			QPainter &p,
			uint64 rowId,
			QSize physicalSize,
			int ratio,
			PaintToImage &&paintToImage) {
		if ((_images.size() > kLimit || _memory > kMemoryLimit)
			&& !_images.contains(rowId)) {
			clear();
		}
		if (const auto i = _images.find(rowId)
			; i != end(_images) && i->second.size() == physicalSize) {
			p.drawImage(0, 0, i->second);
			return;
		}
		auto image = QImage(physicalSize, QImage::Format_RGB32);
		image.setDevicePixelRatio(ratio);
		paintToImage(image);
		p.drawImage(0, 0, image);
		auto &slot = _images[rowId];
		_memory += image.sizeInBytes() - slot.sizeInBytes();
		slot = std::move(image);
	}

	void invalidate(uint64 rowId);
	void clear();

private:
	static constexpr auto kLimit = 256;
	static constexpr auto kMemoryLimit = 32 * 1024 * 1024;

	base::flat_map<uint64, QImage> _images;
	base::Timer _stopTimer;
	int64 _memory = 0;
	bool _scrolling = false;

};

} // namespace Ui
