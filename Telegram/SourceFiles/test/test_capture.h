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

// The harness's one rect formatter: "x,y WxH". Every refusal that quotes a
// rect prints through it, so two logs name the same rect the same way and
// stay comparable line by line, exactly as the widget-identity formatter
// below does for a widget.
[[nodiscard]] QString RectText(const QRect &rect);

// The harness's one misframing refusal: empty when |logicalRect| is non-empty
// and lies fully inside |widget|, otherwise the named refusal quoting both
// rects and the overlap. Every helper that refuses a rect prints through it,
// so two logs name the same fault the same way.
[[nodiscard]] QString MisframedDetails(
	not_null<QWidget*> widget,
	const QRect &logicalRect);

// The harness's one widget-identity formatter: the typeid name of the live
// instance plus its "x,y WxH" geometry. Every refusal here prints identities
// through it, so two logs name the same widget the same way and stay
// comparable line by line.
[[nodiscard]] QString WidgetDescription(not_null<QWidget*> widget);

// Near-uniform images are capture failures, never evidence.
[[nodiscard]] bool LooksBlank(const QImage &image);

class MappedRect;

// Pollable capture state for animated/layer-owned surfaces. prepare() accepts
// a frame only after the exact target is visible, non-empty, nonblank, and a
// valid paint root. save() persists that same accepted frame, so the target
// cannot regress between a runner's readiness check and its evidence step.
//
// prepareCrop() is the checked full-owner composition: it renders the whole
// |owner| through prepare(owner) and crops an owner-local logical |crop| out
// of that render, and accepts only after, in this order, (a) |descendant| is
// a real descendant of |owner| and not |owner| itself - an owner-as-origin
// substitution loses the descendant's scroll ancestry and is refused by
// name - (b) ReadMappedTarget(owner, descendant, descendant->rect())
// resolves, so the whole descendant lies inside the owner and its actual
// viewport (refusal prefixed "descendant: "), (c) |crop| is non-empty and
// fully inside owner->rect() (refusal prefixed "crop: ", quoting the
// requested rect, the owner extent and the inside rows and columns), then
// prepare(owner)'s own refusals, and (d) DevicePixelRect(crop, ratio) lies
// inside the full render and the cropped image has that entire device
// extent. Crop()'s silent intersection is never relied on: a crop that would
// come back shorter is refused before any image is accepted. Every refusal
// goes through invalidate(), so image() is then null and pendingReason()
// names it; on acceptance widget() is |owner|, image() is the crop and
// save() persists it. Reference: docs/helpers/test_capture.md, "Checked
// full-owner crops".
//
// prepare(owner, target) is the typed mapped composition: a refused
// ReadMappedTarget(owner, target) goes through invalidate() with that
// reading's refusal, otherwise it is exactly
// prepare(owner, target.origin(), target.rect()).
class PreparedWidgetCapture final {
public:
	[[nodiscard]] bool prepare(QWidget *widget);
	[[nodiscard]] bool prepare(
		QWidget *owner,
		QWidget *origin,
		const QRect &localRect);
	[[nodiscard]] bool prepare(
		QWidget *owner,
		const MappedRect &target);
	[[nodiscard]] bool prepareCrop(
		QWidget *owner,
		QWidget *descendant,
		const QRect &crop);
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

// Saves a PNG under ScreenshotsDir(), appending .png when missing, and
// returns its absolute path. A null image or a failed write returns empty
// and logs nothing. A successful save logs SCREENSHOT: "<relative>", the
// path relative to EvidenceDir() with \ and " backslash-escaped, so the log
// never names the host evidence root. Escaping and resolution:
// docs/helpers/test_capture.md, "Saved images and the SCREENSHOT marker".
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

// A mapped rectangle bound to the widget that paints it, so a call site
// names the coordinate space of its rect and a mismatched pairing is refused
// when it is read instead of applying a child's position twice.
//
// Local(w, rect): |rect| is in |w|'s own coordinates, w->rect() or a
// sub-rect of what |w| paints. InParent(w, rect): |rect| is a frame |w|
// reports relative to its parent, w->geometry() or a ...Geometry() accessor
// that translates by pos(). A rect that also covers the parent's own
// painting is Local(parent, rect). widget() is the painted widget; origin()
// is the widget the rect's coordinates belong to: |w| for Local, its parent
// for InParent, and null once |w| is destroyed or, for InParent, when |w|
// has no parent or is a window (a window's geometry is not in its parent's
// coordinates). The widget is a QPointer, so a retained value never
// dangles. The two factories are the only construction. Reference:
// docs/helpers/test_capture.md, "Coordinate owners of mapped rectangles".
class MappedRect final {
public:
	[[nodiscard]] static MappedRect Local(
		not_null<QWidget*> widget,
		const QRect &rect);
	[[nodiscard]] static MappedRect InParent(
		not_null<QWidget*> widget,
		const QRect &rect);

