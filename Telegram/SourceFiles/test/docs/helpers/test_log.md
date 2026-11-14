# test_log

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_log.h](../../test_log.h).

## Use when

Evidence logs, assertions and telemetry formatting. Read the contract below and the source header before use.

## Contract and usage

### `test_log.h`

Absolute flushed logs, steps, notes, checks whose `details` are printed on the
passing verdict as well as the failing one, tolerances, geometry, completion
markers, N/A rows for stages that did not apply, and their count. One `LogRaw`
call always writes exactly one physical line, whatever it is handed: every
character Python's `str.splitlines()` breaks on - U+000A, U+000B, U+000C,
U+000D, U+001C, U+001D, U+001E, U+0085, U+2028, U+2029, and so a CRLF pair as
its two code points - is written as a visible `\uXXXX` escape, so a record
carrying a break stays one row the external readers' line grammar reads whole
and cannot mistake for a completion, while text with no separator is passed
through byte for byte and the escape adds no trailing whitespace.
`LogGeometry(name, rect)` writes `GEOMETRY: <name>: x=<x> y=<y> w=<w> h=<h>` -
so do `CaptureWidget`, `CaptureRect`, `CaptureViaWindow`,
`CaptureToastSubtree` and a saved `PreparedWidgetCapture` - and
`CheckNear(actual, expected, tolerance, what)` writes `TEST_RESULT: PASS:
<what> (actual <a>, expected <e> ±<t>)`, or the same `FAIL` row with ` - out
of tolerance` appended. Their numbers are ordinary integers (`x=-4821 y=3764
w=5937 h=6148`, `actual 4826, expected 4821 ±37`), and `TelemetryNumber`s in a
row formatted after `RequestTelemetryNumbers()` (`x=-4821p y=3764p w=5937p
h=6148p`, `actual 4826p, expected 4821p ±37p`); a judge reads a value of
either format with `float(v.replace('p', '.'))`. `GeometryText` (the whole
`GEOMETRY:` row) and `CheckNearText` (the check text without its verdict
prefix) are the pure formatters, taking an explicit `NumberFormat`.
`CheckNear`'s `actual`, `expected` and `tolerance` are the caller's numbers
and print in the requested format too, which hides digits from the secrecy
scan, so they must never be a fixture secret or a number read from one.

### `test_log.h` (`TelemetryNumber`, `RequestTelemetryNumbers`)

