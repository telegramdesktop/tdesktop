# test_capture

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_capture.h](../../test_capture.h).

## Use when

Prepared grabs, paint owners and mapped targets. Read the contract below and the source header before use.

## Contract and usage

In-process grabs, paint-root validation, mapped rects, complete-target mapped
readiness (`ReadMappedTarget` / `MappedTargetReady` / `CaptureMappedTarget` /
`PreparedWidgetCapture::prepare(owner, origin, rect)`), blank detection,
painting-layer-root resolution for boxes inside a layer, `CaptureBoxLayer` for
the whole `Ui::BoxLayerWidget` (title + `addButton` footer row, which
`setTitle` / `addButton` parent onto the shell) versus `CaptureInLayerRoot`
for a frame cropped to the box content, crops, zoom, contact sheets,
window-mapped capture for widgets that paint no opaque background of their
own; a **toast** goes to `test_toast_capture.h` instead, whose frame cannot
compose the surface beneath it.

## Capture and visual assertions

Choose the narrowest helper that captures the real paint owner:

| Need | Helper |
| --- | --- |
| Static visible widget or local rect | `CaptureWidget` / `CaptureRect` |
| Rect expressed in a child or offscreen content widget's coordinates | `CaptureMappedRect` |
| Complete mapped target visible in painted owner and scroll viewport before that capture | `ReadMappedTarget` / `MappedTargetReady` / `PreparedWidgetCapture::prepare(owner, origin, rect)` / `CaptureMappedTarget` |
| Full box, layer owner, animation, or asynchronously populated surface | `Runner::captureWidget` |
| Animating owner whose product paints can run back to back (several animations at once, especially in Debug), sampled right after each of them instead of on a timer | `PostPaintSampler` (`test_post_paint.h`) |
| Short `Ui::Animations`-driven product animation (a slide transition, a reveal) captured at chosen elapsed times after one synchronous action or click handler, whether or not the target receives compositor paints (a minimized window, a never-shown widget) | `RunClockedFrames` (`test_animation_clock.h`) |
| Whole box inside a layer (title + footer shell) | `PaintingLayerRoot` + `Runner::captureWidget`, or `CaptureBoxLayer` |
| Box content cropped inside a layer | `CaptureInLayerRoot` |
| Open `Ui::PopupMenu` | `CapturePopupMenu`, a supplementary frame taken only while the menu is still open; its action texts and lifecycle come from `OpenContextMenu` (`test_menu.h`), which reads the menu in the turn that built it and grabs no pixels |
| Toast whose frame must not compose the product surface painted beneath it | `CaptureToastSubtree` |
| Other widget that paints no opaque background of its own: a fade-in wrapper that is a child widget, such as `Ui::ImportantTooltip` | `CaptureViaWindow` |
| A `Ui::Tooltip`, which is `RpWidget(nullptr)` and so its own top-level window that `CaptureViaWindow` refuses | Find it in `QApplication::topLevelWidgets()` (`dynamic_cast<Ui::Tooltip*>`, visible only), read its text from `accessibilityName()`, capture it with `CaptureWidget` |
| Exact accepted frame plus numeric/raster assertions | `Runner::captureAndInspect` |
| Small target comparison | `Crop`, `Zoom`, `ContactSheet` |
| Foreground colour, contrast, or painted-band measurement | `MeasurePaintedInk` and the helpers in `test_ink.h` |
| Rounded corner of a media or bubble against a twin or reference: the background beyond the corner, arc coverage across twins whose media colours differ, containment, chrome inside the patch | `ReadCornerBackground` / `CompareCornerCoverage` / `ReadCornerContainment` / `GateCornerChrome` (`test_corner_patch.h`) |
| Day or night palette variant of the same surface, whatever the current night mode and whether the theme follows the system dark mode | `AppendPaletteSwitch` (`test_palette.h`) in a stage before the captures, judged by its settled live palette sample, and `AppendPaletteRestore` in a stage after them, which puts back the night mode and the system-follow setting the switch found; a bare `Window::Theme::ToggleNightMode()` is silently refused while the theme follows the system (Failure diagnosis). Confirm with `AppendPaletteSwitchSelfTest`. |

