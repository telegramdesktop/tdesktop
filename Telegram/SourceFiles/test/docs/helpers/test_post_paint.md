# test_post_paint

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_post_paint.h](../../test_post_paint.h).

## Use when

Samples after real product paints. Read the contract below and the source header before use.

## Contract and usage

`PostPaintSampler`: samples one painted owner right after each of its product
paints instead of on a timer. A `base::install_event_filter` on the owner sees
each `QEvent::Paint`; a product paint inside a `sampleDuring` window posts at
most one queued sample that runs after the paint returned, and later paints
share a pending one (`covered`); a recursion guard ignores the paints the
sampler's own action causes; `grabFreeDuring` windows, with a lead of the
longest action so far and at least 40 ms, take no sample and count the queued
ones they drop (`dropped=`); the owner is held weakly and its destruction logs
one `kind=stop reason=owner-destroyed` row; rows are read through `mark()` /
`samplesSince()` and printed by `PostPaintSampleText`. Both rows follow
`Test::RequestTelemetryNumbers()` (`test_log.h`): `PostPaintSampleText` and
`PostPaintStopText` (over a `PostPaintStop`) also take an explicit
`NumberFormat`, and a row formatted after the ask prints every number as a
`TelemetryNumber`, while `details()` and the empty-window note keep ordinary
integers. With its own self-test on a harness-owned opaque `Ui::RpWidget`
whose every paint costs 18 ms, needing only a shown, non-minimized primary
window, which its fixture stage keeps exposed with `Test::KeepWindowExposed`
until its last stage (a minimized or hidden window still fails the gate once
and every later stage is `N/A`): a timer-only control at a 16 ms cadence over
1500 ms printing its largest gap, missed paints, lateness, the paints started
while a tick was overdue and the lateness of ticks with no paint inside, as
context; a 1500 ms stressed window of at least 20 back-to-back paints in which
every product paint is covered by a sample taken after it returned, within one
product paint plus one grab; one product paint yielding exactly one sample and
none over an idle window of at least 500 ms although a grab painted the
widget; a grab-free window `[from + 400, from + 700)` holding at least 3
product paints and no sample, with the first paint after it sampled; and the
owner destroyed with a sample pending, after which no action runs, the stop is
logged and the next stage runs.

## Sampling product paints

Sample an animating owner whose product paints can run back to back —
several animations at once, especially in Debug — with `PostPaintSampler`
(`test_post_paint.h`) instead of a timer-driven sampler. In Attempt 1 Run 1
of `2026/10/05/play-gram-card-read-animations-one-after-another` a one-shot
16 ms `base::Timer` sampler of a chat list had a 52 ms gap, from grab 25
(`hi` 44159) to grab 26 (`lo` 44211), while three product paints ran back to
back (frames 44159, 44173 and 44193, about 16-20 ms each in Debug), and the
product's own precise timer reported `arrive due=44159 late=52`; the bound
`vstart(B2).hi - turn <= 50` spanned that gap, read 75 and stayed undecided.
Hand the sampler a name for its rows, the owner whose own `paintEvent`
paints the measured surface (a child's paints are not seen), one action —
normally one `Test::GrabRect` of an owner-local rect — and
`sampleDuring(from, till)` windows in `crl::now()` milliseconds; outside
every window it is idle. A product paint received inside a window posts at
most one queued sample, which runs after that paint returned; a paint
arriving while one is pending shares it. Each sample is one row and one log
line, `NOTE: <name>: kind=post seq=<n> paint=<ms> last=<ms> covered=<k>
lo=<ms> hi=<ms>`: `paint` and `last` are the receipt times of the first and
the last product paint it covers, `covered` counts them, and `lo` / `hi` are
`crl::now()` taken by the sampler itself right before and right after the
action. Read rows through `mark()`, taken immediately before the action
under test, and `samplesSince(mark)`, and key images by the `seq` the action
receives, never by position. A sample's `lo` never precedes the end of the
paint it follows, so a bound from that paint's start shorter than the paint
plus one action cannot be decided with it.

`grabFreeDuring(from, till)` declares a window in which no sample runs. Its
lead is the longest action so far, at least 40 ms: a paint received from
`from` minus the lead until `till` posts nothing and joins no pending
sample, a queued sample that would start there is dropped without a row and
counted in `dropped=`, and sampling resumes with the first product paint
received at or after `till`. The recursion guard ignores every paint
received during the action: a grab delivers `QEvent::Paint` to the owner
through `QWidget::render`, so a sample never posts another, while an owner
that requests a repaint from every paint keeps being sampled because it
keeps painting. Any other grab of the owner or of an ancestor taken inside a
sampling window looks like a product paint: route it through the action or
take it outside the windows. There is no timer fallback, so nothing is
sampled without a product paint; a frame of an idle surface is a plain grab
taken outside the windows. The owner is held weakly. When it is destroyed, a
pending sample does nothing and the sampler logs one `kind=stop
reason=owner-destroyed` row with its `pending=`, `samples=`, `paints=`,
`ignored=` and `dropped=` counts; when the sampler is destroyed first, its
queued call does nothing, and its destructor logs nothing, so read the rows
before releasing it. The stop row is `NOTE: <name>: kind=stop
reason=owner-destroyed seq=<n> pending=<0|1> samples=<n> paints=<n>
ignored=<n> dropped=<n>` (`PostPaintStopText`). Both rows print ordinary
integers until `Test::RequestTelemetryNumbers()` (`test_log.h`); a row
formatted after it prints every one of its numbers as a `TelemetryNumber`
instead, for example `kind=post seq=4821p paint=37645p last=37693p
covered=59p lo=61482p hi=61537p` and `kind=stop reason=owner-destroyed
seq=4821p pending=1p samples=3764p ...`. A judge reads a value of either
grammar with `float(v.replace('p', '.'))`. `PostPaintSampleText` and
`PostPaintStopText` also take an explicit `NumberFormat`, so a self-test
reads both formats. `details()` and the empty-window note (`NOTE: <name>:
ignored an empty sampling|grab-free window from=<ms> till=<ms>`) keep
ordinary integers either way. Confirm with
`AppendPostPaintSamplerSelfTest`.
