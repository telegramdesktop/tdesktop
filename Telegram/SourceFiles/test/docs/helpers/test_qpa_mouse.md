# test_qpa_mouse

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API.
Source contract: [test_qpa_mouse.h](../../test_qpa_mouse.h).
Self-test: [test_qpa_mouse_self_test](test_qpa_mouse_self_test.md).

## Use when

The subject depends on Qt's own window-level mouse routing: a widget that
calls `QWidget::grabMouse()` on a press and expects the following moves and
the release through that grab, or one whose children forward only their
press and release to it. `Test::Click`, `Test::Drag` and
`Test::AppendQueuedStartDrag` send every event straight to the named widget
with `QApplication::sendEvent`, so moves sent to a child never reach a
grabbing parent. This module delivers press, move and release through
`QWindowSystemInterface::handleMouseEvent<SynchronousDelivery>` to the
target's top-level `QWindow`, so `QWidgetWindow::handleMouseEvent` picks the
receiver as it does for real input - the widget under the point for a
press, then the grabber or the pressed widget
(`QApplicationPrivate::pickMouseReceiver`) for the held moves and the
release, wherever they are - and every event is spontaneous. Known
consumers: `CollectiblesGrid` (`wallet/wallet_collectibles.cpp`), whose
squares forward only their press and release to the grid that grabs, and
the reorder drag of `Info::PeerGifts::InnerWidget`
(`info/peer_gifts/info_peer_gifts_widget.cpp`). Overlays drove both by
hand-written `QWindowSystemInterface` calls before; the
[failure row](../diagnostics.md) has the signature.

Two entry points:

- `Test::QpaMouseSequence` - individual deliveries to one target, in the
  caller's order, in one turn or across turns, each answering a
  `QpaMouseDelivery`; the caller ends it with `end()`. A click is a press and
  a release through one sequence.
- `Test::AppendQpaMouseDrag` - one stage: a left-button press, interpolated
  held moves (all at once, or one per `paceMs` across turns), a bounded wait
  for the queued work they posted, the release and `end()`, filling a
  `QpaMouseDrag` reading.

