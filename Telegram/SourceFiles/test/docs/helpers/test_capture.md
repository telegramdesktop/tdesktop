# test_capture

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_capture.h](../../test_capture.h).

## Use when

Prepared grabs, paint owners, mapped targets with explicit coordinate
owners, strict same-state comparisons, checked full-owner crops and saved
images with their `SCREENSHOT` marker. Read the contract below and the
source header before use.

## Contract and usage

In-process grabs, paint-root validation, mapped rects, complete-target mapped
readiness (`ReadMappedTarget` / `MappedTargetReady` / `CaptureMappedTarget` /
`PreparedWidgetCapture::prepare(owner, origin, rect)`) and its typed
`MappedRect` overloads, the strict render path (`RenderStrict` /
`CompareStrict`), the checked full-owner crop
(`PreparedWidgetCapture::prepareCrop`), blank detection,
painting-layer-root resolution for boxes inside a layer, `CaptureBoxLayer` for
the whole `Ui::BoxLayerWidget` (title + `addButton` footer row, which
`setTitle` / `addButton` parent onto the shell) versus `CaptureInLayerRoot`
for a frame cropped to the box content, crops, zoom, contact sheets,
window-mapped capture for widgets that paint no opaque background of their
own; a **toast** goes to `test_toast_capture.h` instead, whose frame cannot
compose the surface beneath it.

## Saved images and the SCREENSHOT marker

`SaveImage(image, name)` writes `image` as a PNG to `ScreenshotsDir()` plus
`name`, appending `.png` unless `name` already ends with it in any case, and
returns that absolute path. The capture helpers on this page save through
it. A null image or a failed write, such as a missing parent directory or a
name the file system refuses, returns an empty string and writes no marker.
Callers use the return value; none of them reads the marker.

A successful save appends exactly one row to `test_log.txt`:

```text
SCREENSHOT: "screenshots/<file>"
```

- **Value.** The returned path without its `EvidenceDir()` prefix: the
  `screenshots/` folder and the saved file name, with Qt's `/` separators.
  The row never names the host evidence root, so a host directory that
  happens to equal a fixture identity no longer reaches a publication audit
  through screenshot metadata.
- **Escaping.** `SaveImage` writes each `\` as `\\`, then each `"` as `\"`.
  `LogRaw` then writes each of its ten line separators (U+000A, U+000B,
  U+000C, U+000D, U+001C, U+001D, U+001E, U+0085, U+2028, U+2029) as an
  uppercase `\uXXXX` token ([test_log](test_log.md)). Every other character,
  leading and trailing spaces and non-ASCII text included, stays literal
  UTF-8. The backslash is doubled first, so a name holding the literal text
  `\u000A` is written `\\u000A` and never reads back as a line feed.
- **Resolution base.** The evidence directory is `EvidenceDir()`, the
  directory holding `test_log.txt`: `TDESKTOP_TEST_EVIDENCE_DIR` when set,
  otherwise `test_evidence` in the working directory. `workspace.py test-run`
  sets that variable to its resolved `--run-dir`. `EvidenceDir()` followed
  by the decoded value is exactly the returned path.
- **Reading.** Decode only a payload that is one whole quoted string in this
  grammar. Read it once, left to right: `\\` is `\`, `\"` is `"` and each
  of the ten uppercase tokens is its separator. Join the result to the
  evidence directory. `parse_test_log` in
  `.agents/skills/process-inbox/scripts/workspace.py` does exactly this, so
  the `test-run` report's `markers.screenshots` lists openable absolute
  paths, in log order.
- **Historical rows.** Logs written before this grammar carry the unquoted
  absolute path (`SCREENSHOT: /...`, `C:/...`, `//host/...`). The reader
  returns those verbatim and untrimmed, as it does any payload outside the
  grammar: an unterminated quote, another escape, a lowercase or
  non-separator `\u` token, or text after the closing quote. No old report
  is rewritten.

