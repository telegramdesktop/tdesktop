# test_clipboard

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_clipboard.h](../../test_clipboard.h).

## Use when

Clipboard round-trip and classified read-back. Read the contract below and the source header before use.

## Contract and usage

The retried clipboard round trip: `AppendClipboardRoundTrip(runner, { label,
accessor, delay, attempts })` appends one stage, `clipboard round trip:
<label>`, that writes a sentinel `harness-clipboard:<label>:<serial>:a<k>` and
reads it back, up to `kClipboardRoundTripAttempts` (10) times
`kClipboardRoundTripDelay` (300 ms) apart: attempt 1 in the stage's `run`,
every later one from a `Qt::PreciseTimer` `base::Timer` across event-loop
turns, never a main-thread sleep, and a pure `until` on `finished`. Each
attempt is one `CLIPBOARD_ROUND_TRIP_ATTEMPT: label=<label> ...` row
(`read=sentinel`, `empty`, `stale-sentinel:<held>` or `foreign`, then `owns`,
`yielded`, `atMs`, `writeMs`), so keep the label one token without spaces, and
the stage's `then` Notes the `CLIPBOARD_ROUND_TRIP` summary. The returned
`ClipboardRoundTrip`, read from a later stage, carries `succeededAt` or a
refusal naming what the last read held - `read-empty`, `read-stale-sentinel`,
`read-foreign` - or `invalid-bound` (fewer than one attempt or a negative
delay; nothing written). A `read-*` refusal's reason also ends with the
console-lock reading taken at refusal, `; console at refusal: CONSOLE_LOCK:
...` (`Test::ReadConsoleLock()`, `test_console_lock.h`) - the host console's
state, printed for every accessor, fakes included - so a campaign's own
refusal names a locked Windows console with no wiring; a success and
`invalid-bound` carry none. A read is only classified, inside a scope its text
never leaves: every sentinel is registered process-wide before it is written
and only those are ever printed, foreign text is never stored, printed,
measured or hashed, a sentinel from an earlier process counts as foreign, and
the owner's clipboard is overwritten and never saved or restored.
`ReadClipboard(expected, accessor)` is a one-shot reading for a product copy,
deliberately not a wait: `expected`, `empty`, `harness-sentinel` (naming it)
or `foreign`, with `owns` (`ownsClipboard()`, meaningful on Windows, macOS and
X11) and the platform name; `ClipboardReadingText` prints neither the expected
nor the read text. On Windows a read while the process owns the clipboard is
served in process, so a success there proves the set took, not an OS-level
read. `ClipboardAccessor` (`write`, `read`, optional `owns`) is the injection
seam, and `SystemClipboardAccessor()` the default. Its own self-test,
`AppendClipboardRoundTripSelfTest`, counts rows only from a mark it takes in
the test log: an accessor failing three times reads back at attempt 4 with
exactly 4 rows; an always-empty one with the shipped defaults is refused
`read-empty` after exactly 10 logged attempts; a stale sentinel is refused
`read-stale-sentinel` naming it, and an injected foreign text `read-foreign`;
`ReadClipboard` classifies the same fakes; the always-empty refusal carries
the console reading and the three-failure success none; one live round trip
runs against the system clipboard, and is `TEST_RESULT: N/A` by name - the
lock, the probe row, the refusal and its attempts - instead of failing when
`Test::ReadConsoleLock()` reads a locked Windows console as its judging stage
begins and it did not read back, while a judging stage that runs Notes the
`CONSOLE_LOCK` row its gate read; and a `Test::DiscriminatingScan` finds the
foreign text in no line the self-test appended, with the foreign arm's own
attempt rows as its control.
