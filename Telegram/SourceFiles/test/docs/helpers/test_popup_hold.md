# test_popup_hold

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API.
Source contract: [test_popup_hold.h](../../test_popup_hold.h).

## Use when

A `-testagent` scenario must keep a widget popup, such as a `Ui::PopupMenu`,
open across event-loop turns on a Windows console that may be locked: a
capture through `CapturePopupMenu`, a close stage, a reopen. On a locked
Windows console the application is deactivated, and Qt closes every shown
popup on `QEvent::ApplicationDeactivate` before the next turn. Arm the hold
before opening the popup instead of skipping the leg as N/A. A leg that reads
the popup in the turn that built it, with `OpenContextMenu` or
`ReadMenuItem` ([test_menu](test_menu.md)), needs no hold.

## Preconditions and limits

A `-testagent` run: arming is a no-op unless `Test::Active()`. Main thread
only. One process has one runner and one hold, and its callers run
serially: arming while armed changes nothing, so a nested caller's release
would end the outer hold. There is no nesting API.

**Where it acts.** `QApplication::notify` calls `closeAllPopups()` for
`QEvent::ApplicationDeactivate` in its type switch, before `notify_helper`
runs any application or object event filter (`qapplication.cpp`, Qt 6.11.2
and Qt 5.15.19 alike), so an event filter cannot stop that close.
`Core::Sandbox::notify` overrides that boundary. On the main thread it asks
`Test::KeepPopupsOnDeactivate()`, declared in `core/sandbox.h` and defined in
`test_popup_hold.cpp`, for each `ApplicationDeactivate`, and returns true
without calling `QApplication::notify` when it answers true. It answers true
only while the hold is armed, in a Windows build, with
`QApplication::activePopupWidget()` set. It counts the event and writes its
row. A normal Release build defines it as `return false`. Events off the
main thread never reach it.

**What the product still sees.** Qt sets the application state before it
sends `ApplicationDeactivate`, and then sends `QApplicationStateChangeEvent`
and emits `applicationStateChanged` whatever that event's result
(`QGuiApplicationPrivate::setApplicationState`). So
`Core::App().appDeactivatedValue()` still reads deactivated, and
`Core::Application::handleAppDeactivated()` still runs. Only the swallowed event itself is
lost: the popup close, and the filters and the receiver that would have seen
it. No tdesktop code reacts to it.

**Product closes are unchanged.** Escape, a click outside the popup (on
Windows the popup's mouse grab routes it to the popup's own window), the
owning code's `hideMenu()`, `Integration::forcePopupMenuHideRequests()` on a
passcode lock and `QEvent::OrientationChange` still close a held popup. None
of them is an `ApplicationDeactivate`. Nor does the hold keep a popup open
against a focus change: `Ui::PopupMenu::focusOutEvent` still hides a menu
whose focus chain the focus leaves.

**What it does not do.** It never shows, activates, raises or focuses a
window, and it does not keep the application active: the deactivation still
happens. Its promise covers widget popups, the ones
`QApplication::activePopupWidget()` answers; a popup that is only a
`QWindow` is not covered. A deactivation with no widget popup open passes
untouched and is not counted.

**Platforms.** The module builds everywhere. Off Windows arming writes
`windows=0` and the hold stops nothing. A locked macOS console does not
close a shown popup (`2026/10/06/add-a-post-paint-capture-sampler-to-the-harness`,
`work/test.md`, Test 2), and the other platforms rely on Qt's deactivation
close.

## Minimal use

An overlay sketch against the real API; no translation unit compiles it.
`state->menu` is a `base::unique_qptr<Ui::PopupMenu>` with
`deleteOnHide(false)`, already filled with actions:

```cpp
runner->add({
	.name = u"hold and open the menu"_q,
	.run = [=] {
		Test::ArmPopupHold(runner, u"menu-campaign"_q);
		state->menu->popup(state->at);
	},
});
Test::CapturePopupMenu(
	runner,
	u"menu_open"_q,
	[=]() -> QWidget* { return state->menu.get(); });
// ... more legs that need the menu open across turns ...
runner->add({
	.name = u"teardown: close the menu and release the hold"_q,
	.run = [=] {
		if (const auto menu = state->menu.get()) {
			menu->hideMenu(true);
		}
		state->menu = nullptr;
		Test::ReleasePopupHold(u"teardown"_q);
		Test::Note(u"menu campaign: deactivations stopped=%1"_q.arg(
			Test::ReadPopupHold().stopped));
	},
});
```

