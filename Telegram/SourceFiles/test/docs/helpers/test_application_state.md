# test_application_state

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API.
Source contract: [test_application_state.h](../../test_application_state.h).

## Use when

A scenario needs Telegram Desktop to read deactivated and then active again
for product code that follows `Core::App().appDeactivatedValue()`: the
read-metrics tracker (`history_view_read_metrics_tracker.cpp`), the dialogs
and history widgets, the top controls, the calls top bar and group-call
panels, and the Windows main window's `_lastDeactivateTime`
(`main_window_win.cpp`) - on a console where OS focus cannot be moved.
`Test::InjectApplicationActive(false)` and `Test::InjectApplicationActive(true)`
set the state in one call each and read it back; `Test::ReadApplicationState()`
re-checks it after a hold.

Not for focus-routed input - `Test::ForceWindowActive`
([input helpers](test_widgets.md#input-helpers)) - nor for window activation
or marking read ([test_marking_read](test_marking_read.md)), nor for keeping
a covered window painting ([test_window_exposure](test_window_exposure.md)).

Never call `QWindowSystemInterface::handleApplicationStateChanged()` on
Windows. Its first line asserts `QPlatformIntegration::ApplicationState`,
which `QWindowsIntegration::hasCapability()` does not report, so a Debug
launch ends in
`QtFatal: ASSERT: "QGuiApplicationPrivate::platformIntegration()->hasCapability(QPlatformIntegration::ApplicationState)"`
(`qwindowsysteminterface.cpp`). That assert cost
`2026/11/11/report-for-you-preview-and-visit-reads-with-their-recommendation`
a launch (`work/test.md`, Run 4); this helper replaces the overlay call to
the private setter that campaign used afterwards.

## Preconditions and limits

- **Where.** On the main thread, inside a stage, after `Test::Start()`, so
  `Core::App()` exists.
- **Synchronous.** `QGuiApplicationPrivate::setApplicationState()`
  (`qguiapplication.cpp`) sends `QEvent::ApplicationActivate` or
  `QEvent::ApplicationDeactivate` and a `QApplicationStateChangeEvent`, and
  emits `applicationStateChanged()`, inside the call. Every direct product
  subscriber - `Core::Application::handleAppDeactivated()` and
  `handleAppActivated()` among them - has run when the helper returns. A
  reaction one of them queues runs on a later turn: the main window's
  `handleActiveChanged()`, which `MainWindow::eventFilter()`
  (`mainwindow.cpp`) queues on `ApplicationActivate`, is one. It is the
  function every application-state change lands in: the focus-derived
  changes of `QGuiApplicationPrivate::processFocusWindowEvent()` and a
  platform's own report alike.
- **Side effects inside the call.** An injected inactive closes every open
  popup menu: `QGuiApplication::event()` and `QApplication::notify()` call
  `closeAllPopups()` on `ApplicationDeactivate`. The exception is an armed
  [popup hold](test_popup_hold.md) in a Windows build: while a widget popup
  is active, `Core::Sandbox::notify` swallows that event, so the popup stays
  open and the hold counts it. The state change event, the
  `applicationStateChanged()` signal and `appDeactivatedValue()` still
  follow. The Windows key mapper
  clears its key recorder on `Qt::ApplicationInactive`
  (`qwindowskeymapper.cpp`). `QWidgetPrivate::setFocus_sys()`
  (`qwidget.cpp`) asks the OS to activate a widget's window on `setFocus()`
  only while the state reads active, so an injected active with no focus
  window lets a later product `setFocus()` call `QWindow::requestActivate()`
  (on Windows `SetForegroundWindow`), and an injected inactive suppresses
  those requests.
- **What it does not change.** The focus window, `QWidget::isActiveWindow()`,
  `MainWindow::computeIsActive()`, `Core::App().hasActiveWindow()` - so the
  online status `Api::Updates::updateOnline()` sends stays as it was - and
  `MainWindow::markingAsRead()`.
- **Platforms.** Verified on Windows only. The call is platform independent:
  on macOS it is made the same way, but the cocoa plugin reports its own
  state (see below). Built with `DESKTOP_APP_DISABLE_PRIVATE_QT_API` it
  refuses `unsupported`. Wherever the call does not take effect, the
  read-back refuses `not-read-back`.

## Minimal use

```cpp
runner->add({
	.name = u"the application reads inactive"_q,
	.run = [=] {
		state->inactive = Test::InjectApplicationActive(false);
		Test::Check(
			state->inactive.refusal.isEmpty(),
			u"the application reads inactive"_q,
			Test::ApplicationStateText(state->inactive));
	},
});

// ... the scenario's own hold ...

runner->add({
	.name = u"the application reads active again"_q,
	.run = [=] {
		state->held = Test::ReadApplicationState();
		Test::Check(
			(state->held.state == Qt::ApplicationInactive)
				&& (state->held.deactivated == std::optional<bool>(true)),
			u"the application still reads inactive at the end of the hold"_q,
			Test::ApplicationStateText(state->held));
		state->active = Test::InjectApplicationActive(true);
		Test::Check(
			state->active.refusal.isEmpty(),
			u"the application reads active again"_q,
			Test::ApplicationStateText(state->active));
	},
});
```

`state` is the overlay's shared state holding three
`Test::ApplicationStateReading`s. A hold that must tell its own injections
from the platform's subscribes to `Core::App().appDeactivatedValue()` for
its duration and logs each value it receives, as the source campaign did: a
value it did not inject is a platform change.

## Readings and refusals

`ApplicationStateReading`:

| Field | Meaning |
| --- | --- |
| `requestedActive` | The request, `true` for active and `false` for inactive; empty for a pure reading. |
| `state` | `QGuiApplication::applicationState()`, read after the call. |
| `deactivated` | The first value `Core::App().appDeactivatedValue()` gives on subscription, read after the call; empty if it gave none. |
| `focusWindowSet` | `QGuiApplication::focusWindow() != nullptr`: which case of the next section applies. |
| `refusal` | Empty, or a named refusal. |

`Test::ApplicationStateText` prints it as one row:
`application state: requested=<active|inactive|none> state=<active|inactive|hidden|suspended> appDeactivated=<1|0|none> focusWindow=<1|0>`,
followed by ` - <refusal>` when refused. The helper neither Notes nor Fails.

| Refusal | Meaning |
| --- | --- |
| `unsupported` | Built with `DESKTOP_APP_DISABLE_PRIVATE_QT_API`: there is no `QGuiApplicationPrivate::setApplicationState`, nothing was set, and the reading is the current one. |
| `not-read-back` | After the call `state` or `deactivated` is not the requested one; the text names the request and both readings. |

`refusal` is empty exactly when the requested state is in force at return. A
request for the state already in force emits nothing - Qt returns early on an
unchanged state - and succeeds. `deactivated` is derived from the same Qt
state, so it cannot disagree with `state` on its own: the signal path is
proved by a subscriber, such as the self-test's, not by the reading. A
refusal is an instrument failure - the measurement that needed the state is
undecidable - and never a PASS. `Test::ReadApplicationState()` carries no
request and no refusal: never read it as "injected".

## What returns an injected state to the platform's reading

The rule, on Windows and on every platform plugin without
`QPlatformIntegration::ApplicationState` (xcb and Wayland too):
`QGuiApplicationPrivate::processFocusWindowEvent()` sets active when the
focus window goes from none to a window, and inactive when it goes from a
window to none. A change between two windows, or to the window that already
has focus, leaves the state alone. The Windows plugin itself never reports
an application state: `QWindowsIntegration::hasCapability()` has no
`ApplicationState`, and `WM_ACTIVATEAPP` is classified in
`qtwindowsglobal.h` but handled nowhere. So, by the injected state and the
reading's `focusWindow=`:

- **Injected inactive, `focusWindow=0`**: the
  next focus change to a window returns active - `Test::ForceWindowActive()`
  and the helpers built on it (the `KeepMainWindowNotMarkingRead` restore,
  the console shield's focus restore), `Window::Controller::activate()`,
  `Ui::SeparatePanel::showAndActivate()`, a newly shown window that takes
  focus, or the owner clicking the client. `ForceWindowActive` also flushes
  queued platform events, so it delivers a pending OS focus change at once.
- **Injected inactive, `focusWindow=1`**: losing focus leaves it inactive
  and emits nothing, but the focus gain that follows returns active, so any
  focus round trip ends it - a minimize and restore, another application
  taking and returning the foreground, `Test::ClearWindowActive()` then
  `ForceWindowActive()`. `ForceWindowActive()` on the window that already
  has focus changes nothing.
- **Injected active, `focusWindow=0`**: a focus gain leaves it active, and
  the loss that follows returns inactive. On a locked console the platform
  clears an injected focus window again asynchronously between turns, so
  `ForceWindowActive()` followed by that de-activation ends an injected
  active.
- **Injected active, `focusWindow=1`**: this is the platform's own reading;
  losing focus returns inactive.

On macOS the cocoa plugin reports the state itself
(`applicationDidBecomeActive`, `applicationDidResignActive` and
`applicationShouldHandleReopen` in `qcocoaapplicationdelegate.mm`), so the
next OS activation change ends an injected state there; not verified on a
host. On every platform any other `setApplicationState` call - the helper's
opposite call included - ends it.

After a hold, re-check with `Test::ReadApplicationState()` - `state`,
`appDeactivated` and `focusWindow` - and read the flips the hold's own
subscriber logged. Inject again after anything that re-asserts focus.

## Lifetime and cleanup

Nothing is retained: no subscription, timer or `Runner::onFinish`
registration. The injected state stays until one of the changes above ends
it, the end of the process included. A scenario restores what its later
stages need in a settling stage of its own, with another injection - not
with `ClearWindowActive` or `ForceWindowActive`. Popups an injected inactive
closed stay closed. The self-test restores the state it found.

## Verification

`Test::AppendApplicationStateSelfTest(runner)`, appended after
`runner->waitEvent(u"launch_finished"_q);` in an evidence overlay's
`SetupScenario`, needs no account, chats list, window or network; the
session gate runs before it as usual. It is one stage,
`application state self-test: active, inactive and active again in one turn`.
Its `run` does everything in one turn: it reads the `CONSOLE_LOCK` row
([test_console_lock](test_console_lock.md)) and the found reading, injects an
active baseline, attaches its own `appDeactivatedValue()` subscriber,
injects inactive and active again, detaches the subscriber, and injects the
state it found. Its `then` writes two notes,
`application state self-test: CONSOLE_LOCK: ...` and
`application state self-test: found - <row>`, and eight Checks, in this
order, each printing what it was decided on in its details:

1. baseline: the application reads active before the inactive change;
2. a subscriber of `Core::App().appDeactivatedValue()` starts from false
   (`values=[0]`);
3. inactive read back: `Qt::ApplicationInactive` with `appDeactivatedValue()`
   true;
4. the subscriber received the inactive change once, as true (`values=[1]`);
5. active read back: `Qt::ApplicationActive` with `appDeactivatedValue()`
   false;
6. the subscriber received the active change once, as false (`values=[0]`);
7. the injection leaves the focus window as it found it (`focusWindow=`
   equal in the found, baseline, inactive and active readings);
8. the state the stage found is restored (an empty refusal).

Checks 1, 3 and 5 compare the reading's own fields, not only its refusal.
The verdict does not depend on the console state: the turn processes no
queued platform event, so neither OS focus nor the owner's input can
interleave. The found reading depends on the launch, not on the console
alone: a found active state makes the baseline a success that emits nothing,
a found inactive one makes it a real change, and both lead to the same eight
Checks. The `CONSOLE_LOCK` row names which console the run had.

Executed on a Windows Debug build of this module's first commit, in the task
`2026/11/11/inject-application-inactive-and-active-states-from-a-testagent-scenario`
(`work/test.md`, Attempt 1). Both runs were on a locked console
(`CONSOLE_LOCK: state=locked`) whose launch found
`state=active appDeactivated=0 focusWindow=1`:

- the self-test as committed: all eight Checks PASS, the subscriber strings
  read `[0]`, `[1]` and `[0]`, the run ends
  `SCENARIO_RESULT: PASS (failures: 0)` with exit code 0, and no Qt assert
  appears in the test log, stderr or crash report (Run 1);
- the deliberate mutation that removes the
  `QGuiApplicationPrivate::setApplicationState(requested);` line: the
  inactive injection refuses `not-read-back: requested inactive,
  QGuiApplication::applicationState() reads active and
  Core::App().appDeactivatedValue() reads 0`, Checks 3, 4 and 6 FAIL with
  the subscriber receiving nothing, every PASS left is for the active state
  its details show, and the run ends cleanly with
  `SCENARIO_RESULT: FAIL (failures: 3)` (Run 2).

On a launch that finds the state inactive the same mutation fails
Checks 1, 2, 4, 5 and 6 instead; that case and an unlocked console were
not run, and the verdict does not depend on either, for the reason above.

Built with `DESKTOP_APP_DISABLE_PRIVATE_QT_API` every injection refuses
`unsupported` and its Checks fail with that refusal; that branch is checked
by reading only. Off Windows the self-test has not been run.

## Related guidance

[Input helpers and `ForceWindowActive`](test_widgets.md#input-helpers);
[window activation self-test](test_activation.md);
[marking read](test_marking_read.md);
[window exposure](test_window_exposure.md);
[console lock](test_console_lock.md);
[launch](../launch.md);
[teardown](../teardown.md).