	[[nodiscard]] QWidget *widget() const;
	[[nodiscard]] QWidget *origin() const;
	[[nodiscard]] QRect rect() const;
	[[nodiscard]] bool parentRelative() const;

private:
	MappedRect(
		not_null<QWidget*> widget,
		const QRect &rect,
		bool parentRelative);

	QPointer<QWidget> _widget;
	QRect _rect;
	bool _parentRelative = false;

};

// Complete-target readiness for a mapped capture.
//
// The caller supplies the real rectangle origin, the painted owner that
// CaptureMappedRect will grab, and an exact origin-local rectangle. The
// helper maps with Ui::MapFrom and reports ready only when that complete
// mapped rectangle lies fully inside the owner and the relevant viewport.
// Whole-rectangle containment, never overlap: it does not intersect, clip,
// or reframe the requested rect to make the check pass. An empty, hidden,
// or partially clipped target is unready, including a rectangle that fits
// the owner but is clipped by the scroll that actually paints it.
//
// The relevant viewport is discovered from the origin's ancestors: a
// Ui::ElasticScroll is itself the clipper (its viewport() is Dummy and
// returns the inner content widget — treating that pointer as the clipper
// recreates origin-local containment always succeeding). A
// QAbstractScrollArea uses viewport(). The walk does not stop at the
// owner, because when the owner is the inner content the clipper is its
// parent. No clipper means only owner containment applies.
//
// |origin| and |owner| are non-null exactly when |refusal| is empty at the
// moment the reading is taken. They are QPointer<QWidget>, so a retained
// reading answers resolved() false once either widget is destroyed, while
// |local|, |mapped|, |inViewport|, |identity| and |refusal| stay printable.
// |viewport| may be null when there is no scrolling ancestor.
//
// MappedTargetReady is that geometry reading. It is not capture-ready: a
// nonpainting owner whose geometry fits is still refused by
// PreparedWidgetCapture::prepare(owner, origin, rect) and by
// CaptureMappedTarget, which then compose CaptureMappedRect so owner
// misframing, blank, and blank-root refusals remain the existing helpers.
// CaptureMappedRect itself is unchanged and still grabs a scrolled-out
// row when the grabbed widget is the content widget.
//
// Capture selection: those compositions render the mapped rect directly,
// a shifted render origin. A region cropped out of a full-owner render,
// which must keep its whole requested extent while its originating
// descendant is certified whole inside its viewport, is
// PreparedWidgetCapture::prepareCrop above.
//
// The typed path binds the rect to its coordinate owner: the overloads that
// take a MappedRect instead of an origin and a rect. Typed readiness is
// ReadMappedTarget(owner, target).resolved(). That reading refuses, in
// order: a null owner or a destroyed painted widget (the untyped null
// refusals); "detached:" (an InParent rect without a parent coordinate
// space); "wrong owner:" (the painted widget is neither |owner| nor inside
// it); "painted widget is not visible:" (a hidden InParent widget, whose
// rect would otherwise map through its parent alone); "coordinate
// mismatch:" (a non-empty rect not fully inside the painted widget's own
// bounds in its space, rect() for Local and geometry() for InParent - the
// doubled-position signature of a parent-relative rect handed as local, or
// the reverse - quoting the requested rect, the space, origin, painted
// widget, bounds, owner and where the rect would have mapped). Otherwise it
// is exactly the untyped ReadMappedTarget(owner, target.origin(),
// target.rect()). Every typed reading carries |binding|, the bound rect's
// description; the untyped readings leave it empty, and
// MappedTargetDetails prints it only when set. The typed
// PreparedWidgetCapture::prepare and CaptureMappedTarget store or log that
// reading's refusal and otherwise compose their untyped overloads, which
// keep their exact behaviour for compatible callers. Child geometry, such
// as a ...Geometry() accessor that translates by pos(), must go through
// MappedRect::InParent.
//
// PaintingLayerRoot still resolves boxes inside layers specifically. This
// reading does not walk to a Ui::BoxLayerWidget, and callers still own
// semantic navigation and exact item identity.
struct MappedTarget {
	QPointer<QWidget> origin;
	QPointer<QWidget> owner;
	QPointer<QWidget> viewport;
	QRect local;
	QRect mapped;
	QRect inViewport;
	QString identity;
	QString refusal;
	QString binding;