The label is one token, so the rows stay space-separated. Do not assert a
nonzero count: whether a deactivation arrives while the popup is open
depends on the console, not on the scenario.

## Readings and refusals

`ReadPopupHold()` returns `PopupHoldReading`: `label`, `stopped` and
`armed`. It describes the current arm or, after its release, the released
arm with `armed=false` and its count, until the next arm resets it.
`stopped` counts the deactivations the hold kept from closing a popup. Zero
means none arrived while a widget popup was open, not that the hold failed.
A `stopped` row says that a deactivation arrived while a widget popup was
active and was swallowed. It does not name the popup or the cause: a lock
screen, another application's window, an injected focus change and
`Test::InjectApplicationActive(false)`
([test_application_state](test_application_state.md)) read alike.

The rows, each a `NOTE:`:

| Row | When |
| --- | --- |
| `POPUP_HOLD: armed label=<label> windows=<0\|1>` | A fresh arm. |
| `POPUP_HOLD: already armed label=<label> requested=<label> stopped=<n>` | Arming while armed: nothing changes. |
| `POPUP_HOLD: stopped n=<n> label=<label>` | Each deactivation the hold swallowed. |
| `POPUP_HOLD: released by=<by> label=<label> stopped=<n>` | The release, with the arm's count. |
| `POPUP_HOLD: release skipped by=<by> - not armed` | A release with nothing armed. |

There are no refusals: arming outside a `-testagent` run does nothing and
writes no row, and `ReleasePopupHold` returns false when nothing is armed.
A hold row is an observation, never a PASS; the scenario's own checks
decide.

## Lifetime and cleanup

The module owns its reading and one finish registration, and nothing else:
no filter, timer or window. A fresh arm resets the reading and, once per
process, registers `ReleasePopupHold(u"finish"_q)` with `Runner::onFinish`,
so a stage timeout, the watchdog, a skip to the end or a teardown never
reached all release it (`by=finish`) before `Complete()`. Callbacks run in
registration order, so a callback registered after the first arm sees the
hold already released. A second release changes nothing and returns false.

Release after the last leg that needs the popup open across turns. From
then on a deactivation closes popups exactly as before. The release closes
nothing: a popup still open at the release stays open until something closes
it, and while open it keeps Qt's popup mouse and keyboard grab. Close the
scenario's popup in the same teardown.

## Verification

The module has no self-test of its own. A caller that holds popups names its
standing check in its own reference. Its focused verification is the
disposable overlay probe of
`2026/11/12/keep-a-scenarios-popup-open-on-a-locked-windows-console`,
designed for an unlocked and a locked Windows console in a Debug build with
the private Qt API. This page describes that design; the task's
`work/test.md` records which legs ran, on which console, and what they
read. The probe's fixture is one three-action `Ui::PopupMenu` with
`deleteOnHide(false)`, parented to the primary window.

- Same-turn controls, each inside one stage `run`: focus the menu, then
  deliver focus `nullptr` with
  `QWindowSystemInterface::handleFocusWindowChanged<SynchronousDelivery>`,
  then give the primary window its focus back. A synchronous delivery, not
  `ForceWindowActive` and `ClearWindowActive`, because their flush can
  deliver an older queued deactivation first. Each leg first requires an
  active application and the ready menu as `activePopupWidget()`. Unarmed,
  the menu closes in that call. Armed, it stays ready and active, `stopped`
  grows by exactly one, the product's `appDeactivatedValue()` reads
  deactivated, and the menu is still open on the next turn. Released, it
  closes again and the count stays.
- Product closes while armed: Escape through `Test::PressKey`, a synchronous
  QPA left press and release on the popup's own window at a point outside
  its rect inside `Settle`, and the animated `hideMenu()`. Each starts on the
  ready menu with the hold armed, and must hide it within 3 s.
- Finish: a hold left armed reads `armed=false` in an `onFinish` callback
  registered after the helper's, and its released row comes before
  `TEST_COMPLETE`.

They catch these mutations: no seam or a hold that never answers keeps the
armed control from staying open; a hold that answers while not armed keeps
the unarmed control open; a release that leaves the hold armed keeps the
released control open; a dropped finish registration leaves the finish
reading armed. Off Windows the hold is checked by source review only.

## Related guidance

[Popup captures and menu readings](test_menu.md);
[console lock reading](test_console_lock.md);
[console input shield](test_console_shield.md);
[runner and `onFinish`](test_runner.md); [input helpers](test_widgets.md#input-helpers);
[failure diagnosis](../diagnostics.md).