The relative value still carries the caller's own image name. A complete
identity placed in that name is a real presentation, and
[`CheckPublicationIdentity`](test_publication_identity_scan.md) counts it;
only the incidental host path is gone.

## Capture and visual assertions

Choose the narrowest helper that captures the real paint owner:

| Need | Helper |
| --- | --- |
| Static visible widget or local rect | `CaptureWidget` / `CaptureRect` |
| Rect expressed in a child or offscreen content widget's coordinates | `CaptureMappedRect` |
| Complete mapped target visible in painted owner and scroll viewport before that capture | `ReadMappedTarget` / `MappedTargetReady` / `PreparedWidgetCapture::prepare(owner, origin, rect)` / `CaptureMappedTarget` |
| The same, for a child's own rect or a frame it reports relative to its parent (`geometry()`, a `...Geometry()` accessor), with the coordinate space named so a doubled position is refused | `MappedRect::Local` / `MappedRect::InParent` with the typed `ReadMappedTarget` / `prepare` / `CaptureMappedTarget` ([coordinate owners](#coordinate-owners-of-mapped-rectangles)) |
| Region cropped out of a full-owner render with its whole requested extent, its originating descendant certified whole inside its actual scroll viewport | `PreparedWidgetCapture::prepareCrop(owner, descendant, crop)` ([checked crops](#checked-full-owner-crops)) |
| Two frames of one state compared at zero tolerance (repeated render, twins, before and after a no-op) | `RenderStrict(owner, owner->rect(), crop)` for both arms, then `CompareStrict` ([strict comparisons](#strict-same-state-comparisons)) |
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
| Small target comparison | `Crop`, `Zoom`, `ContactSheet`; `Crop` intersects with the image, so its result never certifies a requested extent |
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
A rect a child reports relative to its parent goes through the typed path
([coordinate owners](#coordinate-owners-of-mapped-rectangles)). These
compositions render the mapped rect directly, a shifted render origin: a
crop out of a full-owner render is `prepareCrop`
([checked crops](#checked-full-owner-crops)).
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
For an owner-local logical crop that boundary is
`DevicePixelRect(rect, image.devicePixelRatio())`: left and top floored,
right and bottom ceiled, the ratio applied once. The strict and the checked
full-owner compositions both crop through it; harness grabs carry an
integer ratio (`style::DevicePixelRatio()`), so the rounding only states the
contract.
`TDESKTOP_TEST_SCALE` is a third, independent multiplier: it scales the
logical style metrics themselves, so derive expected geometry from the live
scaled tokens, never from literals recorded at 100%. Rects from different
widgets never share an origin by accident — map both through one declared
frame (`Ui::MapFrom`, `mapToGlobal`) before comparing, cropping, or
clicking.

## Coordinate owners of mapped rectangles

The untyped readings take an origin widget and a rect in that origin's
coordinates and check that the mapped rect is whole inside the owner and
its viewport. Nothing ties the rect to the origin, so a child's
parent-relative frame handed with the child as origin maps with the
child's position applied twice, and wherever that doubled rect still fits
it is accepted. In Run 4 of
`2026/10/17/open-ones-own-qr-box-without-a-username-with-temporary-only-codes`
`Ui::PeerQrPill::pillGeometry()`, which translates the pill's frame by
`pos()`, was handed with the pill as origin: the mapped y read 736, the
correct 397 plus the pill's own y 339, and both captures failed outside the
owner.

`MappedRect` binds a rect to the widget that paints it. Its two factories
are its only construction, so a call site names the space:

| Factory | Rect | `origin()` |
| --- | --- | --- |
| `MappedRect::Local(w, rect)` | In `w`'s own coordinates: `w->rect()` or a sub-rect of what `w` paints | `w` |
| `MappedRect::InParent(w, rect)` | A frame `w` reports relative to its parent: `w->geometry()` or a `...Geometry()` accessor that translates by `pos()` | `w`'s parent; null when `w` has none or is a window |

A rect that also covers the parent's own painting is `Local(parent, rect)`.
`widget()` is the painted widget, held as a `QPointer`, so a retained value
never dangles; `origin()` is null once it is destroyed.

The typed `ReadMappedTarget(owner, target)` refuses, in order:

- a null owner or a destroyed painted widget, with the untyped null
  refusals;
- `detached: ...`, an `InParent` rect whose widget has no parent
  coordinate space;
- `wrong owner: painted <widget> is not <owner> or inside it`, by Qt's
  ancestry, which stops at the window;
- `painted widget is not visible: ...`, a hidden `InParent` widget, whose
  rect would otherwise map through its visible parent alone;
- `coordinate mismatch: requested=<rect> space=local|parent origin=<widget>
  painted=<widget> bounds=<rect> owner=<widget> would map to <rect> - ...`,
  a non-empty rect not fully inside the painted widget's own bounds in its
  space (`rect()` for `Local`, `geometry()` for `InParent`): the
  doubled-position signature, or its reverse.

Otherwise it is exactly the untyped `ReadMappedTarget(owner,
target.origin(), target.rect())`, so every complete-target refusal (owner,
viewport, hidden, empty) still applies and nothing is intersected, shrunk
or reframed. Typed readiness is `ReadMappedTarget(owner, target).resolved()`;
there is no typed `MappedTargetReady`. Every typed reading carries
`binding`, which `MappedTargetDetails` appends as ` binding=local to
<widget> rect=<rect>` or ` binding=in parent of <widget> rect=<rect>`;
untyped readings leave it empty and print exactly what they printed
before.

The typed `PreparedWidgetCapture::prepare(owner, target)` stores a refused
reading through `invalidate()` and logs nothing; the typed
`CaptureMappedTarget(owner, target, name)` logs it as `capture <name>`
FAIL. Accepted, each is exactly its untyped overload. The untyped overloads
keep their behaviour for compatible callers, which pass an origin-local
rect with its real origin; child geometry goes through
`MappedRect::InParent`.

```cpp
runner->add({
	.name = u"the pill's complete frame"_q,
	.until = [=] {
		const auto pill = state->pill.data();
		return pill && Test::ReadMappedTarget(
			state->content.data(),
			Test::MappedRect::InParent(pill, pill->pillGeometry())).resolved();
	},
	.then = [=] {
		const auto pill = state->pill.data();
		Test::CaptureMappedTarget(
			state->content.data(),
			Test::MappedRect::InParent(pill, pill->pillGeometry()),
			u"pill_frame"_q);
	},
});
```

Confirm with `AppendMappedTargetSelfTest`'s typed stages
([self-test](test_mapped_target.md)).

## Checked full-owner crops

Some evidence must be a region of a full-owner render: a band that spans
several children, or the crop a [strict comparison](#strict-same-state-comparisons)
takes on the full-owner path. Two weaker compositions accept it wrongly:

- `Crop(image, pixelRect)` intersects `pixelRect` with the image on
  purpose, so a crop reaching past the owner silently comes back shorter
  and non-null: a non-empty result certifies no requested extent.
- Owner-local containment of the crop, or a mapped reading with the owner
  itself as origin, never walks a descendant's scroll ancestry, so it
  accepts a row that fits its owner while its scroll viewport, shortened
  above a bottom overlay or hint, cuts it.

In Run 3 of `2026/10/18/tighten-money-tabs-without-moving-the-transactions-title`
at interface scale 200 the Content owner was 784x828 while its first row
ended at 862: the requested band/strip/full-row crop extended 34 px past
Content, the Transactions bottom hint also clipped the scroll viewport, and
a full-owner grab followed by `Crop` returned a shorter image that cut the
row behind the hint.

`PreparedWidgetCapture::prepareCrop(owner, descendant, crop)` renders the
whole `owner` through `prepare(owner)` and crops the owner-local logical
`crop` out of that render. It refuses, in this order:

| Refusal | Condition |
| --- | --- |
| `no painted owner or descendant was handed ...` | Either is null. |
| `owner-as-origin substitution: <owner> cannot certify a descendant's scroll viewport` | `descendant` is `owner`. |
| `descendant is not inside the painted owner: ...` | `descendant` is not a descendant of `owner`. |
| `descendant: <reading's refusal>` | `ReadMappedTarget(owner, descendant, descendant->rect())` does not resolve: the whole descendant must lie inside the owner and its actual viewport. |
| `crop: <MisframedDetails> owner=<widget>` | `crop` is empty or not fully inside `owner->rect()`; the details quote `requested=`, the owner extent (`widget=`) and the inside rows and columns. |
| `prepare(owner)`'s own refusals | Hidden, empty, blank or a nonpainting render root. |
| `device crop is not fully inside the full-owner render: ...` | `DevicePixelRect(crop, ratio)` leaves the render. |
| `cropped extent requested=<WxH> actual=<WxH>: ...` | The cropped image lacks part of that device extent. |

Every refusal goes through `invalidate()`, so `image()` is null,
`pendingReason()` names it and nothing is logged. On acceptance `widget()`
is `owner`, `image()` is the crop with its entire device extent and
`save()` persists that same frame. The descendant is always certified
whole with its own rect, so the crop needs no coordinate pairing with it:
pass the descendant whose visibility the evidence claims, such as the row,
never the owner.

When a crop or its descendant is refused because the fixture is too short,
make the owner taller - a `Ui::SeparatePanel` fixture through
[panel sizing](test_panel.md#sizing-an-oversized-panel-fixture) - and keep
the request. Never shrink, intersect or reframe the crop, change the
interface scale, style or rows, or mask the clipped edge to make it pass.

```cpp
auto capture = Test::PreparedWidgetCapture();
const auto saved = capture.prepareCrop(content, row, band)
	&& capture.save(u"band"_q);
Test::Check(
	saved,
	u"the whole band is saved with its row wholly visible"_q,
	saved ? Test::RectText(band) : capture.pendingReason());
```

Confirm with `AppendFullOwnerCaptureSelfTest` group C
([self-test](test_full_owner_capture.md)).

## Strict same-state comparisons

A strict comparison claims two frames of one state are identical: a
repeated render, two twins, before and after a no-op. Equal geometry does
not make the inputs identical when the arms render differently. In Run 1 of
`2026/10/18/tighten-money-tabs-without-moving-the-transactions-title` two
reverse samples differed from their baseline in 1046 pixels (`outside1046`),
all inside a static `QLinearGradient` avatar and mostly one channel by one
level, while geometry and both settled endpoint comparisons agreed: the
endpoints rendered the full Content owner and then cropped, the samples
rendered the shifted Content region directly.

The mechanism is Qt's raster linear-gradient fetch
(`qt_fetch_linear_gradient_template` in `qdrawhelper.cpp`). It computes the
gradient position in floating point at each span start and then steps an
increment truncated to 8 fraction bits per pixel, so its error accumulates
from the span start. A full-owner render starts the gradient child's spans
at the child's left edge; a direct-region render whose rect starts inside
the child - `GrabRect(owner, rect)`, or `prepare(owner, origin, rect)`,
which renders the mapped rect directly - starts them at the rect's left
edge, so the same physical pixel reads a stop-table entry one step away:
one channel level. A vertical gradient, a rect starting at the child's
edge, or integer translation alone gives identical bytes.

The repair is the identical render path: the same owner and the same
owner-local rendered rect in both arms - the strict path is
`owner->rect()`, a full-owner render - then one predeclared owner-local
crop through `DevicePixelRect`, the ratio applied once, compared at zero
tolerance. A path mismatch is never repaired by accepting one-level
differences, excluding or masking the gradient, translating, rescaling or
altering pixels.

`RenderStrict(owner, rendered, crop)` returns a `StrictRender` that records
`owner`, `identity`, `rendered`, `crop`, `pixels` (the device crop) and the
cropped `image`. It refuses, with a null image and the requested rects
named: no owner; an empty `rendered`, or one not fully inside
`owner->rect()` (`MisframedDetails`); a `crop` that is empty or not fully
inside `rendered` (`crop is not fully inside the rendered rect:
requested=<crop> rendered=<rendered> ...`); and a device crop outside the
render. A crop is never shortened to fit. `valid()` turns false once the
owner is destroyed, while the record stays printable: `StrictRenderText`
prints `owner= rendered= crop= pixels= image=<WxH> dpr= format=` and any
refusal.

`CompareStrict(a, b)` refuses, first applicable, `invalid-a`, `invalid-b`,
`render-owner-differs`, `render-path-differs: rendered=<a> vs <b>` (another
origin or extent), `crop-differs`, `decoded-size-differs`, `dpr-differs`,
`format-differs` and `unsupported-depth` (not a 32-bit format), followed
by ` - a={...} b={...}` with both records. Size, `devicePixelRatio()` and
`format()` are read from the images, so a modified copy carries no stale
metadata. Whenever the images are comparable (same size, ratio and 32-bit
format) every 32-bit pixel is compared line by line into `compared`,
`differing`, `maxChannelDelta` and `first`, so a refused comparison's count
is diagnostic and never supports equality. `equal()` means no refusal,
`compared > 0` and `differing == 0`; there is no tolerance, mask or region
parameter. `StrictComparisonText` prints every field.

```cpp
const auto crop = state->avatarCrop; // owner-local, frozen once
const auto before = Test::RenderStrict(content, content->rect(), crop);
// ... the no-op under test ...
const auto after = Test::RenderStrict(content, content->rect(), crop);
const auto comparison = Test::CompareStrict(before, after);
Test::Check(
	comparison.equal(),
	u"the avatar is unchanged after the no-op"_q,
	Test::StrictComparisonText(comparison));
```

Confirm with `AppendFullOwnerCaptureSelfTest` group B
([self-test](test_full_owner_capture.md)).

## Verification

The marker grammar has a producer check and a reader check.

- `AppendPublicationIdentityScanSelfTest` ends with the
  `publication_self_screenshot_marker` stage
  ([self-test](test_publication_identity_scan_self_test.md)). It saves five
  probes through the real `SaveImage`: a plain name; Unicode text padded by
  two spaces on each side; a quote; the literal text `\u000A`; and a
  backslash followed by all ten separators and a quote. It compares each
  row's appended bytes with an independently written row, and each return
  value and reloaded image with the expected path and probe pixels. The
  stage applies only when `EvidenceDir()` has exactly one directory
  component named `synthetic_marker_collision_handle`; otherwise it is
  `TEST_RESULT: N/A` and proves nothing.
- From `.agents/skills/process-inbox/scripts`,
  `python3 -m unittest workspace_test -k parse_test_log -k test_run_reports_complete_markers`
  decodes the same five rows with LF and CRLF terminators, keeps historical
  POSIX, drive and UNC payloads and malformed quoted payloads verbatim, and
  resolves a fake `test-run` row against that report's `run_dir`.

The capture compositions have their own self-tests:

- `AppendFullOwnerCaptureSelfTest` ([self-test](test_full_owner_capture.md)):
  group B runs the strict path against a shifted direct-region render of a
  static gradient, a repeated same path, one changed pixel and the metadata
  refusals; group C runs `prepareCrop` against a crop past its owner and a
  viewport-clipped row, then accepts both after a typed panel growth.
- `AppendMappedTargetSelfTest` ([self-test](test_mapped_target.md)): the
  complete-target readings and, in its typed stages, `MappedRect` from both
  coordinate spaces, the doubled-position refusal and the valid typed crop.

Record executed results in the change's evidence.

## Related guidance

[Post-paint sampling](test_post_paint.md); [clocked frames](test_animation_clock.md); [corner comparisons](test_corner_patch.md); [panel sizing](test_panel.md#sizing-an-oversized-panel-fixture); [full-owner capture self-test](test_full_owner_capture.md); [failure diagnosis](../diagnostics.md).
