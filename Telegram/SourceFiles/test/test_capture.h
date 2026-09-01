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

// The harness's one widget-identity formatter: the typeid name of the live
// instance plus its "x,y WxH" geometry. Every refusal here prints identities
// through it, so two logs name the same widget the same way and stay
// comparable line by line.
[[nodiscard]] QString WidgetDescription(not_null<QWidget*> widget);

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

// The one render root a capture of a box inside a layer may use.
//
// Ui::BoxContent's constructor sets Qt::WA_OpaquePaintEvent
// (ui/layers/box_content.h:117-119) and BoxContent::paintEvent fills with the
// delegate's style().bg only while that attribute is set
// (ui/layers/box_content.cpp:450-459), so a plain Ui::GenericBox paints its
// own background and the blank-root refusal short-circuits on it: a plain box
// is NOT refused. setNoContentMargin(true) clears the attribute again
// (box_content.h:224-230), which is what 53 call sites under
// Telegram/SourceFiles/ do, and that is the only shape the refusal fires for.
// Such a box paints no background of its own, and the Ui::BoxLayerWidget the
// layer stack wrapped it in is what paints instead
// (ui/layers/box_layer_widget.cpp:120-141), with the box as its direct child
// (:46) - so the walk is normally one hop, but it is written as a walk
// because a scenario resolves from a descendant just as often.
//
// Run 1 of 2026/08/28/complete-server-history-details-hash-and-paging handed
// the bare box to Runner::captureAndInspect: PreparedWidgetCapture::prepare()
// refused every frame it was offered and the stage could only end in a
// timeout, although the refusal it printed already named the widget to grab -
// "grab N2Ui14BoxLayerWidgetE 14,72 364x413 instead (unpainted 0/1000)".
// This answers that once instead of leaving every call site to hand-roll it.
//
// The walk stops at widget->window() and refuses there, so it never leaves
// the target's own window looking for a layer. That is also why it must never
// be used on a Ui::PopupMenu: a popup is its own window, so the walk refuses
// on its first hop, and Ui::PopupMenu::init() sets Qt::WA_NoSystemBackground
// (ui/widgets/popup_menu.cpp:126), so the blank-root refusal never applies to
// it and it needs no layer root at all.
//
// |widget| is non-null exactly when |refusal| is empty: a caller cannot take
// the pointer without being handed the reason there is none.
struct PaintingLayerRootResult {
	QWidget *widget = nullptr;
	QString refusal;

	[[nodiscard]] bool resolved() const {
		return widget != nullptr;
	}
};

[[nodiscard]] PaintingLayerRootResult PaintingLayerRoot(QWidget *box);

// Saves the box's own rect, grabbed out of the Ui::BoxLayerWidget that paints
// it. An unresolved root is a logged FAIL carrying the refusal above, never a
// null the caller has to re-check before CaptureMappedRect, which this
// composes and which takes not_null<QWidget*>.
bool CaptureInLayerRoot(not_null<QWidget*> box, const QString &name);

[[nodiscard]] QImage Crop(const QImage &image, const QRect &pixelRect);

// Nearest-neighbor upscale for readable small-target evidence.
[[nodiscard]] QImage Zoom(const QImage &image, int factor);

// Lays the images out side by side at their native pixel sizes (no
// rescaling), for same-scale comparisons.
[[nodiscard]] QImage ContactSheet(const std::vector<QImage> &images);

} // namespace Test
