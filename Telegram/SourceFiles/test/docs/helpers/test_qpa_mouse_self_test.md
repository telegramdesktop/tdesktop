# test_qpa_mouse_self_test

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Implementation: [test_qpa_mouse_self_test.cpp](../../test_qpa_mouse_self_test.cpp).
Entry contract: [test_qpa_mouse.h](../../test_qpa_mouse.h).

## Use when

Verifying the [QPA mouse helper](test_qpa_mouse.md): Qt's grab routing of its
deliveries, the drag stage's queued-work wait and refusals, its coordinate
conversion, its double-click protection, its clean end state, and that its
spontaneous deliveries pass an armed [console shield](test_console_shield.md).
Product overlays do not call it. The measured ingredient and high-DPI results
are recorded in the reference's [Verification](test_qpa_mouse.md#verification).

## Fixture

`AppendQpaMouseSelfTest(runner)`, appended by a disposable overlay after
`runner->waitEvent(u"launch_finished"_q);` and before
`AppendConsoleShieldSelfTest`, which comes last. It needs no account, chats
list, network or wallet - an empty portable root is enough - and a desktop
that can show two windows. Both are parentless `Ui::RpWidget` top-level
windows shown without activation (`Qt::WA_ShowWithoutActivating`, never
raised) with `Qt::WA_QuitOnClose` cleared, stacked in the corner of the
screen farthest from the real cursor: one holds two probe pairs in two rows,
the subject and the control, the other one `LandingPad`. A pair is a
`GrabParent` of two by two cells (a cell is `st::defaultActiveButton.height`)
and its `ForwardingChild` of one cell, inset half a cell. The child accepts
every mouse event and forwards only its press and release to the parent, with
mapped local and global positions; the parent grabs the mouse on that press,
records every event with the grabber at that moment, releases the grab on
the release and starts a 200 ms `Ui::Animations::Basic`, counting its ticks.
At held move 2 the pair posts its own `InvokeQueued` and `crl::on_main`
calls, each recording whether the release had arrived. The gesture goes from
the child's centre to one cell right of the parent: outside the child and the
parent, inside the window. An object filter on each `QWindow` records what
the window gets and its spontaneous flag.

The fixture stage logs `QPA_MOUSE_SELF_TEST: fixture` - Qt version, the
pairs window's `devicePixelRatio()`, Qt's conversion factor
(`QHighDpiScaling::factor`), the double-click interval and distance, the
animation mode, the cursor and every geometry - and its `fixture gate:`
requires both windows shown inside the screen and Qt holding no button,
grab, hover, popup or modal. Every later stage is gated on it
(`Runner::addGated`).

## Stages and expected readings

Each stage is named `QPA mouse self-test: <leg>`; the drags it appends are
`QPA mouse drag: self-test <name>`. Every leg also requires Qt to hold no
button, no grab and no hovered fixture widget right after its cleanup.

- **subject**: `AppendQpaMouseDrag`, eight unpaced moves. The child gets only
  the press; the parent gets the forwarded press (sent), eight moves with the
  left button held through its grab, outside the child and outside itself
  too, and the release through the grab; then Qt's leave to the child and the
  parent. Every position is within 0.01 logical px of the requested local and
  global point; the window gets press, eight moves and release, all
  spontaneous, and no double click; the pair's own two calls both ran before
  the release; the reading is `delivered()`, both markers ran, the child took
  the press, the parent held the grab and took the release, and `end()`
  needed only its leave. Then the animation the release started ends within
  2000 ms of the release with ticks and no later input
  (`QPA_MOUSE_INGREDIENT: leg=both`).
- **Test::Drag control**: `Test::Drag` with the same points on the control
  child. The child gets Test::Drag's press, eight moves, release and leave;
  the parent only the forwarded press and release; the window nothing; the
  pair's own calls run after the release. The opposite route.
- **ingredient raw, settle, wait**: the subject gesture again through direct
  `QWindowSystemInterface` calls with the helper's mapping and pre-press
  preparation, with neither the settle nor the queued-work wait, the settle
  only, or the wait only. Each must take the subject's exact route and start
  and end clean, after its explicit release, grab and leave cleanup and a
  drain. Whether its animation ends is logged as a `QPA_MOUSE_INGREDIENT` row
  and never judged; `QPA_MOUSE_INGREDIENTS` puts all four side by side.
- **coordinates**: on the pad, an unconverted direct click - the window's
  logical point P passed as native - then the helper's click at P
  (`QPA_MOUSE_COORDINATES`). The helper lands at P within 0.01 px, local and
  global; the control within 0.5 px of P / factor. At a factor other than 1
  the control must land more than 0.5 px from P. At factor 1 - every default
  macOS launch, Retina DPR 2 included - it lands on P, and the row says the
  run does not decide: the discriminating reading needs a `QT_SCALE_FACTOR=2`
  launch.
- **double click**: two helper clicks through two sequences at one pad point,
  a runner tick apart and inside the double-click interval, reach the pad
  and its window as press, release, press, release; `end()` called again on
  the completed sequence returns the same reading and delivers nothing. The
  positive control: two direct clicks stamped 1 to 4 ms after the helper's
  last timestamp, the pairing button reset before the first press only, give
  the pad press, release, double click, release, and the window the marked
  second press too.
- **armed console shield**: the self-test arms its own shield (label
  `qpa-mouse-self-test`) and repeats the subject paced at 50 ms: the same
  route checks, all eight moves, the shield's reading unchanged with no veto;
  then it releases its own arm before any window closes.
- **destroyed target**: a fresh pair in the control row queues its own
  deletion at held move 2, paced at 50 ms. It gets exactly two moves, in
  separate turns, no release and nothing after the deletion; the reading is
  `target-destroyed` after two of eight moves, and `end()` releases the held
  button through the live window.
- **bound expired**: a fresh pair withholding every `QEvent::MetaCall`, bound
  300 ms: the Qt marker never ran, the main-queue marker did, `waitedMs` is at
  least 300, the reading is `bound-expired`, and the pair gets exactly one
  release, through its grab.
- **refusals**: a never-shown window is refused `no-window-handle` and gets
  no `QWindow`; a shown `Qt::WA_DontShowOnScreen` window is refused
  `window-hidden`. Nothing is sent and the pads record nothing.
- **teardown**, a recovery stage: releases the self-test's own shield arm if
  it still holds one, cleans up a direct gesture left open and destroys the
  windows. A `Runner::onFinish` registered after every drag stage does the
  same on every finish path, after each drag's own finish callback.

Expected: every row `TEST_RESULT: PASS` and no `FAIL`. Built with
`DESKTOP_APP_DISABLE_PRIVATE_QT_API`, every delivering stage is `N/A` with an
`unsupported: ...` reason, and one added stage checks that a delivery and the
subject's drag are refused `unsupported` and send nothing. A failed fixture
gate leaves the gated stages `N/A`: incomplete evidence, never a pass. The
scale factor is process-wide, so the coordinate control's discriminating
reading takes a second launch with `QT_SCALE_FACTOR=2`.

The controls catch these mutations: `sendEvent` in place of QPA delivery
fails the route and spontaneity checks; dropping the coordinate conversion
fails the coordinates leg at a factor other than 1; dropping the pairing
reset pairs the helper's second click; dropping `end()`'s release or leave
fails the end-state readings; posting the markers before the moves, or
releasing before both queues ran, fails the pair's own before-release
readings; a spontaneous-only console-shield rule vetoes the armed leg.
