# test_probe

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_probe.h](../../test_probe.h).

## Use when

Windowed observations and correlated requests. Read the contract below and the source header before use.

## Contract and usage

Append-only observation records read only through a declared window, each
carrying the time it was recorded; keyed issue/answer rows correlated into one
round trip by key rather than by list position, refusing every reading it
cannot positively pair; and scans that must match a control before a zero
counts as absence. `record(row)` logs `NOTE: <name>: <row>` and `recordIssue`
/ `recordAnswer` log `NOTE: <name>: <row> role=issue|answer key=<key>`: caller
text only, holding no helper number. Every check's details are `probe=<name>
window=[<from>,<till>) rows=<rows>` (`ProbeWindowText`: the window's rows
joined by `; `, or `<none>`); `checkCountSince`'s are `expected=<n> actual=<n>
<window>` (`ProbeCountText`); and `checkRoundTripSince` prints the round
trip's observation, `key=<key> state=<state> issues=<n> answers=<n>
dropped=<n> preIssues=<n> preAnswers=<n> issueAtMs=<ms> answerAtMs=<ms>
roundTripMs=<ms> discarded=<rows> <window>` (`RoundTripText`, which
`roundTripSince` builds into `RoundTrip::observation`).
`DiscriminatingScan::report()` writes `NOTE: <name>: examined=<n>
subject(<subjectWhat>)=<n> control(<controlWhat>)=<n>` (`tallyText`), then the
caller's details as `NOTE: <name> subjects: ...` and `NOTE: <name> controls:
...`, then the check `<name> discriminates`, whose FAIL details are `the walk
matched no <controlWhat>, so its <subjectWhat> count of <n> over <n> examined
items cannot tell absence from an enumeration that never reaches the subject`
(`refusalText`). The numbers the helper itself counts or times there - window
bounds, counts, tallies and times - are ordinary integers
(`window=[4821,5937)`, `expected=6148 actual=6148`, `issues=41 ...
roundTripMs=48`, `examined=4821`), and `TelemetryNumber`s in a text formatted
after `Test::RequestTelemetryNumbers()` (`window=[4821p,5937p)`,
`expected=6148p actual=6148p`, `issues=41p ... roundTripMs=48p`,
`examined=4821p`); a judge reads a value of either format with
`float(v.replace('p', '.'))`. `ProbeWindowText`, `ProbeCountText`,
`RoundTripText`, `tallyText` and `refusalText` are pure and take an explicit
`NumberFormat`. `checkCountSince`'s `expected` is the caller's number and
prints in the requested format too, so it must never be a fixture secret or a
number read from one. Caller text - a row, a key, a probe or scan name, a
subject or control name, a detail - prints exactly as given, and a
`timedRowsSince` time or a `RoundTrip` field that a caller prints itself is
the caller's number to format.
