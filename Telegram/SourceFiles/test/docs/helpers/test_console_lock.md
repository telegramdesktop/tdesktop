# test_console_lock

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_console_lock.h](../../test_console_lock.h).

## Use when

Current console-lock classification. Read the contract below and the source header before use.

## Contract and usage

Whether the console session this process runs in is locked, read at the moment
of the call and never cached, on Windows and macOS: `Test::ReadConsoleLock()`.
On Windows it reads the `SessionFlags` of
`WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, <own session>,
WTSSessionInfoEx)`, the own session taken from `ProcessIdToSessionId` - the
reading Qt's own clipboard takes before it stops retrying `OleSetClipboard`
(`QWindowsContext::isSessionLocked()`, which reads the active console session
instead). `WTS_SESSIONSTATE_LOCK` reads `locked` and `WTS_SESSIONSTATE_UNLOCK`
`unlocked`; any other value, a failed call, a short answer, a level other than
1, and Windows 7 / Server 2008 R2, which report the two flags reversed, read
`unknown`. On macOS it reads a fresh `CGSessionCopyCurrentDictionary()` per
call, released before it returns, and two booleans in it: the undocumented
`CGSSessionScreenIsLocked` - true while the session's lock screen is up,
measured absent on an unlocked console on macOS 27 - and
`kCGSessionOnConsoleKey` (the key `kCGSSessionOnConsoleKey`). In this order: a
null dictionary (no window-server session) reads `unknown`; a
`CGSSessionScreenIsLocked` that is true reads `locked`, and one that is not a
boolean `unknown`; `kCGSessionOnConsoleKey` true, with the lock key absent or
false, reads `unlocked`; anything else - a session off the console (fast user
switching), whose lock bit does not say whether its screen is shown - reads
`unknown`. So it also sees a console locked before the launch; `test-run`'s
`screen_locked` reads the same two keys out of process through `ioreg`'s
`IOConsoleUsers`, the independent control. Elsewhere it reads `not-applicable`
with source `none`, and `locked` is reachable only in the Windows and macOS
branches. `ConsoleLockText` prints the one `CONSOLE_LOCK:
state=<locked|unlocked|unknown|not-applicable> source=<call> <raw> - <reason>`
row, naming that source
(`CGSessionCopyCurrentDictionary().CGSSessionScreenIsLocked` on macOS) and its
raw values - on Windows session, console session, bytes, level, reported
session, session state, flags in hex; on macOS
`kCGSSessionOnConsoleKey=<1|0|absent|not-boolean>
CGSSessionScreenIsLocked=<1|0|absent|not-boolean>`, none for a null dictionary
- never the user, domain or station name the Windows answer carries, nor the
user's short and long names, UID, GID, session UUID or audit and security
session ids the macOS dictionary carries. `ConsoleLockGate` answers `locked
console: <row>` for a `locked` reading in a Windows build only
(`Platform::IsWindows() && reading.locked()`) and an empty string for every
other reading and on every other platform, so it can be (part of) a stage's
`skipReason`; `unknown` and `not-applicable` never gate. So the gates keyed on
it stay Windows-only - `AppendClipboardRoundTripSelfTest`'s live arm,
`AppendPopupMenuCaptureSelfTest`'s across-turn stages and every campaign leg
gated the same way - because those are the legs a locked Windows console
breaks: no process on its desktop can open the clipboard, and Qt closes a
shown popup on `ApplicationDeactivate`. A locked macOS console did not close
the popup: a packed launch with `CGSSessionScreenIsLocked` true ran
`AppendPopupMenuCaptureSelfTest` with 0 FAIL
(`2026/10/06/add-a-post-paint-capture-sampler-to-the-harness`, `work/test.md`,
Test 2). On macOS the reading is printed in those places and gates nothing; a
check that a locked macOS console does break gates on `locked()` itself, as
`AppendKeepWindowExposedSelfTest` does. Not `Core::App().screenIsLocked()`,
which is not consulted on any platform: a product flag set only by
notifications that arrive after the launch (`WM_WTSSESSION_CHANGE`, the
`com.apple.screenIsLocked` / `com.apple.screenIsUnlocked` observers, the
xdg-desktop-portal Inhibit monitor's `screensaver-active`), so it reads false
on a console already locked at launch; not the input desktop's name
(`OpenInputDesktop` named it `Default` on a locked Windows 11 console); not an
`OpenClipboard` attempt (the measurement a clipboard check makes). Read by
`AppendClipboardRoundTrip`'s `read-*` refusal, by the gates of
`AppendClipboardRoundTripSelfTest`'s live arm and
`AppendPopupMenuCaptureSelfTest`'s across-turn stages, by
`AppendKeepWindowExposedSelfTest`'s lock `N/A` and its control's
`screenLocked=`, and into the printed `screenLocked` of
`ReadMainWindowMarking` and of the animation-clock window reading
(`test_animation_clock.h`); a campaign gates its own clipboard or
popup-capture legs on it the same way (its judging stage's `skipReason`,
`CapturePopupMenu`'s `skipReason`), Windows-only through the same gate. It has
no self-test of its own: on Windows those two self-tests' gated and ungated
rows, and on macOS the window-exposure lock `N/A` and the marking-read and
animation-clock `screenLocked=` rows, each beside an independent reading of
the console, confirm it.
