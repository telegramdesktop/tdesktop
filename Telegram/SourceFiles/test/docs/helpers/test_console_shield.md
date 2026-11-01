# test_console_shield

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API (includes a harness self-test).
Source contract: [test_console_shield.h](../../test_console_shield.h).

## Use when

A `-testagent` campaign must not be disturbed by the owner's own keyboard
and pointer while it runs: a keystroke, click, shortcut or window-close
request made at the console that lands in a test window.
`Test::ArmConsoleShield` installs one application event filter that vetoes
and counts that input, while the harness's own input and every close or
quit the product or the scenario causes pass. It answers Run 2 of
`2026/10/17/start-qr-box-colors-from-black-and-show-controls-only-where-savable`,
where a Cmd+W typed at the console (`Shortcut event caught: Ctrl+W`,
`input_during_run: true`) closed a primary window mid-run and made the other
account active. Packed campaigns with two primary windows are the most
exposed; see the [failure row](../diagnostics.md).

It is opt-in, and arming it is the switch. A campaign whose subject is real
OS input - a Computer Use driver, native event injection, a
`test-run --activate` campaign about genuine input - never arms it; every
armed run logs a `CONSOLE_SHIELD: armed` row, so the absence is visible. It
does not replace `--wait-idle`, the background launch, `KeepWindowExposed`
or `ForceWindowActive`, and it never activates, raises or focuses anything.

## Preconditions and limits

A `-testagent` run: arming is a no-op unless `Test::Active()`. One process
has one runner and one shield; there is no multi-owner API.

**Harness input.** Input delivered while a `Test::HarnessInputScope` is open,
at the event-loop level that scope was opened at, is harness input and
always passes. `Test::Settle` opens one around its action and closes it
before its drain, so every input helper passes - `Click`,
`ClickWithPressOrigin`, `TypeText`, `CommitText`, `Drag`,
`AppendQueuedStartDrag`, `Wheel`, `DropText`, `PressKey`, `OpenContextMenu`,
`OpenChatListContextMenu` - including the `QShortcutEvent` a sent key
produces when it matches a shortcut, a sent input-method commit and
`DropText`'s drag pair. An overlay's own delivery passes only inside
`Settle`, a spontaneous `QWindowSystemInterface` delivery included: the
scope, not the spontaneous flag, decides. The same sent key or raw platform
call outside `Settle` is console input to the shield. Call
`ForceWindowActive` outside `Settle`: it flushes queued platform events,
which inside the scope would pass.

**The veto rule.** Outside the scope an event of a class below is vetoed
when it is spontaneous. Shortcut, input-method and drop events are vetoed
whether spontaneous or not, because their platform routes are sent events:
`QShortcutMap::dispatchEvent` sends every `QShortcutEvent`, macOS `QNSView`
sends IME text, and `QGuiApplicationPrivate::processDrag` and `processDrop`
send platform drags.

| Class | Qt event types |
| --- | --- |
| `key` | `KeyPress`, `KeyRelease` |
| `shortcut-override` | `ShortcutOverride` (its own class, so `key` counts presses and releases on every platform; off macOS Qt adds an override to each platform key press) |
| `shortcut` | `Shortcut` |
| `input-method` | `InputMethod` |
| `mouse-button` | `MouseButtonPress`, `MouseButtonRelease`, `MouseButtonDblClick` and their `NonClientArea` forms |
| `pointer` | `MouseMove`, `NonClientAreaMouseMove`, `Enter`, `Leave`, `TabletPress`, `TabletMove`, `TabletRelease`, `TabletEnterProximity`, `TabletLeaveProximity` |
| `wheel` | `Wheel` |
| `touch` | `TouchBegin`, `TouchUpdate`, `TouchEnd`, `TouchCancel`, `NativeGesture` |
| `context-menu` | `ContextMenu` |
| `drop` | `DragEnter`, `DragMove`, `DragLeave`, `Drop` |
| `close` | `Close` whose receiver is a widget |

