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
constexpr auto kTallRatio = 1.29;
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

QRect CanvasPlacement(QSize image, QSize canvas) {
	if (image.isEmpty() || canvas.isEmpty()) {
		return QRect(QPoint(), canvas);
	}
	auto scale = canvas.width() / float64(image.width());
	if (image.height() / float64(image.width()) > kTallRatio) {
		scale = std::max(scale, canvas.height() / float64(image.height()));
	}
	const auto size = QSize(
		std::max(int(base::SafeRound(image.width() * scale)), 1),
		std::max(int(base::SafeRound(image.height() * scale)), 1));
	return QRect(
		QPoint(
			(canvas.width() - size.width()) / 2,
			(canvas.height() - size.height()) / 2),
		size);
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

QRect RoundedCanvasRect(const QRectF &rect) {
	return QRect(
		QPoint(
			int(base::SafeRound(rect.x())),
			int(base::SafeRound(rect.y()))),
		QSize(
			std::max(int(base::SafeRound(rect.width())), 1),
			std::max(int(base::SafeRound(rect.height())), 1)));
}

QImage FitOnCanvas(
		const QImage &image,
		QSize canvas,
		const CanvasBackground &background,
		QRect placement) {
	if (image.isNull() || canvas.isEmpty()) {
		return image;
	}
	if (placement.isEmpty()) {
		placement = CanvasPlacement(image.size(), canvas);
	}
	auto result = QImage(canvas, QImage::Format_ARGB32_Premultiplied);
	auto p = QPainter(&result);
	PaintCanvasBackground(p, result.rect(), background);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(placement, image);
	return result;
}

} // namespace Media::Encode
