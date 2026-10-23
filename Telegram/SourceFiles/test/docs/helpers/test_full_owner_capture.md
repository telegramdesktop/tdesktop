# test_full_owner_capture

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Source contract: [test_full_owner_capture.h](../../test_full_owner_capture.h).

## Use when

`Test::AppendFullOwnerCaptureSelfTest(runner)` qualifies the full-owner
capture helpers on one synthetic composite owner. It protects four
contracts, each with its own oracle and negative control:

- Part A, [coordinated samplers and receipts](test_post_paint.md#coordinated-samplers-and-receipts):
  samplers sharing a `PostPaintCaptureScope` never count a member's
  composite capture as a product paint, still sample every genuine paint,
  and reconcile to the fixture's own paint records by receipt index.
- Part B, [strict same-state comparisons](test_capture.md#strict-same-state-comparisons):
  `RenderStrict` / `CompareStrict` keep one render owner, origin and extent,
  refuse a mismatched path or decode before equality and count at zero
  tolerance.
- Part C, [checked full-owner crops](test_capture.md#checked-full-owner-crops)
  and [panel sizing](test_panel.md#sizing-an-oversized-panel-fixture):
  `PreparedWidgetCapture::prepareCrop` refuses a crop past its owner and a
  descendant cut by its scroll viewport, and accepts the same request whole
  after a typed `Ui::SeparatePanel` growth that is restored exactly.
- One joint stage of Parts C and D: the typed
  [coordinate owners](test_capture.md#coordinate-owners-of-mapped-rectangles)
  do not weaken Part C's descendant rule. Part D's own stages are in
  [test_mapped_target](test_mapped_target.md).

It is a self-test, not a routine for product overlays. Pack it when a
change touches `test_post_paint`, `test_capture`'s strict, checked-crop or
typed mapped paths, or `test_panel`'s sizing helpers.

## Preconditions and limits

- Append it from the disposable `test_scenario.cpp` overlay after
  `runner->waitEvent(u"launch_finished"_q)`. It needs no session, account,
  network, chats list or primary window, so an account-free
  `workspace.py test-run --portable-root <dir>` sandbox is enough. On macOS
  wrap the launch in `caffeinate -dimsu` so the display link keeps pacing
  `update()` paints.
- It builds one top-level `Ui::SeparatePanel`, so it must not be packed
  beside `AppendSeparatePanelWalkSelfTest`, which needs to be the only
  owner of a live panel ([test_panel](test_panel.md)). While it runs, its
  panel carries the `KeepWindowExposed` stay-on-top hint and floats above
  other applications' windows; it is never activated.
- A locked console never gates: a locked macOS console still paints. The
  paint group decides by receipts and prints the `CONSOLE_LOCK` and
  exposure readings in its details on both verdicts.
- Arm A4's premise needs `repaint()` to be a synchronous backing-store
  paint, which holds in a top level without texture children such as this
  panel; on macOS an `update()` is display-link paced either way.
- Sizes are multiples of one row, `st::defaultActiveButton.height`, so the
  fixture follows `TDESKTOP_TEST_SCALE`; harness grabs carry an integer
  device pixel ratio.

## Fixture

The fixture stage builds the panel with the `MakePanel()` recipe of
`test_window_exposure.cpp`: `Qt::WindowStaysOnTopHint` cleared before any
native window, `Qt::WA_ShowWithoutActivating`, `Qt::WA_QuitOnClose` false,
an ASCII title and the object name `harness-full-owner-panel`, and
`setInnerSize(QSize(6 rows, st::separatePanelTitleHeight + 6 rows))`, the
inner size including the title. It shows it with plain `show()`, calls
`KeepWindowExposed` and waits up to 3000 ms for an exposed panel whose
composite owner is six rows square.

The composite owner P is an opaque `Ui::RpWidget` child of the panel itself
(not of its private body, which `showInner()` would activate), laid out on
`innerGeometry()` after the first show and after a settled sizing, never
from the panel's `sizeValue()`. Its children, all opaque:

| Child | Owner-local geometry | Role |
| --- | --- | --- |
| First and second child | `(0, 0, 3row, row)` and `(3row, 0, 3row, row)` | Exact paint owners of group A. Each paint stamps a record: child, generation, the observing sampler's `receiptMark()` (its receipt index for this paint), start, end and whether a fixture capture was running. |
| Gradient | `(row, row, w, row)` | Static horizontal `QLinearGradient` from `st::windowBgActive` to `st::attentionButtonFg`. `w` is the smallest width in `[4row, 5row]` whose truncated stop-table increment `1023 * 256 / (w * dpr)` has a fractional part in `[0.25, 0.75]`, else `4row`. |
| Scroll | `(0, 2row, 6row, height - 3row)` | Real `Ui::ScrollArea`, horizontal bar off, content of five alternating rows at content y `row / 2 + i * row`. |
| Overlay | `(0, height - row, 6row, row)` | The bottom overlay that keeps the scroll viewport above the owner's bottom. |

At the original height row 1 is wholly visible, while row 2 crosses the
viewport bottom and still maps whole inside P. The fixture gate is a Check
of exactly that: built, panel alive and shown, P six rows square, row 1
resolved by `ReadMappedTarget`, row 2 refused by its viewport while P
contains its whole mapping. Exposure is printed but is not part of the
gate. A refused gate turns every gated stage into `TEST_RESULT: N/A` with
`the full-owner capture fixture gate failed: ...`; the restore recovery
stages read N/A by their own `no panel sizing is owed` reason, and the
teardown still runs.

## Stages and expected verdicts

Every stage name starts `full-owner capture self-test:`. On a healthy host
every row passes and no row is N/A. In group A each arm is preceded by a
settle stage that waits until no record ended for 100 ms (bounded at
3000 ms), so an arm's paints are its own.

### Group A: coordinated samplers

Two samplers, one per child, share one `PostPaintCaptureScope`. Each action
counts its call, then takes one `GrabWidget` of P with the fixture's
capture flag set: a composite capture that synchronously paints both
children. The group runs inside its own `addGated` paint gate.

| Stage | Expected verdict |
| --- | --- |
| A1 idle control | Both samplers' 500 ms windows open and nothing requests a paint; `firstReceiptSince(mark)` refuses with `absent-receipt` for both. A stray paint is named in the details. |
| A1 one requested paint | A 3000 ms window on the first sampler, a mark, one `update()` of the first child. The first receipt is `Product`, index `mark + 1`, matched by the first child's own non-capture record with that index, received at or after the request, and followed by a row whose `paintAt` is that receipt and whose `lo` is after the paint ended. Otherwise this Check FAILs and sets the paint gate (`no product paint receipt within 3000 ms: absent-receipt: ...`), so A2 to A5 read N/A by that name. |
| A2 coordinated | 1200 ms windows on both, one `update()` on each child. Each child has exactly one product record and one row, `covered=1`, `paintAt` and `lastPaintAt` equal to that record's receipt, `lo` after the paint's end and within 250 ms of it; each child has at least two capture records, every one a `Capture` receipt; the last row's `hi` is at least 500 ms before the window closes; receipts since the mark reconcile one to one with the records. |
| A3 instance-local control | Two samplers without a scope whose actions grab only for their first four calls (a cap on the recursion), a 600 ms window, one `update()` of the first child. Passes when the defect shows: at least one capture paint of the second child is a `Product` receipt to its sampler, and exactly one real paint yields more rows than that. |
| A4 coalesced | An arm filter installed after the first sampler's filter runs first and, on the first non-capture paint, posts a call R ahead of the queued sample; R calls `repaint()`, a synchronous paint that joins the pending sample, and the first capture paint calls `update()` once. Premise: the second product record started before the first row's `lo` (`coalescing premise: repaint() was not synchronous` otherwise). Checks: three product records and two rows; row 1 `covered=2` with the receipts of records 1 and 2, row 2 `covered=1` for record 3, which started after row 1's `hi`; every record of both children matches its receipt kind. |
| A5 replacement | An arm filter on the second child posts a call D ahead of its sampler's queued sample. D reads the premise, destroys that generation, starts a replacement sampler (same scope, 1000 ms window) on a new generation at the same geometry, and only then shows it. Checks: the premise (one generation-1 product paint, a sample pending, no row); after the destruction no generation-1 action call or row, stop reason `owner-destroyed`, `receiptMark()` unchanged and nothing pending; every generation-2 record indexes the replacement sampler's receipts from 1 and its rows cover only generation-2 product paints. The next stage runs and releases the stopped sampler. |
| Group A: release | Outside the paint gate: every sampler is released while the children it observed are still alive. |

### Group B: strict render path

The crop K starts a third of the way into the gradient and ends at its
right edge, its full height, in owner coordinates, so the two arms'
gradient spans start at different device pixels. No paint is needed.

| Stage | Expected verdict |
| --- | --- |
| B1 measure | `RenderStrict(owner, owner->rect(), K)` (full owner) and `RenderStrict(owner, K, K)` (shifted direct region) both record the owner, rendered rect, crop and the cropped image's size, ratio and format. A NOTE prints both `StrictRenderText`s, the self-test's own `QImage::pixel` count (differing, largest channel delta, one-level pixels), the ratio, gradient width, device span, increment, its fraction and the predicted index offset. |
| B2 mismatch control | The independent count is nonzero; `CompareStrict(full, shifted)` refuses with `render-path-differs` and is not `equal()`; its diagnostic `differing` equals the independent count. |
| B3 repeated same path | A second full-owner render with the same owner, rect and crop is `equal()` and the independent count is 0. |
| B4 changed pixel | One pixel at the cropped image's centre, inside the gradient, inverted in a copy: the comparison is unrefused, `differing=1` at that point, not `equal()`, independent count 1. |
| B5 refusals | Another extent with the same crop refuses `render-path-differs`; the image converted to `QImage::Format_RGB32` refuses `format-differs`; the ratio plus one refuses `dpr-differs`; a crop reaching past the rendered rect is refused by `RenderStrict` with `requested=<rect>` named and no image. The stage saves `strict_full` and `strict_shifted` for inspection. |

### Group C: checked crop and panel sizing

The requested crop E is `(0, row, 6row, 5row + row / 2)`: from the
gradient's top to half a row past P's bottom. `rowCrop` is row 2's whole
rect mapped into P. Both are taken in C1 and reused unchanged after the
growth.

| Stage | Expected verdict |
| --- | --- |
| C1 crop beyond owner | `prepareCrop(owner, row 1, E)` is refused with `crop: ...` naming `requested=`, the owner extent (`widget=`) and the inside rows, no image, and `FailureCount()` unchanged. Pitfall premise: `Crop(GrabWidget(owner), DevicePixelRect(E, dpr))` is non-null and shorter than the requested device height. |
| C2 viewport clip | Premises: `owner->rect()` contains `rowCrop` and `ReadMappedTarget(owner, owner, rowCrop)` resolves, so owner-only and owner-as-origin checks would pass. `prepareCrop(owner, row 2, rowCrop)` is refused with `descendant: ... not fully inside the relevant viewport` naming the scroll viewport; `prepareCrop(owner, owner, rowCrop)` is refused as `owner-as-origin`; no failure is logged. |
| C3 joint typed reading | Joint Part C x Part D, deleted with either part: `ReadMappedTarget(owner, MappedRect::Local(owner, rowCrop))` resolves, while `MappedRect::Local(row 2, row 2's rect)` is refused by its viewport with its whole mapping kept. |
| C4 size modes | Pure: `PanelSizeModeRefusal` is empty for `Qt::WindowNoState` and `Qt::WindowActive`, and non-empty for minimized, maximized and fullscreen, each alone and with Active. A NOTE prints the live panel state. |
| C5 grow | `extra = max(rowCrop bottom + bottom reserve, E bottom) - P height`, the reserve measured as P's height below the scroll. `GrowPanelInnerHeight(panel, extra)` has no refusal, full telemetry and `applied = request + (0, extra)`, then `PanelSizingApplied` within 3000 ms; after the relayout P is exactly `extra` taller and fills the body. |
| C6 accept | `prepareCrop(owner, row 2, E)` is accepted with `widget()` P and an image of `E.size()` times the ratio of the self-test's own full-owner grab, and saved as `full_owner_crop_grown`; row 1 with E and row 2 with the unchanged `rowCrop` are accepted whole too. |
| C7 normal restore | Recovery stage, N/A unless a sizing is owed: `RestorePanelSizing`, then `PanelSizingRestored` within 3000 ms and a relayout. Exact original outer and body geometry, minimum, maximum and full window state; P six rows square again and row 2 refused by its viewport again. |
| C8 controlled abort | The same growth again, applied from the same frozen frame; then a recovery stage restores it through the very `RestorePanelSizing(state->sizing)` call the finish-path release makes, with no normal restore and no relayout first, and Checks the same exact restoration. |

## The N/A rule

B2's `skipReason` is B1's premise. When the independent count finds no
differing pixel, B2 writes `TEST_RESULT: N/A: ... - premise not
demonstrated: ...` with the gradient numbers, and the mismatch control is
not claimed. That N/A is never a pass. The fixture's geometry is under the
self-test's control and every harness grab has an integer device pixel
ratio, so treat it as a fixture flaw to repair - a larger crop offset, a
diagonal gradient, another `TDESKTOP_TEST_SCALE` - rather than as a host
that cannot show the mismatch.

Three premise refusals are FAIL rows instead: the fixture gate (every
gated stage then reads N/A), the A1 first receipt (A2 to A5 then read N/A)
and the A4 coalescing premise. Each is a test flaw to diagnose - for A1 the
`caffeinate` wrapper, the exposure reading and the panel's mapping; for A4
whether the panel gained a texture child or lost its mapping, which defers
`repaint()` - never a reason to loosen a check.

## Lifetime and cleanup

The self-test registers `Runner::onFinish` before any stage. It and the
teardown stage run the same idempotent release, in a fixed order: every
sampler first (a sampler goes before the widget it observes), then
`RestorePanelSizing`, then `RestoreWindowExposure`, then the panel. The
teardown is a recovery stage outside the fixture gate and Checks that the
panel and every fixture widget are released, that no sizing is owed and
that the exposure hint was cleared before the panel went. The state is
leaked on purpose, as this directory's other self-tests leak theirs.

## Verification

Its negative controls run in the same launch, so it needs no mutation
launch: A3 (instance-local samplers on the same fixture), B2, B4 and B5
(path mismatch, changed pixel, metadata refusals), the C1 and C2 premises
(raw `Crop` and the weaker checks really accept), C3, and C5's applied
growth, which gives C7 and C8 something to undo. Pack
`AppendMappedTargetSelfTest`, `AppendPostPaintSamplerSelfTest`,
`AppendPaintingLayerRootSelfTest` and `AppendCaptureViaWindowSelfTest`
beside it to show the existing capture and sampler contracts unchanged.
Record executed results in the change's evidence.

## Related guidance

[test_post_paint](test_post_paint.md); [test_capture](test_capture.md);
[test_panel](test_panel.md); [test_mapped_target](test_mapped_target.md);
[test_window_exposure](test_window_exposure.md);
[gated groups and recovery stages](test_runner.md#gated-groups-and-recovery-stages);
[failure diagnosis](../diagnostics.md).
