# test_window_exposure

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_window_exposure.h](../../test_window_exposure.h).

## Use when

Keep covered windows painting without activation. Read the contract below and the source header before use.

## Contract and usage

Keep a top-level window that another application covers exposed, without
activating anything. `ReadWindowExposure(widget)` is the pure reading of
`widget->window()`'s `QWindow`, legal in an `until`: the target's identity and
frame, `exposed` (`isExposed()`), `active` (`isActive()`), `stayOnTop`
(`Qt::WindowStaysOnTopHint` in its flags), `applied`, the application state,
the focus window, and a refusal. `KeepWindowExposed(widget)` returns that
reading taken before its call - so `exposed`, `active` and `stayOnTop` are the
values before it - and on macOS then sets `Qt::WindowStaysOnTopHint` on the
`QWindow` (`NSModalPanelWindowLevel`; no ordering, key-window or activation
call, the same call `Calls::Window::setPinnedOnTop` makes), with `applied=1`
only when this call set the hint, so a window that already carries it gets
nothing applied. It never waits: call it, wait for `isExposed()` in a pure
`until`, then ask for the repaint you need - a re-exposed window with nothing
dirty is only flushed, not painted. Named refusals, returned and never logged,
in order: `null-widget`, `minimized` (stacking cannot expose a minimized
window, so the helper and `KeepMainWindowNotMarkingRead` exclude each other)
and `hidden` (no `QWindow` handle - never retargeted at another window -,
`isHidden()`, or a `QWindow` that is not visible, such as under
`Qt::WA_DontShowOnScreen`). `RestoreWindowExposure(reading)` clears the hint
only when that reading applied it, so it leaves a product's own hint alone; it
is idempotent and safe after the window is gone. `WindowExposureText` prints
one `window exposure: target=... exposed= active= stayOnTop= applied=
stacking= appState= focusWindow=` row, with ` - <refusal>` when refused. Off
macOS the helper is a documented no-op that still returns its reading
(`applied=0 stacking=none`): Windows and X11 never turn a covered window
unexposed and the hint would restyle or recreate the native frame there, and
Wayland gives clients no stacking control. Exposing the primary window can
make it mark messages read (see the failure row). Own self-test,
`AppendKeepWindowExposedSelfTest`, needing no session, network or account,
over two legs - an ordinary parentless top-level window and a
`Ui::SeparatePanel` with the hint cleared before show - each shown without
activation (`Qt::WA_ShowWithoutActivating` before the native window exists,
never `raise()` or `showAndActivate()`) under a harness-owned opaque titled
cover: showing them changes neither the focus window nor the application
state; a settle stage puts the cover up only once the target has been shown
for 2000 ms, because a freshly shown window gets one more layer display about
half a second after it is shown and Qt counts any layer display as an expose,
so a target covered sooner reads `isExposed()` true while the window server
still reports it occluded; the control reads the target unexposed under the
cover within 3000 ms or writes a named `N/A`, never a pass, and the leg's
deciding stages are `N/A` with it; the premise shows the covered target, which
nothing asks to repaint, staying unexposed with no paint for 500 ms; the
helper exposes it within 3000 ms with no repaint requested, `isActive()`, the
focus window and the application state unchanged, and the widget still shown
with its own flags unchanged; only then does the self-test ask for one
`update()`, which must paint within 3000 ms; the undo - a second call applies
nothing and its undo leaves the hint set, the first reading's undo clears it
and activates nothing; the teardown destroys the target and the cover
(`before=2 after=0`), and `Runner::onFinish` releases them on every path; and
a refusal stage that returns `null-widget`, `hidden` and `minimized` without a
logged failure. Off macOS, or on a console `Test::ReadConsoleLock()` reads
locked - on macOS the session's `CGSSessionScreenIsLocked`, so a console
locked before the launch too, where a window shown behind the lock screen
keeps a stale `isExposed()` - every leg stage is a named `N/A`; the lock's
`N/A` ends with the `CONSOLE_LOCK` row it read (`the console is locked, so
occlusion and exposure cannot be decided: CONSOLE_LOCK: state=locked ...`),
and the control's `screenLocked=` is the same reading. After a default
background `test-run` launch a target that another application's window covers
from the moment it is shown cannot be turned unexposed by the harness cover
(see the exposure failure row), so that leg is the named `N/A`, never a pass.