	[[nodiscard]] bool resolved() const {
		return origin && owner && refusal.isEmpty();
	}
};

[[nodiscard]] MappedTarget ReadMappedTarget(
	QWidget *owner,
	QWidget *origin,
	const QRect &localRect);
[[nodiscard]] bool MappedTargetReady(
	QWidget *owner,
	QWidget *origin,
	const QRect &localRect);
[[nodiscard]] QString MappedTargetDetails(const MappedTarget &reading);
bool CaptureMappedTarget(
	QWidget *owner,
	QWidget *origin,
	const QRect &localRect,
	const QString &name);
[[nodiscard]] MappedTarget ReadMappedTarget(
	QWidget *owner,
	const MappedRect &target);
bool CaptureMappedTarget(
	QWidget *owner,
	const MappedRect &target,
	const QString &name);

// The one render root a capture of a box inside a layer may use.
//
// Ui::BoxContent's constructor sets Qt::WA_OpaquePaintEvent
// (ui/layers/box_content.h) and BoxContent::paintEvent fills with the
// delegate's style().bg only while that attribute is set
// (ui/layers/box_content.cpp), so a plain Ui::GenericBox paints its own
// background and the blank-root refusal short-circuits on it: a plain box is
// NOT refused. setNoContentMargin(true) clears the attribute again
// (box_content.h), which is what 53 call sites under Telegram/SourceFiles/
// do, and that is the only shape the refusal fires for. Such a box paints no
// background of its own, and the Ui::BoxLayerWidget the layer stack wrapped
// it in is what paints instead (BoxLayerWidget::paintEvent in
// ui/layers/box_layer_widget.cpp), with the box as its direct child
// (BoxLayerWidget::BoxLayerWidget) - so the walk is normally one hop, but it
// is written as a walk because a scenario resolves from a descendant just as
// often.
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
// (ui/widgets/popup_menu.cpp), so the blank-root refusal never applies to it
// and it needs no layer root at all.
//
// |widget| is non-null exactly when |refusal| is empty at the moment the
// reading is taken: a caller cannot take the pointer without being handed
// the reason there is none. It is a QPointer<QWidget>, which is what makes
// a reading safe to keep: it can only go from non-null to null and never
// back, so once the layer stack destroys the resolved Ui::BoxLayerWidget
// resolved() answers false on the very object the caller is still holding,
// instead of handing back a pointer into freed memory. |refusal| is a value
// and does not change with it - a reading that resolved and then lost its
// widget answers resolved() == false with an empty |refusal|, so a caller
// that needs to print something prints what it recorded while the reading
// was still resolved. WindowMappedCapture below carries the same type and
// the same contract; the two structs really do mean the same thing by
// "resolved".
struct PaintingLayerRootResult {
	QPointer<QWidget> widget;
	QString refusal;

