/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_enhance.h"

namespace Editor {
namespace {

constexpr auto kTilesPerSide = 4;
constexpr auto kTileCount = kTilesPerSide * kTilesPerSide;
constexpr auto kLevels = 256;

[[nodiscard]] int PixelValue(QRgb pixel) {
	return std::max({
		int(qRed(pixel)),
		int(qGreen(pixel)),
		int(qBlue(pixel)),
	});
}

[[nodiscard]] QImage Converted(const QImage &image) {
	return (image.format() == QImage::Format_ARGB32)
		? image
		: image.convertToFormat(QImage::Format_ARGB32);
}

[[nodiscard]] int Level(float value) {
	return std::clamp(int(std::round(value * 255.f)), 0, 255);
}

} // namespace

EnhanceLut PrepareEnhance(const QImage &image) {
	auto result = EnhanceLut();
	if (image.isNull()) {
		return result;
	}
	const auto tileWidth = image.width() / kTilesPerSide;
	const auto tileHeight = image.height() / kTilesPerSide;
	const auto tileArea = tileWidth * tileHeight;
	if (!tileArea) {
		return result;
	}
	const auto clipLimit = std::max(
		1,
		int(1.25f * tileArea / float(kLevels)));
	const auto source = Converted(image);
	for (auto ty = 0; ty != kTilesPerSide; ++ty) {
		const auto fromY = ty * tileHeight;
		const auto toY = fromY + tileHeight;
		for (auto tx = 0; tx != kTilesPerSide; ++tx) {
			const auto fromX = tx * tileWidth;
			const auto toX = fromX + tileWidth;
			auto histogram = std::array<int, kLevels>();
			histogram.fill(0);
			for (auto y = fromY; y != toY; ++y) {
				const auto line = reinterpret_cast<const QRgb*>(
					source.constScanLine(y));
				for (auto x = fromX; x != toX; ++x) {
					++histogram[PixelValue(line[x])];
				}
			}
			auto excess = 0;
			for (auto &count : histogram) {
				if (count > clipLimit) {
					excess += count - clipLimit;
					count = clipLimit;
				}
			}
			const auto share = excess / kLevels;
			const auto remainder = excess % kLevels;
			for (auto i = 0; i != kLevels; ++i) {
				histogram[i] += share + ((i < remainder) ? 1 : 0);
			}
			auto &tile = result.tiles[ty * kTilesPerSide + tx];
			auto sum = 0;
			for (auto i = 0; i != kLevels; ++i) {
				sum += histogram[i];
				tile.cdf[i] = uint8(std::clamp(
					int(255.f * sum / float(tileArea)),
					0,
					255));
			}
			for (const auto value : tile.cdf) {
				if (value) {
					tile.cdfMin = value;
					break;
				}
			}
			tile.cdfMax = tile.cdf[kLevels - 1];
		}
	}
	result.valid = true;
	return result;
}

QImage ApplyEnhance(
		const QImage &image,
		const EnhanceLut &lut,
		int amount) {
	if (amount <= 0 || !lut.valid || image.isNull()) {
		return image;
	}
	const auto intensity = std::clamp(amount, 0, 100) / 100.f;
	auto normalized = std::array<std::array<float, kLevels>, kTileCount>();
	for (auto i = 0; i != kTileCount; ++i) {
		const auto &tile = lut.tiles[i];
		const auto minValue = int(tile.cdfMin);
		const auto maxValue = int(tile.cdfMax);
		const auto range = maxValue - minValue;
		for (auto j = 0; j != kLevels; ++j) {
			normalized[i][j] = (range > 0)
				? float(int(tile.cdf[j]) - minValue) / float(range)
				: float(tile.cdf[j]) / 255.f;
		}
	}
	auto result = Converted(image);
	const auto width = result.width();
	const auto height = result.height();
	for (auto y = 0; y != height; ++y) {
		const auto line = reinterpret_cast<QRgb*>(result.scanLine(y));
		const auto v = (y + 0.5f) / float(height);
		const auto cv = (std::clamp(v, 0.125f, 0.87499f) - 0.125f) * 4.f;
		const auto ty0 = std::clamp(int(std::floor(cv)), 0, 2);
		const auto ty1 = ty0 + 1;
		const auto fractionY = cv - float(ty0);
		for (auto x = 0; x != width; ++x) {
			const auto pixel = line[x];
			const auto red = qRed(pixel) / 255.f;
			const auto green = qGreen(pixel) / 255.f;
			const auto blue = qBlue(pixel) / 255.f;
			const auto value = std::max({ red, green, blue });
			const auto minimum = std::min({ red, green, blue });
			const auto delta = value - minimum;
			const auto saturation = (value > 1e-6f)
				? (delta / value)
				: 0.f;
			auto hue = 0.f;
			if (delta > 0.f) {
				if (value == red) {
					hue = (green - blue) / delta
						+ ((green < blue) ? 6.f : 0.f);
				} else if (value == green) {
					hue = (blue - red) / delta + 2.f;
				} else {
					hue = (red - green) / delta + 4.f;
				}
				hue /= 6.f;
			}
			const auto u = (x + 0.5f) / float(width);
			const auto cu = (std::clamp(u, 0.125f, 0.87499f) - 0.125f) * 4.f;
			const auto tx0 = std::clamp(int(std::floor(cu)), 0, 2);
			const auto tx1 = tx0 + 1;
			const auto fractionX = cu - float(tx0);
			const auto level = std::clamp(
				int(value * 255.f + 0.5f),
				0,
				255);
			const auto &n00 = normalized[ty0 * kTilesPerSide + tx0];
			const auto &n01 = normalized[ty0 * kTilesPerSide + tx1];
			const auto &n10 = normalized[ty1 * kTilesPerSide + tx0];
			const auto &n11 = normalized[ty1 * kTilesPerSide + tx1];
			const auto top = n00[level]
				+ (n01[level] - n00[level]) * fractionX;
			const auto bottom = n10[level]
				+ (n11[level] - n10[level]) * fractionX;
			const auto enhanced = std::clamp(
				top + (bottom - top) * fractionY,
				0.f,
				1.f);
			const auto targetSaturation = std::min(1.f, saturation * 1.2f);
			const auto targetValue = std::min(1.f, enhanced * 1.1f);
			const auto finalSaturation = saturation
				+ (targetSaturation - saturation) * intensity;
			const auto finalValue = value
				+ (targetValue - value) * intensity;
			auto finalRed = finalValue;
			auto finalGreen = finalValue;
			auto finalBlue = finalValue;
			if (finalSaturation > 0.f) {
				const auto sector = hue * 6.f;
				const auto index = int(sector) % 6;
				const auto fraction = sector - std::floor(sector);
				const auto p = finalValue * (1.f - finalSaturation);
				const auto q = finalValue
					* (1.f - finalSaturation * fraction);
				const auto t = finalValue
					* (1.f - finalSaturation * (1.f - fraction));
				switch (index) {
				case 0: finalGreen = t; finalBlue = p; break;
				case 1: finalRed = q; finalBlue = p; break;
				case 2: finalRed = p; finalBlue = t; break;
				case 3: finalRed = p; finalGreen = q; break;
				case 4: finalRed = t; finalGreen = p; break;
				default: finalGreen = p; finalBlue = q; break;
				}
			}
			line[x] = qRgba(
				Level(finalRed),
				Level(finalGreen),
				Level(finalBlue),
				qAlpha(pixel));
		}
	}
	return result;
}

QImage EnhanceImage(const QImage &image, int amount) {
	if (amount <= 0) {
		return image;
	}
	return ApplyEnhance(image, PrepareEnhance(image), amount);
}

} // namespace Editor
