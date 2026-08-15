/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_capture.h"

#include "test/test_log.h"
#include "ui/ui_utility.h"

#include <QtGui/QPainter>

namespace Test {
namespace {

constexpr auto kBlankSpreadThreshold = 6;
constexpr auto kContactSheetGap = 8;

[[nodiscard]] QString WithPngExtension(const QString &name) {
	return name.endsWith(u".png"_q, Qt::CaseInsensitive)
		? name
		: (name + u".png"_q);
}

[[nodiscard]] QString RectText(const QRect &rect) {
	return u"%1,%2 %3x%4"_q
		.arg(rect.x())
		.arg(rect.y())
		.arg(rect.width())
		.arg(rect.height());
}

[[nodiscard]] QString MisframedDetails(
		not_null<QWidget*> widget,
		const QRect &logicalRect) {
	const auto bounds = widget->rect();
	if (!logicalRect.isEmpty() && bounds.contains(logicalRect)) {
		return QString();
	}
	const auto inside = bounds.intersected(logicalRect);
	return u"requested rect is not fully inside the grabbed widget: "
		u"requested=%1 widget=%2 inside=%3 rows=%4/%5 columns=%6/%7"_q
		.arg(RectText(logicalRect), RectText(bounds), RectText(inside))
		.arg(inside.height())
		.arg(logicalRect.height())
		.arg(inside.width())
		.arg(logicalRect.width());
}

} // namespace

QImage GrabWidget(not_null<QWidget*> widget) {
	return widget->grab().toImage();
}

QImage GrabRect(
		not_null<QWidget*> widget,
		const QRect &logicalRect) {
	const auto bounded = logicalRect.intersected(widget->rect());
	return bounded.isEmpty()
		? QImage()
		: widget->grab(bounded).toImage();
}

bool LooksBlank(const QImage &image) {
	if (image.isNull() || image.width() < 2 || image.height() < 2) {
		return true;
	}
	auto minLuma = 255;
	auto maxLuma = 0;
	const auto columns = std::min(image.width(), 32);
	const auto rows = std::min(image.height(), 32);
	for (auto y = 0; y != rows; ++y) {
		for (auto x = 0; x != columns; ++x) {
			const auto pixel = image.pixelColor(
				(x * (image.width() - 1)) / std::max(columns - 1, 1),
				(y * (image.height() - 1)) / std::max(rows - 1, 1));
			const auto luma = int(std::round(255 * pixel.lightnessF()));
			minLuma = std::min(minLuma, luma);
			maxLuma = std::max(maxLuma, luma);
		}
	}
	return (maxLuma - minLuma) < kBlankSpreadThreshold;
}

QString SaveImage(const QImage &image, const QString &name) {
	if (image.isNull()) {
		return QString();
	}
	const auto path = ScreenshotsDir() + WithPngExtension(name);
	if (!image.save(path, "PNG")) {
		return QString();
	}
	LogRaw(u"SCREENSHOT: %1"_q.arg(path));
	return path;
}

bool CaptureWidget(not_null<QWidget*> widget, const QString &name) {
	LogGeometry(name, QRect(widget->mapToGlobal(QPoint()), widget->size()));
	if (!widget->isVisible()) {
		Fail(u"capture %1"_q.arg(name), u"widget is not visible"_q);
		return false;
	}
	const auto image = GrabWidget(widget);
	if (LooksBlank(image)) {
		Fail(u"capture %1"_q.arg(name), u"grabbed image looks blank"_q);
		return false;
	}
	return !SaveImage(image, name).isEmpty();
}

bool CaptureRect(
		not_null<QWidget*> widget,
		const QRect &logicalRect,
		const QString &name) {
	LogGeometry(name, logicalRect);
	if (!widget->isVisible()) {
		Fail(u"capture %1"_q.arg(name), u"widget is not visible"_q);
		return false;
	}
	const auto misframed = MisframedDetails(widget, logicalRect);
	if (!misframed.isEmpty()) {
		Fail(u"capture %1"_q.arg(name), misframed);
		return false;
	}
	const auto image = GrabRect(widget, logicalRect);
	if (LooksBlank(image)) {
		Fail(u"capture %1"_q.arg(name), u"grabbed image looks blank"_q);
		return false;
	}
	return !SaveImage(image, name).isEmpty();
}

bool CaptureMappedRect(
		not_null<QWidget*> widget,
		not_null<QWidget*> rectOrigin,
		const QRect &logicalRect,
		const QString &name) {
	return CaptureRect(
		widget,
		Ui::MapFrom(widget, rectOrigin, logicalRect),
		name);
}

QImage Crop(const QImage &image, const QRect &pixelRect) {
	const auto bounded = pixelRect.intersected(image.rect());
	return bounded.isEmpty() ? QImage() : image.copy(bounded);
}

QImage Zoom(const QImage &image, int factor) {
	return (image.isNull() || factor <= 1)
		? image
		: image.scaled(
			image.size() * factor,
			Qt::KeepAspectRatio,
			Qt::FastTransformation);
}

QImage ContactSheet(const std::vector<QImage> &images) {
	auto width = 0;
	auto height = 0;
	for (const auto &image : images) {
		if (image.isNull()) {
			continue;
		}
		width += image.width() + (width ? kContactSheetGap : 0);
		height = std::max(height, image.height());
	}
	if (!width) {
		return QImage();
	}
	auto result = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::white);
	auto painter = QPainter(&result);
	auto x = 0;
	for (const auto &image : images) {
		if (image.isNull()) {
			continue;
		}
		auto copy = image;
		copy.setDevicePixelRatio(1.);
		painter.drawImage(x, 0, copy);
		x += copy.width() + kContactSheetGap;
	}
	painter.end();
	return result;
}

} // namespace Test

#endif // _DEBUG
