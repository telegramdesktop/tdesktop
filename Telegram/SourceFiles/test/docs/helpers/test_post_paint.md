# test_post_paint

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_post_paint.h](../../test_post_paint.h).

## Use when

Samples after real product paints, coordinated samplers sharing a capture
scope, and per-paint receipts for reconciliation and first-paint controls.
Read the contract below and the source header before use.

## Contract and usage

`PostPaintSampler`: samples one painted owner right after each of its product
paints instead of on a timer. A `base::install_event_filter` on the owner sees
each `QEvent::Paint`; a product paint inside a `sampleDuring` window posts at
most one queued sample that runs after the paint returned, and later paints
share a pending one (`covered`); a recursion guard ignores the paints the
sampler's own action causes, and a shared `PostPaintCaptureScope` extends
it to the actions of every member
([coordinated samplers](#coordinated-samplers-and-receipts)); every paint
the filter sees is one receipt, read through `receiptMark()` /
`receiptsSince()` / `firstReceiptSince()`; `grabFreeDuring` windows, with a lead of the
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
sampling window looks like a product paint, unless it is the action of a
sampler sharing this sampler's capture scope (below): route it through such
an action or take it outside the windows. There is no timer fallback, so nothing is
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

## Coordinated samplers and receipts

One sampler per child of a composite owner whose action grabs the whole
composite is the coordinated case. The grab paints every child
synchronously through `QWidget::render`, and each sampler's own recursion
guard covers only its own action, so without coordination the other
children's samplers count those harness paints as product paints and post
samples whose grabs paint the first child again. In
`2026/10/18/tighten-money-tabs-without-moving-the-transactions-title`
(`work/test.md`, "Independent discovered harness follow-up") such samplers
classified harness paints as product paints and recursively queued more
composite samples; the disposable recovery shared a capture flag and
published each paint's receipt through owner properties.

**The scope.** Create one `std::make_shared<Test::PostPaintCaptureScope>()`
and hand it to every member's constructor. A member enters the scope around
its action, and every member checks the scope as well as its own flag, so a
paint any member receives while any member's action runs is a `Capture`
receipt: counted in `ignored=`, never sampled, never joined to a pending
sample. Only members' actions are excluded. A paint received outside them is
classified as before, including a later paint that a capture paint
requested with `update()`, which is a genuine product paint and is sampled.
The scope is left when the action returns, even when the action destroyed
the sampler, and `scope->capturing()` answers whether some member's action
is running. Samplers that share no scope, and every sampler built without
one, keep the instance-local guard alone: to them another sampler's grab of
their owner is a product paint. `details()` ends with `scope=shared|local
receipts=<n>`.

**Receipts.** While the sampler is live, every paint its filter sees is one
`PostPaintReceipt {index, at, kind}`, classified at receipt; a stopped
sampler records none, and receipts never change a row or the stop row.

| Kind | Meaning |
| --- | --- |
| `Product` | A counted product paint (`paints=`) that posted a sample or joined the pending one; its `at` is the row's `paintAt` or `lastPaintAt`. |
| `Untracked` | A counted product paint outside every sampling window or inside a grab-free window, its lead included; no sample follows it. |
| `Capture` | A paint caused by this sampler's own action or by the action of any sampler sharing its scope, counted in `ignored=`. |

`index` is 1-based in receipt order. Read receipts the way rows are read:
take `receiptMark()` immediately before the action under test and pass it
to `receiptsSince(mark)`. The sampler's filter runs before the owner's
`paintEvent`, so `receiptMark()` read inside that `paintEvent` is this
paint's index: stamp it in the caller's own paint records and reconcile
them to the receipts one to one by index, with a capture record exactly
where the receipt is `Capture`, never by position or by counting.
`PostPaintReceiptText` prints `receipt index=<n> at=<ms>
kind=product|untracked|capture` in ordinary integers, as does the refusal
below, whatever `Test::RequestTelemetryNumbers()` asked for rows.

**The first-receipt pitfall.** An exposed idle window has promised no
paint: being shown or exposed is not a paint request, and time passing is
not one either. In Recovery plan 1 of the same campaign the ledger recorded
zero exact-owner paints in a 500 ms window, `[9016,9516)`, of an idle exposed
surface whose later endpoint had correct geometry; the overlay had never
asked it to update. To prove an owner paints, open a sampling window
(outside one the paint is `Untracked`, and `firstReceiptSince` looks for
`Product` by default), take `receiptMark()`, call `update()` on the owner
once and wait in a pure `until` for `firstReceiptSince(mark).received()`.
Without one the reading is the named refusal `absent-receipt: no <kind>
receipt since mark <m> (receipts=<n>, last=<receipt or none>, stop=<reason
or ->)`, which is a failed control, never a pass. Visibility, elapsed time
or a `Capture` receipt - a grab paints whether or not the window would - is
never a first-paint control.

```cpp
// One sampler per child, one shared scope; the action grabs the composite.
state->scope = std::make_shared<Test::PostPaintCaptureScope>();
const auto grab = [=](int) {
	state->frames.push_back(Test::GrabWidget(state->composite.data()));
};
state->first = std::make_unique<Test::PostPaintSampler>(
	u"first"_q,
	state->firstChild.data(),
	grab,
	state->scope);
state->second = std::make_unique<Test::PostPaintSampler>(
	u"second"_q,
	state->secondChild.data(),
	grab,
	state->scope);

// The first-paint control: one requested paint, its exact first receipt.
runner->add({
	.name = u"the first child paints after one update()"_q,
	.run = [=] {
		state->till = crl::now() + 3000;
		state->first->sampleDuring(crl::now(), state->till);
		state->mark = state->first->receiptMark();
		state->firstChild->update();
	},
	.until = [=] {
		return state->first->firstReceiptSince(state->mark).received()
			|| (crl::now() >= state->till);
	},
	.then = [=] {
		const auto first = state->first->firstReceiptSince(state->mark);
		Test::Check(
			first.received(),
			u"the first child painted after one update()"_q,
			first.received()
				? Test::PostPaintReceiptText(first.receipt)
				: first.refusal);
	},
});
```

Release every sampler before the widgets they observe, and read receipts
and rows before releasing a sampler: it keeps no history after it is gone.
Confirm with `AppendFullOwnerCaptureSelfTest`
([self-test](test_full_owner_capture.md)): its arm A2 shows a shared scope
excluding every composite capture paint while one real update on each child
yields exactly one bounded sample, and arm A3, the same composite capture
through samplers without a scope, passes only when it exposes the defect -
a capture paint taken for a product paint and recursive extra samples.