`CaptureInLayerRoot` crops to the box content's own rect. That is not the
whole box: `Ui::BoxLayerWidget::setTitle` creates `_title` parented to the
shell, and `addButton` re-parents each footer button onto the shell, so
both live outside `box->rect()`. `CaptureBoxLayer` grabs that shell.

`captureWidget` and `captureAndInspect` reject missing, hidden, empty,
near-uniform, and invalid transparent-root frames. Their readiness predicate
should identify current content: model id, generation, title, child count,
paint generation, or settled animation state. It must not assert that the
result has the expected height or colour.

`captureAndInspect` saves the accepted image before it runs assertions, so a
failure still has decisive evidence:

```cpp
runner->captureAndInspect(
	u"gift_composer_150"_q,
	[] { return Test::ReadLiveWidget(u"gift.composer"_q).widget; },
	[](QWidget *widget) {
		return widget->property("contentGeneration").toInt() > 0;
	},
	[](QWidget *widget, const QImage &image) {
		Test::Note(u"composer image=%1x%2"_q
			.arg(image.width())
			.arg(image.height()));
		const auto content = widget->childrenRect();
		Test::Check(
			widget->rect().contains(content),
			u"composer contains its child geometry"_q,
			u"owner=%1,%2 %3x%4 children=%5,%6 %7x%8"_q
				.arg(widget->rect().x())
				.arg(widget->rect().y())
				.arg(widget->rect().width())
				.arg(widget->rect().height())
				.arg(content.x())
				.arg(content.y())
				.arg(content.width())
				.arg(content.height()));
	});
```

If a child grab exposes the harness background, capture the ancestor that
paints the background. If a row is clipped by a scroll viewport, grab the
content widget and map the row rect with `CaptureMappedRect`; do not treat a
partial screenshot as product evidence.

For a capture whose subject is the currently visible mapping — a card, row,
or child rectangle that must appear whole in the painted owner — the caller
supplies that origin widget, the painted owner, and the exact origin-local
rectangle to `ReadMappedTarget` / `MappedTargetReady`. Readiness is
whole-rectangle containment in the owner **and** the relevant viewport
(`Ui::ElasticScroll` itself, because its `viewport()` is Dummy and returns
the inner content; `QAbstractScrollArea::viewport()` otherwise). It never
intersects, clips, or reframes the requested rect. Origin-local containment
alone is not visibility: a HistoryInner-local card rect can sit fully
inside the inner widget while a negative scroll offset leaves part of it
outside HistoryWidget. `CaptureMappedTarget` refuses that unready reading
and then composes `CaptureMappedRect`, which still independently refuses a
rect that is not fully inside the grabbed widget. A nonpainting owner whose
geometry fits is still not capture-ready (`PreparedWidgetCapture::prepare`).
`PaintingLayerRoot` resolves boxes inside layers specifically; it is not a
history/list paint-owner resolver. Callers still own semantic navigation
and exact item identity.

## Coordinate systems

Measurements cross three coordinate systems, and each has a fixed contract.
`CaptureWidget` / `CaptureRect` / `CaptureMappedRect` take widget LOGICAL
rects; the image they produce — like every `Ui::GrabWidgetToImage` grab —
holds logical-size × `style::DevicePixelRatio()` DEVICE pixels, so `Crop`
and any direct `QImage` sampling index device space. Multiply logical values
by the image's `devicePixelRatio()` exactly once, at the sampling boundary.
`TDESKTOP_TEST_SCALE` is a third, independent multiplier: it scales the
logical style metrics themselves, so derive expected geometry from the live
scaled tokens, never from literals recorded at 100%. Rects from different
widgets never share an origin by accident — map both through one declared
frame (`Ui::MapFrom`, `mapToGlobal`) before comparing, cropping, or
clicking.

## Related guidance

[Post-paint sampling](test_post_paint.md); [clocked frames](test_animation_clock.md); [corner comparisons](test_corner_patch.md); [failure diagnosis](../diagnostics.md).
