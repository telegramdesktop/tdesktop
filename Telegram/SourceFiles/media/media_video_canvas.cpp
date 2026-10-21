/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/media_video_canvas.h"

namespace Media::Encode {
namespace {

constexpr auto kSampleSide = 240;
constexpr auto kSampleTop = 0.1;
constexpr auto kSampleBottom = 0.9;

[[nodiscard]] QColor AdaptColor(const QColor &color) {
	const auto hue = float64(color.hsvHueF());
	auto saturation = float64(color.hsvSaturationF());
	auto value = float64(color.valueF());
	value = std::clamp(value - 0.05, 0.15, 0.85);
	if (saturation > 0.1 && saturation <= 0.95) {
		if (saturation <= 0.5) {
			saturation = std::min(saturation + 0.2, 1.);
		} else if (saturation > 0.8) {
			saturation = std::max(saturation - 0.4, 0.);
		}
	}
	return QColor::fromHsvF(hue, saturation, value);
}

} // namespace

CanvasBackground DominantCanvasBackground(const QImage &image) {
	if (image.isNull()) {
		return {};
	}
	const auto sample = image.scaled(
		kSampleSide,
		kSampleSide,
		Qt::KeepAspectRatio,
		Qt::SmoothTransformation
	).convertToFormat(QImage::Format_ARGB32);
	if (sample.isNull()) {
		return {};
	}
	const auto x = sample.width() / 2;
	const auto top = sample.pixelColor(x, int(sample.height() * kSampleTop));
	const auto bottom = sample.pixelColor(
		x,
		std::min(int(sample.height() * kSampleBottom), sample.height() - 1));
	return { AdaptColor(top), AdaptColor(bottom) };
}

void PaintCanvasBackground(
		QPainter &p,
		const QRect &rect,
		const CanvasBackground &background) {
	if (!background.valid()) {
		return;
	}
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomLeft());
	gradient.setStops({
		{ 0., background.top },
		{ 1., background.bottom },
	});
	p.fillRect(rect, gradient);
}

} // namespace Media::Encode
