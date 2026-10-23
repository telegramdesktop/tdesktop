# test_panel

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_panel.h](../../test_panel.h).

## Use when

Separate-panel discovery and show-cache readiness, and the typed sizing
that makes an oversized `Ui::SeparatePanel` fixture measurable. Read the
contract below and the source header before use.

## Contract and usage

Distinguish a live `Ui::SeparatePanel` from its faded/squeezed show-animation
cache, and answer which top-level `Ui::SeparatePanel`s exist.
`Test::WalkPanels(&scan, query)` is the one walk eight disposable overlays
each wrote by hand: it enumerates them with a caller-supplied `exclude` of
panels the caller already holds — never a type — and a caller-chosen
`Test::PanelLiveness`, where `Settled` is decided by this module's own
`ReadPanelShowState` and not by `isVisible()`, so a panel still painting its
show cache is not counted as one that opened, while `Shown` is kept beside it
as that reading's control. A mark is simply a previous answer:
`Test::PanelList` is `std::vector<QPointer<QWidget>>`, so it keeps nothing
alive and a panel at a reused address cannot read back as one the mark held,
and `query.before` — or `Test::PanelsAdded`, the same implementation — reads
the difference back as pointers and never as a count. A walk that reached no
`Ui::SeparatePanel` of any kind refuses by name instead of answering a zero,
certified through `Test::DiscriminatingScan` whose control is every panel the
walk reached, so "panels reached, none settled" is a certified zero while "no
panel reached" is not; and `Test::PickPanel` refuses an ambiguous identity
ask, naming every candidate it saw, in three refusal sentences no two of which
read alike. `PanelWalkText` / `PanelListText` are the pure, pollable readings
and `PANEL_WALK` the one reporting row. Identifying a panel from its labels is
`FindAll<Ui::FlatLabel>` + `CheckTextReads` composed at the call site; no
label-join idiom is promoted here. Own self-test over a synthetic top-level
panel it creates, shows and takes down itself.

## Sizing an oversized panel fixture

A fixture inside a `Ui::SeparatePanel` can be too short for its evidence: a
full row or band that must be captured whole ends below the panel's body,
or its scroll viewport is cut above a bottom overlay. Grow the panel's inner
height, never the request's crop: `GrowPanelInnerHeight`,
`PanelSizingApplied`, `RestorePanelSizing`, `PanelSizingRestored` and
`PanelSizingText`, with the pure classifiers `WindowStatesText`,
`PanelSizeModeRefusal` and `PanelSizeModeStates`.

