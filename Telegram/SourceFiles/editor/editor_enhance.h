/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtGui/QImage>

namespace Editor {

struct EnhanceLut {
	struct Tile {
		std::array<uint8, 256> cdf = {};
		uint8 cdfMin = 0;
		uint8 cdfMax = 255;
	};
	std::array<Tile, 16> tiles = {};
	bool valid = false;
};

[[nodiscard]] EnhanceLut PrepareEnhance(const QImage &image);

[[nodiscard]] QImage ApplyEnhance(
	const QImage &image,
	const EnhanceLut &lut,
	int amount);

[[nodiscard]] QImage EnhanceImage(const QImage &image, int amount);

} // namespace Editor