The number format for telemetry printed into a scanned log, from
`2026/10/08/give-overlay-telemetry-a-number-format-the-secrecy-scan-cannot-count`,
and the one request that makes the shared helpers print their own numbers in
it. `TelemetryNumber(value, decimals)` prints `-` for a value below zero, the
integer digits, `p` and exactly `decimals` fraction digits (`decimals`
defaults to 0, and a negative one counts as 0), rounding as
`QString::number(value, 'f', decimals)` does, and `nan`, `inf` or `-inf` for a
non-finite value: `42p`, `16p31`, `-4821p75`. Every digit run touches the `p`,
so under the scan's bounded rule no digit run of a formatted number is ever
counted. The grammar is an optional `-`, digits, `p` and optional digits, or
`nan`, `inf` or `-inf`; a judge script reads a value with
`float(v.replace('p', '.'))`, and a judge that matches a value with a regular
expression takes the `p` and the fraction digits too (`-?\d+(p\d*)?`), since
`-?\d+` alone silently stops at the `p`. Format only numbers an overlay, or a
converted helper under the request, measured or computed, never a fixture
secret or a number read from one, and choose `decimals` per quantity, never
from a secret. A `TelemetryNumber` is a word to the phrase matcher - it ends a
word run where a bare number is neutral - so never print one as a list index
or between words copied from product or fixture text; keep such text out of
telemetry rows. `RequestTelemetryNumbers()` is one explicit, process-wide,
one-way and idempotent ask that the converted shared helpers print the numbers
they themselves measure, count or time as `TelemetryNumber`s. It takes no
argument and reads nothing, so it cannot depend on a secret: a campaign calls
it unconditionally at its first stage, before any row it may scan, never under
a condition and never from code that read a fixture secret. The first call
logs exactly `TELEMETRY_NUMBERS: requested`; a later call does nothing and
logs nothing, and there is no undo. `HelperNumberFormat()` answers
`NumberFormat::Ordinary` until then and `NumberFormat::Telemetry` after it. A
converted helper formats each row through a pure formatter taking a
`NumberFormat`, which its logging path calls with `HelperNumberFormat()`, and
prints each number through `HelperNumber(value, format)`
(`QString::number(value)` or `TelemetryNumber(double(value))`) or, in an
`.arg` chain that already substituted caller text, `ArgNumber(text, value,
format)`, which under `Ordinary` is the same integer `QString::arg` call as
before, so a row printed without the request stays byte-identical for any
caller text. Every converted quantity is an integer - pixels, counts,
ordinals, 0/1 flags, `crl::now()` milliseconds - and prints with `decimals` 0:
`4821p`, `-12p`, `0p`. The converted rows: `GEOMETRY:` (`LogGeometry`); the
`CheckNear` verdict; the `PostPaintSampler` `kind=post` and `kind=stop` rows;
the `RunClockedFrames` `kind=frame` row, its `frame <k> at <ms> ms` subject,
its gate and refusal reasons and its request refusal; the `Probe` window
bounds, `checkCountSince`'s counts and the round-trip tallies and times; and
the `DiscriminatingScan` `examined=` tally row and its refusal, the secrecy
scan's `SECRECY: <class>` reports among them. Their grammars in both formats
are in the rows of `test_log.h`, `test_probe.h`, `test_post_paint.h` and
`test_animation_clock.h` and in [capture guidance](test_capture.md). Not
converted, ordinary in both modes: caller text and detail lists (a probe row,
a check's `what`, a scan's subject or control name and its details, a
self-test's own numbers), `PostPaintSampler::details()`, the sampler's
empty-window note, the scan's `SECRECY_*` rows and its three check rows,
`test_runner`'s rows, and every other module, such as `test_capture`'s own
readings, `test_ink`, `test_corner_patch` or `test_wallet_intercept`. In every
converted row a helper-written word sits on at least one side of each
converted number, so a number never splits a word run made only of caller
text. A judge splits `test_log.txt` at the first line equal to
`TELEMETRY_NUMBERS: requested`: a converted row before it holds ordinary
integers and one after it `TelemetryNumber`s, and a log without that line
never asked. A text a helper formatted before the ask and a caller prints
after it, such as a kept `RoundTrip::observation` or `ClockedRun::refusal`,
stays ordinary; asking at the first stage avoids that. Its proofs are two
stages of `AppendSecrecyScanSelfTest`: `secrecy_self_telemetry` for the
format, and `secrecy_self_helper_numbers` for the converted rows, which reads
each of them through the helper's own formatter for fixed synthetic values -
without the request it prints today's text, and under it the same text with
only a `p` after each number; the ordinary text's digit runs, as synthetic
short secrets, fail the scan at that row, while the requested row holds none
bounded and scans clean; and a synthetic phrase in its caller text gives the
same word runs in both formats - and never asks the request itself.

## Verdict details

`Test::Check`'s third argument is an observation, not a failure excuse. It is
printed on both verdicts — `TEST_RESULT: PASS: <what> - <details>` and
`TEST_RESULT: FAIL: <what> - <details>` — so a green log says what each check
was made against and a passing run can be audited without re-running it. Pass
the values the check judged: the measured geometry, the observed identity, the
window and the rows behind the verdict. Text that is only true after a failure
stays conditional at the call site — `ok ? QString() : u"out of tolerance"_q` —
which leaves the passing line exactly `TEST_RESULT: PASS: <what>`, the same
line an empty `details` produces. Do not emit a `Note` beside a check only to
print a reading that check's own `details` could carry; that duplication is
what this argument replaces. Where the failure text cannot double as a true
passing observation, the `details` stay conditional and an adjacent `Note`
remains the carrier — `CheckBlockedLaunchesExactly` suppresses its mismatch
text on a pass and keeps its `Test::Note(u"blocked launch record: [...]")`,
which is what `test_launch_fuse.h` promises. None of this loosens a refusal:
failure text still has to name what it judged, and an undecidable reading is
still refused rather than printed as a `Note` a reader would take as a
measurement.