	[[nodiscard]] bool resolved() const {
		return widget != nullptr;
	}
};

[[nodiscard]] PaintingLayerRootResult PaintingLayerRoot(QWidget *box);

// Saves the box content's own rect, grabbed out of the Ui::BoxLayerWidget
// that paints it. That crop excludes the shell chrome a "whole box" frame
// exists to show: Ui::BoxLayerWidget::setTitle creates _title parented to
// the shell (ui/layers/box_layer_widget.cpp), and addButton re-parents each
// footer button onto the shell (raw->setParent(this); raw->show();). An
// unresolved root is a logged FAIL carrying the refusal above, never a null
// the caller has to re-check before CaptureMappedRect, which this composes
// and which takes not_null<QWidget*>. A box that maps outside its layer is
// still that helper's named misframing FAIL.
bool CaptureInLayerRoot(not_null<QWidget*> box, const QString &name);

// Saves the resolved Ui::BoxLayerWidget itself, via CaptureWidget, so the
// frame includes _title and the addButton footer row that
// CaptureInLayerRoot's crop drops. An unresolved root is a logged FAIL
// carrying PaintingLayerRoot's refusal; visibility, blank, and blank-root
// refusals are CaptureWidget's. For a box that is still hidden while the
// layer show animation runs, this fails visibility the same way a grab of
// that shell would — wait until the layer is shown (BoxButtonReady for a
// footer, or box->isVisible() for the content) rather than relaxing it.
bool CaptureBoxLayer(not_null<QWidget*> box, const QString &name);

// The one capture for a widget that paints no opaque background of its own.
//
// Ui::Toast::internal::Widget's constructor sets only
// Qt::WA_TransparentForMouseEvents, never Qt::WA_OpaquePaintEvent nor
// Qt::WA_NoSystemBackground (ui/toast/toast_widget.cpp), and while its
// fade-in opacity is below 1 its paintEvent draws the whole frame into a
// transparent proxy at that opacity and returns. A grab of such a widget
// holds the harness base and nothing else, at perfectly sane geometry - what
// run 4 of 2026/08/30/replace-wallet-with-new-or-imported paid for. The
// repair is to grab the widget's own window and crop it to the widget's rect
// mapped into that window, because the opaque window behind the fade-in is
// what holds the real pixels.
//
// PreparedWidgetCapture cannot answer this. Its blank-frame refusal fires on
// every frame such a wrapper can offer, so a poll around it can only end in
// a stage timeout - the shape test_layer_root.h's comment on
// AppendPaintingLayerRootSelfTest describes for a no-content-margin box.
//
// A blank frame here is a Note and never a FAIL, by contract: the decisive
// oracle for a fade-in wrapper is textual - the joined accessibilityName()
// of its Ui::FlatLabels (ui/widgets/labels.h) - and the capture only
// corroborates it. A structural refusal is still a loud FAIL, because no
// amount of waiting repairs it: no widget, not visible, empty geometry, the
// target is its own window (which keeps this off a Ui::PopupMenu just as the
// walk above refuses one), or its rect does not map inside that window.
//
// ReadViaWindow takes no grab and |window| is non-null exactly when
// |refusal| is empty at the moment the reading is taken; GrabViaWindow and
// ViaWindowReady take exactly one grab, so a poll costs one grab per tick.
//
// |window| is a QPointer<QWidget>, which is what a retained reading rests
// on: a caller may keep this value for as long as it likes, and once the
// window it named is destroyed resolved() answers false on the very same
// object rather than handing back a pointer into freed memory. |mapped|,
// |identity| and |refusal| are values and stay valid for the whole run -
// they are what a reading whose window is gone has left to print, and the
// caller must print those recorded fields instead of re-formatting a
// pointer it no longer has, because WidgetDescription takes
// not_null<QWidget*> and its Expects is a crash and not a refusal.
// Test::PaintingLayerRootResult above carries the same type and contract.
struct WindowMappedCapture {
	QPointer<QWidget> window;
	QRect mapped;
	QString refusal;
	QString identity;

