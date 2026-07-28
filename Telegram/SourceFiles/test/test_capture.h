/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtWidgets/QWidget>

namespace Test {

// In-process render of the widget itself — immune to occlusion by floating
// elements, other windows, or a locked desktop session.
[[nodiscard]] QImage GrabWidget(not_null<QWidget*> widget);

// Grabs the widget and crops to |logicalRect| (widget-local logical
// coordinates), handling device pixel ratio.
[[nodiscard]] QImage GrabRect(
	not_null<QWidget*> widget,
	const QRect &logicalRect);

// Near-uniform images are capture failures, never evidence.
[[nodiscard]] bool LooksBlank(const QImage &image);

// Saves under ScreenshotsDir(), appends .png when missing, emits the
// SCREENSHOT marker, and returns the absolute path (empty on failure).
QString SaveImage(const QImage &image, const QString &name);

// Grab + blank-check + save as one evidence-grade capture: a hidden widget,
// an empty grab, or a blank image is a logged FAIL, never a silent pass.
bool CaptureWidget(not_null<QWidget*> widget, const QString &name);
bool CaptureRect(
	not_null<QWidget*> widget,
	const QRect &logicalRect,
	const QString &name);

[[nodiscard]] QImage Crop(const QImage &image, const QRect &pixelRect);

// Nearest-neighbor upscale for readable small-target evidence.
[[nodiscard]] QImage Zoom(const QImage &image, int factor);

// Lays the images out side by side at their native pixel sizes (no
// rescaling), for same-scale comparisons.
[[nodiscard]] QImage ContactSheet(const std::vector<QImage> &images);

} // namespace Test
