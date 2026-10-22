# test_marking_read

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_marking_read.h](../../test_marking_read.h).

## Use when

Prevent primary-window read marking. Read the contract below and the source header before use.

## Contract and usage

Keep the primary window from marking messages read on an unlocked console.
`ReadMainWindowMarking` / `MarkingReadDetails` carry `markingAsRead`,
`isActive`, `isMinimized`, `isHidden`, `exposed` and `screenLocked` -
`Test::ReadConsoleLock().locked()` (`test_console_lock.h`), read at each call
on every platform, so a console locked before the launch reads `1` on Windows
and macOS and every other platform reads `0` - plus the `WindowActivation`
reading, and a refusal. `KeepMainWindowNotMarkingRead` sets
`Qt::WindowMinimized` (not `Controller::minimize()`, which tray-hides in
`WorkMode::TrayOnly`, and not `QWidget::hide()`, which makes `CaptureWidget`
refuse the window) and refuses by name if that lever is not holding within
`kNotMarkingReadBound`. A null controller refuses without searching for
another window. The self-test covers a true control, the helper,
`Controller::activate()` undoing the lever, a second call, a non-blank
`PreparedWidgetCapture` of a main-window widget while the lever holds, the
null refusal, and showing the window again. From its control until its restore
stage it keeps the primary window exposed with `Test::KeepWindowExposed`, so a
window another application covers - the usual state after a default `test-run`
launch - still reads exposed, and its `Controller::activate()` stages request
activation of the client on macOS, which the window server may grant or refuse
after a background `test-run` launch; the self-test passes either way
(`2026/10/08/launch-the-testagent-client-without-activating-it`, Runs 4, 8 and
9). The lock never ends the control's wait on its own: the control waits for
`markingAsRead` true, and only a control still false at `kNotMarkingReadBound`
on an unexposed window or on a console `Test::ReadConsoleLock()` reads locked
ends it, which makes the deciding half a named `N/A` whose reason names the
lock first (`the console is locked`) - never a pass; an exposed window on an
unlocked console that never reads it true runs to the stage timeout. A locked
console whose control reads true runs and decides the deciding half: two
packed launches on a locked macOS console read 8 PASS
(`2026/10/04/add-a-local-restored-notify-settings-override-to-the-harness` and
`2026/10/06/add-a-post-paint-capture-sampler-to-the-harness`, `work/test.md`).