**Typed panel.** Hold the fixture as `QPointer<Ui::SeparatePanel>` and
pass `not_null<Ui::SeparatePanel*>`. A `Ui::SeparatePanel` is a
`Ui::RpWidget`, not a `Ui::RpWindow`: its size goes through its public
`setInnerSize(size, allowResize = false)`, whose size includes the title
(`st::separatePanelTitleHeight`), so the body is the request minus the
title. The first call on a panel with an empty rect takes `initGeometry`,
which clamps the height to the screen's available height and centres the
panel on its visible parent window or the active window, else on the
screen's available area; every later call takes `updateGeometry`: outer
size = request + padding as a fixed size (a minimum size for a resizable
panel), no clamp and no move, ignored in full screen. That later path is
what makes a panel taller than its first, clamped size. The
[`Ui::RpWindow` static-dispatch rule](test_widgets.md#exact-objects-and-actions),
whose non-virtual `setGeometry` and friends hide the `QWidget` members,
applies to a `Ui::RpWindow` only; a panel's `move()` is the plain `QWidget`
member. Do not confuse the two window types.

**Size modes and activation.** `windowState()` is the stored OR of
`Qt::WindowMinimized` (0x1), `Qt::WindowMaximized` (0x2),
`Qt::WindowFullScreen` (0x4) and `Qt::WindowActive` (0x8). Active is
independent of the size mode, so state 8 is an active normal window - what
`showAndActivate()` leaves - and is sizable. `PanelSizeModeRefusal` refuses
only a set minimized, maximized or fullscreen bit; `WindowStatesText`
prints `<int> (normal|<modes joined by +>[+active])`. In Run 4 of
`2026/10/18/tighten-money-tabs-without-moving-the-transactions-title` a
sizing prerequisite of `windowState() == Qt::WindowNoState` refused that
campaign's active normal panel (state 8) and never resized it; Recovery 4
refused size modes only. The helpers never activate, raise or change the
window state: activation would take the console owner's focus and change
the state the sizing records, and no sizing needs it, so a fixture is shown
without activating and the Active acceptance is decided on the classifier.

**The macOS fixed-size zoom reading.** On macOS a raw maximized bit does
not mean anything maximized the panel. Qt cocoa reports `NSWindow.zoomed`
as `Qt::WindowMaximized` (`QCocoaWindow::windowState()` in
`qcocoawindow.mm`), and AppKit reads a window whose minimum equals its
maximum as zoomed: `windowWillUseStandardFrame` answers the current
position with the maximum size, and the zoom frame is clamped to the
window's only possible size. A shown fixed-size frameless panel - every
panel the gate accepts, since a resizable one is refused - therefore reads
2 (`maximized`), or 10 (`maximized+active`) when active, and can read 0
after a programmatic resize or move. In Run 1 of
`2026/10/19/qualify-full-owner-captures-and-bind-mapped-rectangles-to-their-owner`
the gate refused the self-test's shown, never activated 222x284 panel
(min = max = 222x284) as `size mode maximized: state=2 (maximized)`; the
source campaign's state 8 was observed on Windows, where the bit does not
come from a zoom test. So every sizing helper decides on the effective
state `PanelSizeModeStates(state, outer, minimum, maximum)`: the raw state
unchanged, except that on macOS, for a fixed-size panel at its fixed size
(minimum = maximum = outer size), `Qt::WindowMaximized` is cleared. That
bit is derived from the frame and the constraints, which the helpers
restore exactly, and a zoom cannot change such a frame. Minimized and
fullscreen are never cleared, `Qt::WindowActive` is kept, maximized stays
refused on any other frame - where a zoom does change it - and on every
other platform nothing is cleared, so the rule above is unchanged there.

**Freeze, apply, restore.** `GrowPanelInnerHeight(panel, extra)` records
the outer geometry, body (`innerGeometry()`), current inner request
(`rect().marginsRemoved(computePadding()).size()`), minimum, maximum and
full raw window state first, so a refused sizing still prints full
telemetry. It then refuses, first applicable:

| Refusal | Why |
| --- | --- |
| `not a top-level window: <panel>` | Only a window's geometry is the panel's. |
| `hidden: <panel>` | A hidden panel has no settled geometry to restore. |
| `size mode <modes>: state=<WindowStatesText> - ...` | The recorded frame's effective state (`PanelSizeModeStates`, printed as `state=`) is minimized, maximized or fullscreen, and such a panel is not sized through `setInnerSize`. |
| `resizable panel: minimum=<WxH> maximum=<WxH> - setInnerSize would reset allowResize` | `allowResize` defaults to false, so the call would silently make the panel fixed and drop its resize edges. |
| `no height increase requested: extra=<n>` | `extra <= 0`. |

Otherwise it calls `setInnerSize(request + QSize(0, extra))`, stores that as
`applied` and the sizing is `owed`. `RestorePanelSizing` is idempotent: when
owed it calls `setInnerSize(request)` and `move(outer.topLeft())` on a live
panel and clears `owed`; a panel already gone owes nothing.
`PanelSizingRestored` is true once nothing is owed, the geometry, inner
geometry, request, minimum and maximum equal the recorded values exactly
and the effective state - size mode plus the `Qt::WindowActive` bit -
equals the recorded one. The raw state is not compared: the macOS zoom bit
flips on the helper's own `setInnerSize`, and recreating it would need
`showMaximized()`, a window-state change the helpers never make, so the
restore contract is exact geometry, request, constraints, Active bit and
real size mode. `PanelSizingText` prints `panel= frozen={outer= body=
request= min= max= state= effective=} now={...} applied= owed= refusal=`,
with `now=<gone>` once the panel is destroyed; `state=` is the raw
`windowState()` and `effective=` its `PanelSizeModeStates` on that frame,
so a cleared zoom bit is always visible (`state=2 (maximized)
effective=0 (normal)`).

**Waits and order.** Native geometry settles asynchronously on macOS: wait
for `PanelSizingApplied` (current request equal to `applied`, body grown by
exactly `extra`, effective state unchanged) and, after the restore, for
`PanelSizingRestored` in a pure, bounded `until`. Lay the panel's children
out after that wait settled, never from the panel's `sizeValue()`, which
fires before `SeparatePanel::resizeEvent` lays out the body. Call
`RestorePanelSizing` from `Runner::onFinish` and from a restoration stage
(`Runner::addRecovery`, applicable while owed), before the panel is
destroyed and after any sampler or capture that still reads it.

```cpp
runner->add({
	.name = u"grow the panel by the row it cuts"_q,
	.run = [=] {
		state->sizing = Test::GrowPanelInnerHeight(
			state->panel.data(),
			state->extra);
		state->from = crl::now();
	},
	.until = [=] {
		return !state->sizing.owed
			|| Test::PanelSizingApplied(state->sizing)
			|| (crl::now() - state->from >= 3000);
	},
	.then = [=] {
		Test::Check(
			Test::PanelSizingApplied(state->sizing),
			u"the panel grew by the measured extra"_q,
			Test::PanelSizingText(state->sizing));
	},
});
// In the setup: runner->onFinish([=] { Test::RestorePanelSizing(state->sizing); });
```

Confirm with `AppendFullOwnerCaptureSelfTest` group C
([self-test](test_full_owner_capture.md)): the pure size-mode classifier
and its effective state on fixed and zoomable frames, a growth by a
measured extra, and exact restoration on the normal path and through the
finish-path call.
