/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QPointer>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>

namespace Test {

// In-process renders of the widget itself are immune to occlusion by floating
// elements, other windows, or a locked desktop session. GrabWidget uses the
// full bounds; GrabRect renders |logicalRect| in widget-local coordinates,
// clamped to the bounds, while handling device pixel ratio. A rect outside the
// visible area is still produced. Both initialize non-opaque grabs with active
// st::windowBg; uncovered pixels measure that harness base rather than widget
// paint, while the blank-root refusal probes paint coverage separately.
[[nodiscard]] QImage GrabWidget(not_null<QWidget*> widget);

[[nodiscard]] QImage GrabRect(
	not_null<QWidget*> widget,
	const QRect &logicalRect);

// Near-uniform images are capture failures, never evidence.
[[nodiscard]] bool LooksBlank(const QImage &image);

// Pollable capture state for animated/layer-owned surfaces. prepare() accepts
// a frame only after the exact target is visible, non-empty, nonblank, and a
// valid paint root. save() persists that same accepted frame, so the target
// cannot regress between a runner's readiness check and its evidence step.
class PreparedWidgetCapture final {
public:
	[[nodiscard]] bool prepare(QWidget *widget);
	void invalidate(QString reason);
	[[nodiscard]] bool save(const QString &name);

	[[nodiscard]] QWidget *widget() const;
	[[nodiscard]] const QImage &image() const;
	[[nodiscard]] QString pendingReason() const;

private:
	QPointer<QWidget> _widget;
	QImage _image;
	QRect _globalGeometry;
	QString _pendingReason;
};

// Saves under ScreenshotsDir(), appends .png when missing, emits the
// SCREENSHOT marker, and returns the absolute path (empty on failure).
QString SaveImage(const QImage &image, const QString &name);

// Grab + blank-check + save as one evidence-grade capture: a hidden widget,
// an empty grab, or a blank image is a logged FAIL, never a silent pass.
// CaptureRect's rect must lie fully inside the grabbed widget; a rect that
// leaves it is a logged FAIL naming the overlap, never a silently reframed
// image.
bool CaptureWidget(not_null<QWidget*> widget, const QString &name);
bool CaptureRect(
	not_null<QWidget*> widget,
	const QRect &logicalRect,
	const QString &name);

// Captures |logicalRect|, expressed in |rectOrigin| coordinates, by grabbing
// |widget| — the rect is mapped into |widget| first. A grab renders the
// widget itself, so a row scrolled out of view is captured by passing the
// scrolled content widget as |widget|; the window that clips it holds no
// pixels for that row.
bool CaptureMappedRect(
	not_null<QWidget*> widget,
	not_null<QWidget*> rectOrigin,
	const QRect &logicalRect,
	const QString &name);

[[nodiscard]] QImage Crop(const QImage &image, const QRect &pixelRect);

// Nearest-neighbor upscale for readable small-target evidence.
[[nodiscard]] QImage Zoom(const QImage &image, int factor);

// Lays the images out side by side at their native pixel sizes (no
// rescaling), for same-scale comparisons.
[[nodiscard]] QImage ContactSheet(const std::vector<QImage> &images);

} // namespace Test