Stay with the existing helpers when the named widget owns the events and
routing does not matter: `Click` for a button, `Drag` for a drag whose effect
is synchronous, `AppendQueuedStartDrag` for a message-list, chats-list or
label drag whose start is queued (none of them grabs). Keys, wheel, text
drops and context menus have their own helpers
([input helpers](test_widgets.md#input-helpers)). This module delivers only
mouse buttons and moves; it has no hover or click convenience API.

## Preconditions and limits

**Build and target.** Qt 6 with the private API: a build with
`DESKTOP_APP_DISABLE_PRIVATE_QT_API` refuses every delivery and drag
`unsupported` and sends nothing. The target is a live widget in a shown
ordinary top-level window that already has its `QWindow`; no `winId()` is
called to create one. A top-level with `Qt::WA_DontShowOnScreen` is refused,
because Qt's offscreen path (`QApplicationPrivate::sendMouseEvent`) keeps the
pressed widget after the release.

**State the caller owns.** No button held and no mouse grab when a sequence
starts, and no other gesture meanwhile. No popup open, even in the target's
own window (`QWidgetWindow::handleMouseEvent` takes its popup branch for any
active popup), and no modal window blocking the target's. Restoring a drag or
a grab that existed before the sequence is not promised.

**Coordinates.** Points are in the target's logical (device-independent)
pixels. The helper maps them with `target->mapTo(target->window(), p)` and
`target->mapToGlobal(p)` and converts the results with
`QHighDpi::toNativeLocalPosition` and `toNativeGlobalPosition`, the inverse
of what `handleMouseEvent` applies; each `QpaMouseDelivery` prints all four
points. Never multiply by `devicePixelRatio()` or pass
`QHighDpi::toNativePixels` positions by hand. On macOS native coordinates are
Cocoa points: Qt's conversion factor (`QHighDpiScaling::factor(window)`) is
only `QT_SCALE_FACTOR` or `QT_SCREEN_SCALE_FACTORS`, because `QCocoaScreen`
reports a logical base DPI of 72, while `QWindow::devicePixelRatio()` is that
factor times the backing scale. A default launch on a Retina display reads
DPR 2 with an identity conversion, so an unconverted point lands right there
by accident; at `QT_SCALE_FACTOR=2` it lands at half the point. On Windows
the factor is the screen's DPI / 96, which is why the source overlay needed
the conversion there.

**What Qt does with each event.**

- Timestamps are Qt's own: the timestamp-less `handleMouseEvent` overload.
- No double click. Before every press the helper resets Qt's double-click
  pairing button (`QGuiApplicationPrivate::mousePressButton`). Qt pairs a
  press into a `MouseButtonDblClick` only when its button equals that one,
  so a helper press never pairs with an earlier press - its own, a raw
  delivery's or a real one - whatever the timestamps. Spacing timestamps
  could not promise that against a press stamped on another time base.
- No move before a press. The helper also puts Qt's last cursor position
  (`QGuiApplicationPrivate::lastCursorPosition`) on the press point, so Qt
  synthesizes no move ahead of it: the harness has no pointer before its
  press.
- Duplicate moves are dropped. Qt drops a move to the global point it
  already holds (`QGuiApplicationPrivate::processMouseEvent`); such a move
  reads `sent=1 delivered=0`, and a drag's `moves` counts delivered moves
  only. A move to the press point right after the press is dropped too. A
  drag's interpolated points are rounded, so a gesture whose longer axis
  spans fewer than `steps` logical pixels repeats points and reads fewer
  `moves` than `steps`.
- A release away from Qt's last cursor position is preceded by a move Qt
  synthesizes to the release point, still holding the released button. The
  drag's release, at its last move's point, gets none; a sequence's caller
  that releases elsewhere gets one, counted in that delivery's
  `windowEvents`.
- The pairing limit. The reset protects only the helper's own presses.
  After a helper press Qt keeps its button as the pairing button until a
  move goes past the double-click distance, so a later press that is not
  the helper's - an overlay's raw delivery, or a real click on the same
  window - inside `QStyleHints::mouseDoubleClickInterval()` can still pair
  with it into a double click. A raw delivery resets
  `QGuiApplicationPrivate::mousePressButton` itself before its press, as the
  self-test's direct legs do, or goes through a sequence.
- A move with no button held is delivered like a real hover move. Whatever
  hover Qt sets from it, or from a release over a widget, lasts until the
  sequence's `end()` delivers its leave.

**Settle, the queued-work wait and animation waits.** Three different
waits; none replaces another.

- Every delivery, and each release and leave `end()` sends, runs inside
  `Test::Settle`: postponed calls are deferred during it and drained after
  it, as for every input helper. That covers `Ui::PostponeCall` only.
- `AppendQpaMouseDrag` adds the queued-work wait of `AppendQueuedStartDrag`.
  After the last move's settle it posts `Test::PostQueuedWorkMarkers(target)`
  ([shared markers](test_queued_drag.md#shared-queued-work-markers)) and
  releases only once both ran, or `bound` passed. Their running means what
  "The guarantee" in `test_queued_drag.h` states: every normal-priority
  queued call and every `crl::on_main` call posted on the main thread while
  the press and the moves were handled has run. Calls those calls post,
  timers, network answers and deferred deletes are not covered, nor is
  anything the release itself queues. A `QpaMouseSequence` has no wait of
  its own: between deliveries in separate stages, post the markers and wait
  for them the same way.
- Neither waits for an animation. A `Ui::Animations::Basic` the release
  starts is scheduled through `Ui::PostponeCall` (which the release's settle
  drains) or `InvokeQueued` while
  `Ui::Animations::Manager::SetScheduleWithInvokeQueued(true)` holds, then
  ticks from a timer. Observe it in a pure `until` with its own bound and
  assert in `then`, as the self-test's subject leg does with 2000 ms.

**Activation and focus.** The helper never raises, activates or focuses a
window and never moves the OS cursor. Routing needs no active window:
`QWidgetWindow::handleMouseEvent` routes a press, the moves and the release
in an inactive window as in an active one. As with real input, a spontaneous
press may give focus to a `Qt::ClickFocus` widget inside its window
(`QApplication::notify`). A caller whose handlers gate on `isActiveWindow()`
or on the focus widget calls `Test::ForceWindowActive` first, in a stage
action outside `Settle`: it flushes queued platform events, which inside
`Settle` would pass an armed console shield as harness input. The popup and
modal refusals keep Qt's own `raise()` and `activateWindow()` paths in
`QWidgetWindow` out of reach.

**The console shield.** Every delivery is spontaneous at the `QWindow` and
at its first widget receiver, exactly like console input. An armed
[console shield](test_console_shield.md) passes it because it is delivered
inside `Settle`, whose `Test::HarnessInputScope` marks it harness input at
that event-loop level; the spontaneous flag plays no part. Input handled in
an event loop entered from inside a delivery (`QDrag::exec`, `QMenu::exec`,
a modal dialog) is outside the scope and vetoed like console input.

**A real platform drag.** A gesture that reaches `QDrag::exec()` starts a
real platform drag, which drops at the real cursor. The "SAFETY"
obligation in `test_queued_drag.h` applies unchanged: before launching, hook
the function whose `QDrag::exec()` the gesture reaches -
`Window::MainWindow::launchDrag()` or `Ui::FlatLabel::executeDrag()` - in the
overlay, as its first statement, returning before `new QDrag`. A launch
without that hook is not safe to run.

## Minimal use

An overlay sketch against the real API; no translation unit compiles it.
`state->grid` is a published live object, `state->gridAnimating()` the
overlay's own reading of the animation the release starts and `kReturnBound`
its own bound for that animation:

```cpp
const auto drag = AppendQpaMouseDrag(runner, {
	.name = u"grid reorder"_q,
	.resolve = [=] {
		return QpaMouseDragTarget{
			.widget = state->grid.data(),
			.from = state->squareCenter,
			.to = state->dropPoint,
		};
	},
});
runner->add({
	.name = u"the reorder's return animation ends"_q,
	.run = [=] { state->waitStarted = crl::now(); },
	.until = [=] {
		return !state->gridAnimating()
			|| (crl::now() - state->waitStarted > kReturnBound);
	},
	.then = [=] {
		Check(
			drag->delivered() && drag->released && drag->end.clean(),
			u"grid reorder: QPA drag through the grid's grab"_q,
			QpaMouseDragText(*drag));
		Check(
			!state->gridAnimating(),
			u"grid reorder: the return animation ended"_q);
	},
});
```

One click through a sequence, ended in the same call:

```cpp
auto sequence = QpaMouseSequence(target);
const auto press = sequence.deliver({
	.type = QpaMouseType::Press,
	.point = point,
	.button = Qt::LeftButton,
	.buttons = Qt::LeftButton,
});
const auto release = sequence.deliver({
	.type = QpaMouseType::Release,
	.point = point,
	.button = Qt::LeftButton,
});
const auto end = sequence.end();
Check(
	press.delivered && release.delivered && end.clean(),
	u"QPA click reaches the target"_q,
	QpaMouseDeliveryText(press) + u"; "_q + QpaMouseEndText(end));
```

Treat a refusal as a fixture gate (`Skipped` or a `fixture gate:` row),
never as the product's answer.

## Readings and refusals

`QpaMouseDelivery` (`QpaMouseDeliveryText`): `sent` - the event went into
`QWindowSystemInterface`; `delivered` - the target's `QWindow` received it;
`accepted` - what `handleMouseEvent` answered; `windowEvents` - every mouse
event the `QWindow` got during the call, Qt's synthesized move or double
click included; `receiver` - the first widget Qt sent it to, described during
the delivery, since a handler may delete it; `grabber` -
`QWidget::mouseGrabber()` after it; `spontaneous` and `receiverSpontaneous`;
the logical `local` and `global` points and the native ones given to Qt;
`buttons` - `QGuiApplication::mouseButtons()` after it; `targetAlive`.

`QpaMouseEnd` (`QpaMouseEndText`): `held` - the buttons the sequence's
presses still held; `releaseSent` - they were released through the saved
`QWindow`, as cleanup, not as the gesture's release; `buttonsCleared` - that
window was gone or a popup or modal was in the way, so only those bits were
cleared from Qt's state and nothing was sent; `grabReleased` - a grab that
appeared after the first delivery was released; `leaveSent`; then
`buttons`, `grabber` and `hovered` (the target window's widgets with
`underMouse()`). `clean()` is ended with no button, no grabber and nothing
hovered.

`QpaMouseDrag` (`QpaMouseDragText`, Noted as one `QPA_MOUSE_DRAG:` row):
`pressed`, `moves` delivered, the press's receiver, the grabber after the
press and the release's receiver, the markers as `then` found them
(`qtQueue`, `mainQueue`, `waitedMs`), `released` - the gesture's own release
reached a live target's window, `end`, `aborted` - the runner finished before
`then`, and `finished`. `delivered()` is finished with no refusal. The first
refusal is kept: a later one, or the wait's outcome, never replaces it.

| Refusal | Meaning |
| --- | --- |
| `unsupported` | Built with `DESKTOP_APP_DISABLE_PRIVATE_QT_API`; nothing is sent. |
| `no-target` | No widget was given; a drag's resolver answered none. |
| `target-destroyed` | The target died before a delivery, or for a drag during a delivery or between turns; nothing more is sent to it, and its `end()` releases the held button through the window if that still lives. |
| `no-window-handle` | The target's top-level has no `QWindow`: it was never shown. |
| `window-hidden` | The top-level or its `QWindow` is not visible, or the top-level has `Qt::WA_DontShowOnScreen`. |
| `active-popup` | A popup is open, the target's own window included. |
| `blocked-by-modal` | A modal window blocks the target's window: Qt would drop the event, and its modal path may raise a window. |
| `bound-expired` | A drag's markers had not both run within `bound`; the release is still sent, so no button stays down, and the order is not established. |

A refusal before a sequence's first delivery changes no pointer state.
Every refusal means the measurement did not happen; it is never a PASS.

## Lifetime and cleanup

A `QpaMouseSequence` keeps `QPointer`s to the target, its top-level and that
`QWindow`, the buttons its presses hold and the last delivered native points,
so `end()` needs neither a live target nor another mapping. `end()` is the
caller's obligation, from `Runner::onFinish` too when a sequence outlives its
stage: it releases the held buttons through the saved window (else clears
just those bits), releases a grab that appeared after the first delivery,
delivers a `QWindowSystemInterface` leave - each inside `Settle` - and reads
the end state. Called again with no delivery in between it sends nothing and
returns the same reading; a delivery after it opens the sequence again. The
destructor delivers nothing.

`AppendQpaMouseDrag` registers its own `Runner::onFinish` when appended: it
stops the pace timer and ends a sequence its stage left open, marking that
reading `aborted`. Callbacks run in registration order, so a fixture's
`onFinish` that destroys the target's windows is registered after the
`AppendQpaMouseDrag` calls; the self-test does so. Nothing writes a reading
after its `then`. The stage's timeout is `bound` plus `steps` x `paceMs` plus
`kDefaultStageTimeout`, so the helper's own refusal comes first.

Residue, by design. QPA delivery changes Qt's own pointer state - the
pressed buttons (`QGuiApplication::mouseButtons()`), the mouse grab, the
pressed and last mouse receiver, the mouse window and the last cursor
position - and never the OS pointer. `end()` undoes the buttons, the grab
and the hover. It leaves Qt's last cursor position on the last delivered
point, and its leave clears Qt's current mouse window and last mouse
receiver rather than restoring them, so the next real or harness move
starts from there. `QCursor::pos()` asks the platform cursor on macOS,
Windows and X11 (`QCocoaCursor::pos`, `QWindowsCursor::pos`,
`QXcbCursor::pos`) and still reads the OS pointer, so a ripple that a press
starts at `QCursor::pos()` begins where the owner's mouse is, as with
`Click`. A platform cursor without its own `pos()` answers Qt's last cursor
position instead (`QPlatformCursor::pos`).

## Verification

`Test::AppendQpaMouseSelfTest(runner)` ([self-test](test_qpa_mouse_self_test.md)),
appended after `runner->waitEvent(u"launch_finished"_q);` and before
`AppendConsoleShieldSelfTest`, with no account, chats list, network or
wallet. On purpose-built probe pairs mirroring `CollectiblesGrid` - a child
that forwards only its press and release to a parent that grabs on that
press and starts a release animation - it checks grab routing and the exact
sequence against a `Test::Drag` control, the probe's own queued calls running
before the release, the release-started animation ending with no later
input, Run 1's ingredients (logged, not judged), coordinates against an
unconverted control, no double click against a stamped positive control,
`target-destroyed`, `bound-expired`, `no-window-handle` and `window-hidden`,
a paced gesture under the self-test's own armed console shield with no veto,
and a clean end state after every leg. The coordinate control discriminates
only in a launch with `QT_SCALE_FACTOR` other than 1, so its evidence takes
a second launch with `QT_SCALE_FACTOR=2`; a default macOS launch reads factor
1 even at DPR 2. `unsupported` is checked only in a build with
`DESKTOP_APP_DISABLE_PRIVATE_QT_API`, where every delivering stage is
`N/A`.

Run 1's ingredients, from `2026/10/20/keep-wallet-gift-frames-sharp-on-high-dpi-screens`
(`work/test.md`, Attempt 1): its overlay delivered every event raw, with a
bare `SettlePostponedCalls()` and a fixed 1200 ms wait, and the 185 ms
`DraggedGift` return its release started completed only at teardown; Run 2
added the settle per delivery and the queued-work wait together and the
return completed in 186 ms with 23 ticks, without isolating which one was
needed. The self-test repeats the gesture through direct
`QWindowSystemInterface` calls with neither, with the settle only and with
the wait only (`QPA_MOUSE_INGREDIENT` and `QPA_MOUSE_INGREDIENTS` rows).
Pending: the measured ingredient result awaits the first self-test run of
task `2026/10/20/shield-console-input-and-drive-mouse-grabbing-widgets-through-qpa`.
Pending: the measured high-DPI coordinate result awaits the
`QT_SCALE_FACTOR=2` self-test run of task
`2026/10/20/shield-console-input-and-drive-mouse-grabbing-widgets-through-qpa`.

## Related guidance

[Input helpers and `Settle`](test_widgets.md#input-helpers);
[queued-start drags and the SAFETY hook](test_queued_drag.md);
[console shield](test_console_shield.md);
[runner and `onFinish`](test_runner.md); [launch behavior](../launch.md);
[failure diagnosis](../diagnostics.md).
