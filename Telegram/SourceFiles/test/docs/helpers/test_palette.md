# test_palette

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_palette.h](../../test_palette.h).

## Use when

Switch and restore palettes while respecting system following. Read the contract below and the source header before use.

## Contract and usage

A switch to the day or the night palette, and its restore, that work while the
theme follows the system dark mode, where a bare
`Window::Theme::ToggleNightMode()` is silently refused (Failure diagnosis).
`AppendPaletteSwitch(runner, { name, mode, skipReason, sampleLabel, sample,
stableFor, bound })` appends one stage, `palette switch: <name>`, and returns
the `PaletteSwitch` reading it fills; `AppendPaletteRestore(runner, switched,
name, skipReason)` appends `palette restore: <name>` for that switch and
returns a `PaletteRestore`; both read everything when their stage runs, never
when it is appended. The switch takes the route of
`Window::Theme::ToggleNightModeWithConfirmation()`'s confirmation without its
box: only when `ChatBackground::nightModeChangeAllowed()` reads false,
`Core::Settings::setSystemDarkModeEnabled(false)` and `saveSettingsDelayed()`;
then `ToggleNightMode()` and `KeepApplied()`. The platform's reported mode is
never synthesized and the OS appearance is never changed. A request for the
palette already committed touches nothing and succeeds with `needed=0
applied=0`. After its action each stage samples its probe on every runner tick
until the sample has not changed for `stableFor` (`kPaletteStableFor`, 400
ms), or refuses `not-settled` once `bound` (`kPaletteSettleBound`, 4000 ms)
has passed; the probe is `st::windowBg->c` by default (`windowBg`: day
`#ffffff`, night `#17212b`) or the caller's, and the restore reads the
switch's own probe. `applied` needs the requested mode committed and a settled
sample that differs from the sample before, never `IsNightMode()` alone. Each
reading carries the modes before and after (`PaletteModes`: night,
system-follow, the reported system mode or none, `nightModeChangeAllowed()`,
started, editing; `PaletteModesText`), both samples, the waited time, the
sample count and a named refusal, and is Noted as one `PALETTE_SWITCH:` or
`PALETTE_RESTORE:` row (`PaletteSwitchText`, `PaletteRestoreText`); the
helpers never Pass or Fail. The refusals (`PaletteRefusalName`) are
`invalid-arguments`, `not-started`, `theme-editing`, `already-switched`
(another switch's restore is still outstanding; nothing is touched),
`invalid-sample`, `route-refused`, `not-settled` and `sample-unchanged` (the
probe cannot tell the two palettes apart, so `applied` is not claimed), and
for the restore also `nothing-captured`, `system-mode-changed`, `modes-differ`
and `sample-differs`. The restore puts the night mode back first, with
following paused if it prevents the change, then the follow flag - re-enabling
following runs `Core::Application::checkSystemDarkMode()` at once, and the
reading is taken after it - then `Core::App().saveSettings()`; a later restore
of the same switch only verifies. A switch's first change registers a
restoration obligation, and one module-owned `Runner::onFinish` restores every
outstanding one, newest first, with the same routine and Notes one
`PALETTE_CLEANUP:` row; it cannot wait for the palette to settle, so it is a
backstop for an already failed run, and the normal restore belongs in a stage
(the `d->allThreads.isEmpty()` row under Failure diagnosis). It is no
byte-for-byte rollback of the portable data: `KeepApplied()` writes the
settings when the mode changed and, on a first switch, the other mode's theme
slot. Its own self-test, `AppendPaletteSwitchSelfTest`, needs no session,
network or chat and turns following on when it was off at entry: the premise
(following on, a reported system mode, `nightModeChangeAllowed()` false, and a
bare `ToggleNightMode()` leaving `windowBg` and the night mode unchanged at
once and over a settled interval; a host that reports no system mode is a
named FAIL, never a pass), the switch to the other palette at its shipped
`windowBg`, the restore to the switch's starting values and a second restore
that only verifies, a no-op request, a constant probe refused
`sample-unchanged` while a live `windowBg` shows the palette switched, a
second switch refused `already-switched` while an unfinished one is
outstanding, the finish cleanup restoring that unfinished switch and leaving
the restored ones alone, a second cleanup that changes nothing, and the
palette, the night mode and following left as it found them. Confirm with
`AppendPaletteSwitchSelfTest`.

## Related guidance

[Capture selection](test_capture.md); [teardown](../teardown.md);
[failure diagnosis](../diagnostics.md).