A vetoed event is filtered out at its first hop, normally the `QWidgetWindow`,
so no widget sees it, and it keeps its accepted state (a key left accepted
is not handed back to the AppKit responder chain). A vetoed `Close` is
`ignore()`d first. `Quit` and every unclassified type - activation, focus,
expose, paint, `FileOpen`, the hover events Qt itself sends - always pass.
Input processed in an event loop entered from inside a harness delivery
(`QDrag::exec`, `QMenu::exec`, a modal dialog) runs at a deeper loop level,
outside the scope, and is vetoed.

**Product and scenario closes pass.** In Qt 6 every close of a top-level
window reaches its `QWidgetWindow` as a spontaneous window-level `Close`,
because `QWidget::close()` too goes through `QWindow::close()`,
`QPlatformWindow::close()` and `QWindowSystemInterface::handleCloseEvent`;
that window-level `Close` always passes. `QWidgetWindow::closeEvent` then
gives the widget its own `QCloseEvent` through
`QWidgetPrivate::handleClose`: sent for a product or scenario close
(`QWidget::close()`, `QWindow::close()`, `Ui::RpWindow::close()`, quit's
`closeAllWindows`, lib_ui's macOS `WindowHelper::Private::close`) and
spontaneous for a platform request (a title-bar button, the window
manager). Only that spontaneous widget `Close` is vetoed; being ignored,
`handleClose` returns false, the widget stays shown and `QWindow::event`
keeps its platform window. No pass-through API exists or is needed. A
console Cmd+W in tdesktop is a `QShortcutEvent` (`Command::Close`), vetoed
as a shortcut.

**What it cannot stop.** These are limits of an application event filter;
no switch or hook covers them, and the shield claims no suppression of
every native action:

- **Newer application filters.** Qt runs application filters newest first.
  The shield is moved to the front when armed and again on a timer every
  `kConsoleShieldRefront` (100 ms), only at the outermost loop level and
  never from inside the filter. A product filter installed after the arm
  (search suggestions, a reaction preview, notifications, the chat switch
  filter) sees console input first until the next eligible tick. 100 ms is
  a requested timer interval, not a bound, while the loop is busy or nested.
- **A same-loop-level flush inside a harness action.** A `processEvents()`
  or `QWindowSystemInterface::flushWindowSystemEvents()` call made inside a
  `Settle` action, at the scope's loop level, delivers queued console input
  inside the scope, and it passes.
- **Native menu actions.** On macOS a key equivalent the native menu bar
  resolves runs its `QAction` with no Qt input event the filter sees
  (`QCocoaNSMenuDelegate` `menuHasKeyEquivalent:`); `Platform::GlobalMenu`'s
  Edit items then post non-spontaneous key events to the focused field,
  which pass, and its Preferences item acts directly.
- **The popup monitor.** On macOS `QCocoaWindow::setupPopupMonitor` closes
  open popups on a click outside the app by calling `closeAllPopups()`
  directly.
- **OS activation and quit.** Activating the app or a window is not an event
  the filter can refuse. A console quit (Cmd+Q, the Dock menu) arrives as
  the same spontaneous `QEvent::Quit` as the scenario's own quit, and Quit
  always passes.
- **`FileOpen` and IPC.** A link or file opened from the OS
  (`QEvent::FileOpen`) and a second instance's command are not input
  classes and pass.
- **Closes of non-widget windows.** Only a widget's own `Close` is
  classified, so a platform close request to a `QWindow` that belongs to no
  widget passes. On Windows lib_ui's custom frame turns `WM_CLOSE` into a
  product `close()` (`WindowShadow::windowCallback`), which passes like any
  product close.

## Minimal use

An overlay sketch against the real API; no translation unit compiles it.
Arm after launch, release in the first teardown stage:

```cpp
void SetupScenario(not_null<Runner*> runner) {
	runner->waitEvent(u"launch_finished"_q);
	runner->add({
		.name = u"arm the console shield"_q,
		.run = [=] { ArmConsoleShield(runner, u"qr-colors"_q); },
	});

	// ... the campaign's stages, driven by the input helpers; an
	// overlay's own raw delivery goes inside Settle:
	//     Settle([&] { QApplication::sendEvent(target, &event); });

	runner->add({
		.name = u"teardown: release the console shield"_q,
		.run = [] { ReleaseConsoleShield(u"teardown"_q); },
	});
	// ... teardown stages that close windows or switch accounts ...
}
```

The label is one token, so the rows stay space-separated. Judge nothing by
the shield's own rows; they record what the console sent. The campaign's
checks stay its own.

## Readings and refusals

`ReadConsoleShield()` returns `ConsoleShieldReading`: `label`, `armedAt`
(`crl::now()` at the arm), `vetoes` (the arm's total), `counts` (one per
class, `count(ConsoleInputClass)`) and `armed`. It describes the current
arm, or after its release the released arm with `armed=false`, until the
next arm resets it. Every veto is counted, whatever the log rate.
`ConsoleShieldSummary(reading)` prints `label=<label> armed=<0|1>
vetoes=<n> counts=[key=<n>,shortcut-override=<n>,shortcut=<n>,
input-method=<n>,mouse-button=<n>,pointer=<n>,wheel=<n>,touch=<n>,
context-menu=<n>,drop=<n>,close=<n>]`.

The rows, each a `NOTE:`:

| Row | When |
| --- | --- |
| `CONSOLE_SHIELD: armed label=<label> refrontMs=100` | A fresh arm. |
| `CONSOLE_SHIELD: already armed label=<label> requested=<label> moved=<0\|1> vetoes=<n>` | Arming while armed: only a re-front (`moved=0` inside a nested loop); label, counts and the finish registration stay. |
| `CONSOLE_SHIELD: vetoed class=<class> n=<class count> total=<arm count> receiver=<typeid name> label=<label>` | A veto; a shortcut adds ` sequence=<QKeySequence::PortableText>`, for example `sequence=Ctrl+W`. `pointer`, `wheel` and `touch` rows are written at `n=1` and every 100th; every other class at each veto. |
| `CONSOLE_SHIELD: released by=<by> armedMs=<ms> <summary>` | The release, with the per-class summary - the per-scenario count. |
| `CONSOLE_SHIELD: release skipped by=<by> - not armed` | A release with nothing armed. |

No row carries a key code, key text, IME string, MIME data or position: the
filter reads only an event's type, its spontaneous flag, the receiver's
type and, for a shortcut, the shortcut's sequence. There are no refusals:
arming outside a `-testagent` run does nothing, and
`ReleaseConsoleShield` returns false when nothing is armed. A shield row is
an observation, never a PASS.

## Lifetime and cleanup

The module owns the filter and its re-front timer; a release destroys both.
A fresh arm resets the reading, installs the filter at the front and, once
per process, registers `ReleaseConsoleShield(u"finish"_q)` with
`Runner::onFinish`, so a stage timeout, the watchdog, a skip to the end or a
teardown never reached all release it (`by=finish`) before `Complete()` and
quit. Callbacks run in registration order, so a callback registered after
the first arm sees the shield already released. A second release changes
nothing and returns false. The reading survives the release until the next
arm.

Release it explicitly in the first teardown stage, before any stage that
closes windows or switches accounts, so the teardown runs exactly as an
unshielded one and the released row's summary covers the campaign alone.
Nothing else lasts: no window, focus or activation state is touched.

## Verification

`Test::AppendConsoleShieldSelfTest(runner)`, appended last by a disposable
overlay after `runner->waitEvent(u"launch_finished"_q);`. It needs no
account, chats list, network or wallet - an empty portable root is enough -
and a desktop that can show the self-test's windows; append it last because
its final arm is left for `Runner::onFinish`. Its fixture is three
parentless `Ui::RpWidget` top-level windows, A (the armed subject), B (the
unarmed control) and C (the post-release control), each sized from
`st::boxWidth` and the default field and button, holding a
`Ui::InputField`, a `Ui::RoundButton` and a `Ctrl+Alt+Shift+F12`
`QShortcut` that no product shortcut binds, with object-filter recorders on
the window's `QWindow`, the window widget, the field's raw editor and the
button. They are shown without activation, with `Qt::WA_QuitOnClose`
cleared, stacked in the screen corner farthest from the real cursor; the
gate is the shown native `QWindow`, readiness only. Foreign input goes
through `QWindowSystemInterface` directly, outside `Settle`: a synchronous
key press and release carrying a synthetic U+0436 letter, a left press and
release at the button's centre converted with
`QHighDpi::toNativeLocalPosition` and `toNativeGlobalPosition`,
`handleShortcutEvent`, and `handleCloseEvent<SynchronousDelivery>`. A
shortcut leg makes its window Qt's active window with `ForceWindowActive`
and gives the saved focus window and widget back afterwards; nothing uses
real OS input. Every leg reads the shield and the recorders right before
and right after its delivery, after the activation's flush.

Stages, each `console shield self-test: <leg>`:

- fixture: A, B and C shown, the shield not armed (`fixture gate:`).
- unarmed control B: the four foreign deliveries reach the window and then
  their receivers - the field gets both keys and the letter, the button
  clicks, the shortcut fires, the close request closes the window and
  destroys its platform window - with the reading unchanged.
- arm, then `Click`, `TypeText`, `PressKey` Backspace and `CommitText` on A
  reach their exact targets with no veto.
- armed A: a harness `PressKey` of the shortcut fires it through a sent
  `QShortcutEvent`, and a `Settle`-wrapped `QWindowSystemInterface` key pair
  types the letter, spontaneous, with no veto; the same sent key pair
  outside `Settle` is vetoed (`shortcut=1`) and the shortcut does not fire.
  This separates provenance from spontaneity.
- armed A: an application filter installed after the arm sees a foreign key
  pair first (the control), then the self-test yields five re-front
  intervals.
- armed A: foreign key, mouse, shortcut and close are vetoed and counted
  exactly `key=2`, `mouse-button=2`, `shortcut=1`, `close=1`, each measured
  on its own; the companions Qt adds - a shortcut override, a pre-press
  pointer move - are counted in their own classes and printed; no receiver
  sees anything, the field keeps its text, the window stays shown with its
  platform window, and the newer filter sees none of the keys.
- armed A: `QWidget::close()` closes it, its widget `Close` arriving sent,
  with no veto.
- release: true, `armed=0`, the counts kept; C then receives and is closed by
  the same four foreign deliveries with the reading unchanged; a second
  release returns false and the whole reading is unchanged.
- teardown (a recovery stage): releases only the self-test's own arm if one
  is still held, then destroys A, B and C.
- finish: D, a fourth window, is armed and left armed; a finish check
  registered after that arm reads `armed=0` with D's label, and a foreign key
  pair then reaches D's field with no veto; D is destroyed there. The
  early-finish cleanup, registered before any arm, releases only the
  self-test's measured arm before deleting A, B and C, and leaves D to that
  check.

Expected: every row `TEST_RESULT: PASS`, a `CONSOLE_SHIELD: released
by=finish` row for D's arm before the finish check, and no `FAIL`. Built
with `DESKTOP_APP_DISABLE_PRIVATE_QT_API`, every stage that delivers through
`QWindowSystemInterface` or needs Qt-level activation - control B, the
harness shortcut and platform keys, the newer filter, the foreign legs on A
and C - and the finish check's key leg are named `unsupported: ...` `N/A`
rows; the arm, harness helper, `QWidget::close()`, release, second-release
and finish-arm stages still run. A failed fixture gate leaves the gated
stages `N/A` and is incomplete evidence, never a pass.

The controls catch these mutations: a spontaneous-only rule fails the
`Settle`-wrapped platform keys; treating every sent event as harness input
fires the shortcut outside `Settle`; vetoing the window-level `Close` fails
`QWidget::close()`; leaving a vetoed `Close` accepted closes A; removing the
re-front lets the newer filter see the keys; removing the finish
registration leaves D armed at the finish check. The privacy and row grammar
are judged outside the process, from the flushed `CONSOLE_SHIELD` rows of
the evidence campaign's `test_log.txt`: the allowed fields only, the
shortcut's portable sequence, and no synthetic letter.

## Related guidance

[Input helpers and `Settle`](test_widgets.md#input-helpers);
[runner and `onFinish`](test_runner.md); [teardown](../teardown.md);
[launch behavior](../launch.md); [failure diagnosis](../diagnostics.md).