	[[nodiscard]] bool resolved() const {
		return window != nullptr;
	}
};

[[nodiscard]] WindowMappedCapture ReadViaWindow(QWidget *widget);
[[nodiscard]] QImage GrabViaWindow(QWidget *widget);
[[nodiscard]] bool ViaWindowReady(QWidget *widget);
[[nodiscard]] QString ViaWindowDetails(QWidget *widget);
bool CaptureViaWindow(not_null<QWidget*> widget, const QString &name);

// Strict same-state comparison of two captured images.
//
// Both arms render the same owner with the same owner-local |rendered| rect,
// which fixes the render origin and extent; the strict path is
// owner->rect(), a full-owner render. RenderStrict then applies one
// predeclared owner-local |crop|, converted by DevicePixelRect with the
// grab's ratio applied once, and records the rects beside the cropped
// image. It refuses, with a null image and the requested rects named, a
// missing owner, an empty |rendered| or one not fully inside owner->rect()
// (MisframedDetails), and an empty |crop| or one not fully inside
// |rendered|; a crop is never shortened to fit. |owner| is non-null exactly
// when |refusal| is empty at the moment the render is taken; valid()
// answers false once the owner is destroyed, while the recorded rects,
// |identity| and |refusal| stay printable.
//
// CompareStrict refuses, first applicable, an invalid record ("invalid-a",
// "invalid-b"), "render-owner-differs", "render-path-differs" (another
// rendered rect: another origin or extent), "crop-differs",
// "decoded-size-differs", "dpr-differs", "format-differs" and
// "unsupported-depth" (a non-32-bit format), printing both records. Size,
// devicePixelRatio() and format() are read from the images themselves, so
// a modified copy carries no stale metadata. There is no tolerance, mask or
// region: equal() means no refusal and zero differing 32-bit pixels.
// Whenever the decoded images are comparable (same size, ratio and 32-bit
// format) every pixel is counted, so a refused comparison's count is
// diagnostic only and never supports equality.
//
// The signature this guards: a static QLinearGradient rendered full-owner
// and then cropped differs from the same region rendered directly (a
// shifted render origin) in about a thousand pixels, mostly one channel by
// one level, while geometry agrees - the raster gradient fetch steps a
// truncated fixed-point increment from each span start. The repair is the
// identical render path, never a tolerance or a wider mask:
// docs/helpers/test_capture.md, "Strict same-state comparisons".
struct StrictRender {
	QPointer<QWidget> owner;
	QString identity;
	QRect rendered;
	QRect crop;
	QRect pixels;
	QImage image;
	QString refusal;

	[[nodiscard]] bool valid() const;
};

[[nodiscard]] StrictRender RenderStrict(
	QWidget *owner,
	const QRect &rendered,
	const QRect &crop);

// "owner=<identity> rendered=<rect> crop=<rect> pixels=<rect> image=<WxH>
// dpr=<ratio> format=<QImage::Format>", then " refusal=<refusal>" when
// refused.
[[nodiscard]] QString StrictRenderText(const StrictRender &render);

// |compared| is the pixel count of comparable images (0 otherwise);
// |differing|, |maxChannelDelta| (the largest byte difference of any
// channel) and |first| (the first differing pixel in scan order) stay -1
// until comparable images are counted.
struct StrictComparison {
	int compared = 0;
	int differing = -1;
	int maxChannelDelta = -1;
	QPoint first = QPoint(-1, -1);
	QString refusal;

	[[nodiscard]] bool equal() const;
};

[[nodiscard]] StrictComparison CompareStrict(
	const StrictRender &a,
	const StrictRender &b);

// "compared=<n> differing=<n> maxChannelDelta=<n> first=<x>,<y>
// equal=<0|1>", then " refusal=<refusal>" when refused.
[[nodiscard]] QString StrictComparisonText(
	const StrictComparison &comparison);

[[nodiscard]] QImage Crop(const QImage &image, const QRect &pixelRect);

// An owner-local logical rect in device pixels of a grab rendered at
// |ratio|: left and top are floored, right and bottom ceiled, and the ratio
// is applied exactly once. It is the device crop that both the strict and
// the checked full-owner compositions hand to Crop. Harness grabs carry an
// integer ratio (style::DevicePixelRatio()), so the rounding only states the
// contract.
[[nodiscard]] QRect DevicePixelRect(const QRect &logical, qreal ratio);

// Nearest-neighbor upscale for readable small-target evidence.
[[nodiscard]] QImage Zoom(const QImage &image, int factor);

// Lays the images out side by side at their native pixel sizes (no
// rescaling), for same-scale comparisons.
[[nodiscard]] QImage ContactSheet(const std::vector<QImage> &images);

} // namespace Test
