# test_keyboard_modifiers

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.
Source contract: [test_keyboard_modifiers.h](../../test_keyboard_modifiers.h).

## Use when

Verifying that `Test::PressKey` and `Test::QpaMouseSequence::end()` leave
Qt's cached keyboard modifiers, `QGuiApplication::keyboardModifiers()`,
released, while a press still delivers its modifiers to the receiver and to
Qt's shortcut map. `base::IsCtrlPressed()`, `IsShiftPressed()` and
`IsAltPressed()` compare that state exactly, so a stale value sends product
code down its modifier branch - a reply set after a Ctrl+Return submit opened
the reply-to-chat chooser. The behavior is described under
[keyboard modifier state](test_widgets.md#keyboard-modifier-state) and in
[QPA mouse cleanup](test_qpa_mouse.md#lifetime-and-cleanup). Product overlays
do not call this self-test.

## Fixture

`AppendKeyboardModifiersSelfTest(runner)` needs no account, session, chats
list or network - an empty portable root is enough - only the primary window
`Core::App().activePrimaryWindow()`. It parents a container to that window's
widget, centred and raised above its siblings, painted `st::windowBgOver`,
holding one `Recorder`: a focusable (`Qt::StrongFocus`) `Ui::RpWidget` four
cells wide and one cell high, inset half a cell, where a cell is
`st::defaultActiveButton.height`. The recorder accepts every key and mouse
press and release and records, inside its handler, the event's type, key,
own `modifiers()` (for a key, `QKeyEvent::modifiers()`, which toggles a
modifier key's own flag), Qt's cached modifiers at that moment and the
spontaneous flag.

The fixture stage waits until the recorder and its window's `QWindow` are
visible, then logs `KEYBOARD_MODIFIERS: fixture` and checks the
`fixture gate:`; a missing primary window fails it. Every later stage
inherits that gate through `Runner::addGated`.

## Minimal use

Appended by a disposable overlay's `test_scenario.cpp`; the repository copy
stays a no-op:

```cpp
void SetupScenario(not_null<Runner*> runner) {
	runner->waitEvent(u"launch_finished"_q);
	AppendKeyboardModifiersSelfTest(runner);
}
```

Run it through the workflow's `test-run` with an empty portable root and the
usual `-testagent -noupdate`.

## Stages and expected readings

Every measurement reads the cached state and the three `base::Is*Pressed()`
results right before its action and right after the action returns, before
any log, other input, activation, event-loop turn or cleanup; the checks
judge those saved readings. Each row's details start with
`KEYBOARD_MODIFIERS: leg=<leg>` and carry `before`, `after` with
`isCtrl isShift isAlt`, the press and release counts and every recorded
event with its own and its cached modifiers, on PASS as on FAIL.

- **press legs**, one turn. For each of Control, Shift and Alt (M):
  - `raw-<m>`, the control: a sent `Key_B` press and release carrying M,
    each in its own `Settle` with no reset - `PressKey` as it was before the
    reset. The recorder gets exactly that press and release, and Qt's cached
    modifiers read M afterwards.
  - `chord-<m>`: `PressKey(Key_B, M)`, starting from the raw pair's stale M,
    so restoring the value from before the call cannot pass. Exactly one
    `Key_B` press and release reach the recorder, both reading M in the
    event and in the cached state; right on return the state reads `0x0`
    with `isCtrl=0 isShift=0 isAlt=0`.
  - `alone-<m>`: `PressKey(<M's key>, NoModifier)`, for example
    `PressKey(Key_Control, NoModifier)`. Its press reads M in
    `QKeyEvent::modifiers()` and in the cached state - a modifier key
    pressed alone is a press, not a release; right on return the state reads
    `0x0` with every `Is*Pressed()` false.
- **shortcut**, one turn: a `QShortcut` for Ctrl+B with `Qt::WidgetShortcut`
  on the recorder, the context `Ui::InputField::setupMarkdownShortcuts()`
  gives its Bold shortcut. `ForceWindowActive(recorder)` outside `Settle`,
  `setFocus()`, then `PressKey(Key_B, ControlModifier)`, and only then the
  shortcut is destroyed. A prerequisite row requires an injected activation
  and the recorder as `QApplication::focusWidget()`; the shortcut row also
  requires exactly one `activated()` with Control cached and
  `IsCtrlPressed()` true inside it, no `activatedAmbiguously()`, no
  `KeyPress` at the recorder (Qt's shortcut map consumed it) and the
  released state right on return. The details add the focus widget and
  `WindowActivationDetails`.
- **QPA click**, one turn: one `QpaMouseSequence` on the recorder delivers a
  left press and release at its centre carrying `Qt::ControlModifier`. The
  control row requires both deliveries without refusal, the press's first
  receiver to be the recorder, the recorder to get the press reading Control
  in the event and in the cached state and then the release, and the cached
  state to read Control right before `end()`. The subject row requires the
  state released with every `Is*Pressed()` false right after `end()`, and
  `QpaMouseEnd::clean()`. The details add `QpaMouseDeliveryText` for both
  deliveries and `QpaMouseEndText`.

Expected: every row `TEST_RESULT: PASS`, no `FAIL` and no `N/A`; the
self-test emits no deliberate failure. A failed fixture gate leaves the
press, shortcut and QPA stages `N/A`: incomplete evidence, never a pass.
Built with `DESKTOP_APP_DISABLE_PRIVATE_QT_API`, every stage is `N/A` with
an `unsupported: ...` reason, since neither reset exists there.

## Readings and refusals

The self-test never treats a refusal as success. A recorder gone before or
after an action leaves that measurement `measured=0`, which fails its rows. A
shortcut leg whose activation or focus prerequisite fails decides nothing
about shortcuts: both its rows fail. A QPA refusal (`blocked-by-modal`,
`active-popup`, `window-hidden` and the others in `test_qpa_mouse.h`) or a
press whose first receiver is not the recorder fails both QPA rows, so a
delivery that never set Control cannot pass as released input.

## Lifetime and cleanup

The shortcut lives only inside its stage's turn, so the earlier `Key_B` +
Control legs reach the recorder's own handler. The QPA sequence is ended in
its turn. A recovery stage, `teardown - destroy the recorder`, runs even
after a gate refusal and destroys the container with the recorder; an
idempotent `Runner::onFinish` does the same on every finish path. The
activation the shortcut leg injected stays on the primary window, which
`ForceWindowActive` allows. Every stage ends with Qt's cached modifiers
released.

## Verification

The before-legs run in the same launch instead of a reverted build: the
three raw pairs reproduce the stale value the reset removes, and the QPA
control reads Control right before `end()`. By construction - not run as
separate mutations - dropping `PressKey`'s reset would fail all six
`chord-` and `alone-` released rows and the shortcut row; restoring the
value from before the call would fail at least the three `chord-` rows,
which start from the raw pair's stale M; dropping `end()`'s reset would fail
the QPA subject row; and changing the delivered events' modifiers would fail
the delivery rows and the shortcut row. One host is enough: the path is Qt's
platform-independent widget key delivery.

## Related guidance

[Input helpers](test_widgets.md#input-helpers);
[QPA mouse](test_qpa_mouse.md); [runner and gated groups](test_runner.md);
[launch behavior](../launch.md).
