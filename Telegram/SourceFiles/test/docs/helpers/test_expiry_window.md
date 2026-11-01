# test_expiry_window

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).
Source contract: [test_expiry_window.h](../../test_expiry_window.h).

## Use when

An overlay samples product behavior at a cutoff derived from a served
expiry: a temporary link's or free code's Copy/Share that must refuse inside
the product's share cutoff before the served expiry, a frozen Share that must
close at its own deadline, a renderer with its own deadline, a natural-expiry
wait, or a clock resynchronization. `Test::FreezeExpiryWindow` binds a
positive served expiry to the exact live owner, generation and link, with its
receipt and a fresh server-clock reading, and freezes one immutable window
per output. `Test::ExpiryBoundaryWatch` polls that window to its boundary, and
`Test::PlanExpirySample` re-validates the premise before an action. Every
premise refuses by name before anything can be scheduled.

The module reads no product state: the overlay supplies both sides. For
animation frames use [test_animation_clock](test_animation_clock.md); for
paint timing, [test_post_paint](test_post_paint.md); for a server rate
deadline on fixture writes,
[test_fixture_transaction](test_fixture_transaction.md).

## Preconditions and limits

- Two sources, never copied into each other. `Test::ExpiryObservation` is the
  oracle side, recorded where the served value arrives (an RPC reply hook):
  `owner`, `generation`, `link`, `servedExpires` (unixtime seconds) and
  `receipt = Test::ReadServerClock()`. `Test::ExpiryReading` is the product
  side, read at each use through the product's own typed seam: `owner`, the
  caller's current `generation`, `link`, `productExpires` and a fresh
  `clock = Test::ReadServerClock()`. A validation compares a product value
  with a served one; filling the reading from the observation compares a
  field with itself (an earlier overlay's clock-update control passed as
  `0==0`; see [failure diagnosis](../diagnostics.md)). A caller without a
  typed product seam cannot freeze a window.
- No default satisfies validation. Every number is a `std::optional`, the
  owner a `QPointer<QObject>` and the link a `QString`, and absence refuses
  before any equality, so two absent values (which `std::optional` compares
  equal) or two zeros never establish a premise.
- The owner is the exact object holding the served value (the box), not an
  output the product closes at its cutoff (a Share box). Identity is the
  `QPointer`: it is null once that object is destroyed, and a new object at
  the same address never revives it. Main thread only.
- Feed `productExpires` from a value the product holds until the link
  changes (the box's current link expiry), not a frozen copy it clears at its
  own cutoff. The product's monotonic deadline (for example
  `LinkExpiry::deadline` in `peer_qr_box.cpp`, a receipt-based `crl::now()`
  time) is never an input: the oracle stays independent of it.
- `generation` is the caller's counter, for example the count of served
  responses. Any present value, 0 included, is explicit; only `std::nullopt`
  is absent.
- Links compare exactly and are never printed: details read `same`, `other`
  or `absent`. `ExpiryWindowArgs::label` is public text naming the window,
  never a link or a secret.
- Times are server-shifted wall milliseconds since the epoch.
  `Test::ReadServerClock()` adds `base::unixtime`'s whole-second shift,
  taken as `base::unixtime::now()` minus `time(nullptr)` (the clock that
  shift is added to), to one `QDateTime::currentMSecsSinceEpoch()` read, so
  Qt's millisecond rounding or tick granularity never moves a reading by a
  second. It reads the shift between two equal `time(nullptr)` reads, up to
  three times when that second rolls over, and is absent if it never
  settles. Its fraction comes from the local clock, a resynchronization moves
  it by whole seconds, and it is never a product countdown display value.
  The module never calls `base::unixtime::update()`.
- A window's cutoff is `servedExpires * 1000 - cutoffLead`. Choose
  `cutoffLead` as the earliest moment the product may react: 3000 for the
  product's `kShareCutoff`; a few hundred ms for a natural expiry the product
  acts on at the expiry itself, with a watch offset past it. Freezing
  requires the fresh clock to be at least `minimumAhead` ms before the cutoff
  (inclusive) and strictly before it, so actions above the cutoff have time.
- No rows of its own: callers `Test::Check` the `details` text. Numbers are
  ordinary integers; it is not a helper converted to
  `Test::RequestTelemetryNumbers()`.
- The overlay's `until` polls on the runner's 50 ms tick, so a boundary is
  observed up to one tick late (more on a busy event loop), and `lateMs`
  reports it. The stage timeout and `TDESKTOP_TEST_WATCHDOG` must cover the
  whole wait.

## Minimal use

An overlay sketch against the real API; no translation unit compiles it (the
self-test compiles the API over literal values). `box`, `url` and `expires`
stand for the reply hook's values, and `CurrentReading()` for the overlay's
typed product seam plus `Test::ReadServerClock()`.

```cpp
struct Timing {
	Test::ExpiryObservation served;
	std::shared_ptr<Test::ExpiryBoundaryWatch> share;
	uint64 generation = 0;
	QString gate;
};
const auto timing = std::make_shared<Timing>();

// Reply hook, where the served response arrives:
timing->served = {
	.owner = box,
	.generation = ++timing->generation,
	.link = url,
	.servedExpires = expires,
	.receipt = Test::ReadServerClock(),
};

runner->add({
	.name = u"share expiry window: freeze"_q,
	.then = [=] {
		const auto freeze = Test::FreezeExpiryWindow({
			.label = u"share"_q,
			.cutoffLead = crl::time(3000),
			.minimumAhead = crl::time(4000),
		}, timing->served, CurrentReading());
		Test::Check(
			freeze.frozen(),
			u"fixture gate: share expiry window frozen"_q,
			freeze.details);
		timing->share = std::make_shared<Test::ExpiryBoundaryWatch>(
			freeze,
			crl::time(60));
		if (!freeze.frozen()) {
			timing->gate = u"fixture gate: share expiry premise refused"_q;
		}
	},
});
runner->addGated([=] { return timing->gate; }, [&] {
	runner->add({
		.name = u"share expiry window: boundary"_q,
		.until = [=] {
			return timing->share->poll(CurrentReading())
				!= Test::ExpiryWatchState::Waiting;
		},
		.then = [=] {
			const auto reached = (timing->share->state()
				== Test::ExpiryWatchState::Reached);
			Test::Check(
				reached,
				u"fixture gate: share premise held to the boundary"_q,
				timing->share->details());
			if (!reached) {
				timing->gate = u"fixture gate: share boundary refused"_q;
				return;
			}
			// Product assertions at the boundary.
		},
		.timeout = crl::time(90000),
	});
});
```

A refused freeze sets the gate, so the boundary stage and every later stage
of the group are N/A instead of waiting on a refused premise
([gated groups](test_runner.md#gated-groups-and-recovery-stages)). An action
above the cutoff first calls
`Test::PlanExpirySample(*timing->share->window(), CurrentReading())` and
requires `accepted()` and an `untilCutoffMs` margin that covers the action.
A second output (a render, a natural-expiry wait) gets its own window and
watch; a refreshed served value freezes a new window, and existing windows
never change.

## Readings and refusals

Three pure readings, each with a named refusal and a one-line `details` that
the caller prints on both verdicts:

- `Test::ExpiryFreeze` from `FreezeExpiryWindow(args, observation, current)`:
  `window` is non-null exactly when `refusal` is `none`, and `frozen()`
  requires both. The window exposes `label()`, `owner()` (null once the owner
  is destroyed), `generation()`, `link()`, `servedExpires()`,
  `receiptServerMs()`, `frozenServerMs()`, `cutoffLead()` and
  `cutoffServerMs()`.
- `Test::ExpiryPlan` from `PlanExpirySample(window, current)`: `untilCutoffMs`
  is present only on acceptance (`accepted()`), and is `cutoffServerMs()`
  minus the reading's clock. It may be at or below zero past the cutoff, so
  an action above the cutoff requires its own margin. A refused plan has no
  value, so nothing can be scheduled from it.
- `Test::ExpiryBoundaryWatch`: `poll(current)` returns the state
  (`Waiting`, `Reached`, `Refused`). `refusal()` is `none` unless the state is
  `Refused`, so decide by `state()`. `window()` is null when the watch was
  built from a refused freeze; `boundaryServerMs()` (cutoff plus offset) is
  present when a window exists; `untilBoundaryMs()` only while Waiting, from
  the last poll's clock; then `reachedServerMs()`, `polls()`,
  `confirmations()` and `details()`.

A frozen window proves that at the freeze reading the observed owner was
live and was the product's current owner, both generations and links were
present and equal, the served expiry and the receipt were present and
positive, the product's own expiry was present, positive and equal to the
served one, and the clock was positive and far enough before the cutoff. A
Reached watch adds that every poll before the cutoff (before the boundary,
for a negative offset) re-confirmed that premise, at least one did, and the
owner stayed the window's to the boundary. Neither proves product behavior
at the cutoff: that is the caller's sample. Every result defaults to
`not-evaluated`, which never accepts. A refusal is never a product PASS or
FAIL: it is a fixture gate, and the measurement it guarded is undecided.

| Name | Meaning | Where |
| --- | --- | --- |
| `none` | Accepted. | Freeze, plan, watch. |
| `not-evaluated` | The default of every `ExpiryFreeze` and `ExpiryPlan`; never accepts. | A default result; a watch built from one. |
| `owner-not-live` | The observed owner (freeze) or the window's owner (plan, watch) was never recorded or is destroyed. | Freeze, plan, watch at every poll. |
| `wrong-owner` | The reading's owner is another object or null. | Freeze, plan, watch at every poll. |
| `generation-absent` | Either side has none; a window always has one, so in a plan or a watch it is the reading's. | Freeze, plan, watch before `stop`. |
| `obsolete-generation` | The reading's generation differs. | Freeze, plan, watch before `stop`. |
| `link-absent` | Either side is empty; in a plan or a watch, the reading's. | Freeze, plan, watch before `stop`. |
| `wrong-link` | The reading's link differs. | Freeze, plan, watch before `stop`. |
| `served-expiry-absent` | The observation has no served expiry. | Freeze. |
| `served-expiry-not-positive` | The served expiry is 0 or negative. | Freeze. |
| `receipt-absent` | The observation has no receipt clock. | Freeze. |
| `receipt-not-positive` | The receipt clock is 0 or negative. | Freeze. |
| `product-expiry-absent` | The reading has no product expiry. | Freeze, plan, watch before `stop`. |
| `product-expiry-not-positive` | The product expiry is 0 or negative. | Freeze, plan, watch before `stop`. |
| `product-expiry-differs` | The product's value is not the served one (the window's, in a plan or a watch). | Freeze, plan, watch before `stop`. |
| `clock-absent` | The reading's fresh clock is absent. | Freeze, plan, watch at every poll. |
| `clock-not-positive` | The reading's fresh clock is 0 or negative. | Freeze, plan, watch at every poll. |
| `cutoff-not-ahead` | The fresh clock is not at least `minimumAhead` and strictly before the cutoff. | Freeze. |
| `boundary-unconfirmed` | The watch reached `stop` before any poll confirmed the full premise. | Watch. |

A watch built from a refused freeze is Refused at construction by that
freeze's name, so every freeze-only name can also be a watch's.

First refusal, in order:

- Freeze: owner (`owner-not-live`, then `wrong-owner`), generation
  (`generation-absent`, then `obsolete-generation`), link (`link-absent`,
  then `wrong-link`), served expiry (absent, not positive), receipt (absent,
  not positive), product expiry (absent, not positive, differs from served),
  clock (absent, not positive), then `cutoff-not-ahead`. So a served and a
  product expiry both absent refuse `served-expiry-absent`, both 0 refuse
  `served-expiry-not-positive`, and a receipt and a clock both 0 refuse
  `receipt-not-positive`.
- Plan: `owner-not-live`, `wrong-owner`, `generation-absent`,
  `obsolete-generation`, `link-absent`, `wrong-link`, product expiry
  (absent, not positive, differs from the window's served expiry), clock
  (absent, not positive). The served expiry and the receipt are the
  window's, validated once at freeze.
- Watch poll: the clock (absent, not positive) first; then strictly before
  `stop` the plan's order; at or past `stop`, `boundary-unconfirmed` when no
  poll confirmed, then `owner-not-live`, `wrong-owner`.

### The boundary rule

A watch's boundary is the cutoff plus its offset; `stop` is the earlier of
the cutoff and the boundary.

- Built from a freeze that is not `frozen()`, the watch starts Refused with
  that freeze's refusal (`not-evaluated` when it reads `none`), a null window
  and zero polls: there is nothing to wait on.
- Reached and Refused are final: a later poll changes and counts nothing.
  Every other poll counts in `polls()` and refuses an absent or non-positive
  clock.
- Strictly before `stop`, `PlanExpirySample` must accept, or the watch
  refuses with its refusal. Each acceptance counts in `confirmations()`, is
  kept as `confirmedServerMs`, and sets `untilBoundaryMs` to the boundary
  minus this reading.
- At or past `stop`, zero confirmations refuses `boundary-unconfirmed`;
  otherwise only the owner is validated (`owner-not-live`, `wrong-owner`), so
  a destruction or replacement during the wait never transfers. Generation,
  link and product expiry are not refused there: from its cutoff the product
  may close the frozen Share, clear its frozen expiry or replace the link,
  and that reaction is what the caller samples at the boundary.
- Strictly before the boundary the watch stays Waiting with
  `untilBoundaryMs` the boundary minus this reading; at or past it, Reached
  with `reachedServerMs` this reading and `untilBoundaryMs` cleared.
- Every poll recomputes from its own reading, so a clock resynchronization in
  either direction moves the boundary in wall time and never changes the
  window. A negative offset samples before the cutoff: `stop` is then the
  boundary, and the full premise is validated up to it.

### Details formats

Each is one line, wrapped here. A freeze:

```
window=<label> refusal=<name> owner=<live:Class|not-live>
  current=<same|other|absent> generation=<n|none>/<n|none>
  link=<same|other|absent> served=<n|none> receiptServerMs=<n|none>
  product=<n|none> clockServerMs=<n|none> cutoffLead=<ms>
  minimumAhead=<ms> cutoffServerMs=<n|none> untilCutoffMs=<n|none>
```

The generations are the observation's and the reading's. A not-positive
value prints as its number, so `served=0` is visible; `cutoffServerMs` is
printed only from a positive served expiry, and `untilCutoffMs` only from it
and a positive clock. A plan:

```
window=<label> refusal=<name> owner=<live:Class|not-live>
  current=<same|other|absent> generation=<window>/<n|none>
  link=<same|other|absent> served=<window> product=<n|none>
  clockServerMs=<n|none> cutoffServerMs=<n> untilCutoffMs=<n|none>
```

A watch:

```
window=<label|none> state=<waiting|reached|refused> refusal=<name>
  offset=<ms> cutoffServerMs=<n|none> boundaryServerMs=<n|none>
  polls=<n> confirmations=<n> confirmedServerMs=<n|none>
  untilBoundaryMs=<n|none> reachedServerMs=<n|none> lateMs=<n|none>
  premise=[<premise>]
```

`lateMs` is the reached reading minus the boundary. The premise is the last
poll's plan details before `stop`, or
`owner=<live:Class|not-live> current=<same|other|absent> clockServerMs=<n|none>`
for a poll at or past it or with a refused clock; it is empty before the
first poll.

## Lifetime and cleanup

The module has no timers, rows, subscriptions, `onFinish` registration or
product state, and owns nothing but its values. A window holds its owner
weakly (`QPointer`) and is shared as `std::shared_ptr<const ExpiryWindow>`; a
watch keeps that pointer. Release watches with the scenario state. Nothing
needs restoring, and `base::unixtime` is never changed.

## Verification

`Test::AppendExpiryWindowSelfTest(runner)`, called after
`runner->waitEvent(u"launch_finished"_q)`, appends nine `then`-only stages
named `expiry window self-test: <leg>`. They run synchronously over literal
caller-owned identities and deadlines and plain `QObject` owners: no
session, network, account, widget, fixture secret, server fixture mutation
or QR surface. Only the last leg reads the real clock, and nothing calls
`base::unixtime::update()`. An account-free `--portable-root` sandbox (an
empty `<dir>/test_TelegramForcePortable`) is enough. Every refusal is
observed through pure readings and asserted as a passing Check whose details
carry it; a finish release frees the owners. Expected:
`SCENARIO_RESULT: PASS (failures: 0)` with no FAIL and no N/A row. The legs,
each with its control and falsifier:

- arm: a fixture gate on three live distinct owners; one distinct non-empty
  name for every refusal; defaults that never accept (an empty
  `ExpiryFreeze` and `ExpiryPlan`, a freeze from an empty observation and
  reading refusing `owner-not-live` with no window, a watch from an empty
  freeze Refused `not-evaluated` with a null window and zero polls, still so
  after a poll). Falsifier: an accepting default, or a missing or duplicate
  name.
- positive observation: a current positive observation freezes a window
  whose fields are the observation's, the receipt keeps its sub-second
  fraction, a plan at the freeze reading has the expected `untilCutoffMs`,
  and a watch polled at the freeze reading, one ms before the cutoff, at the
  cutoff and at the boundary reads Waiting, Waiting, Waiting, Reached with
  two confirmations (the poll at the cutoff is not one) and `lateMs=0`. The
  details read `link=same` and never the link text. Controls: a product
  expiry one second off refuses `product-expiry-differs`, and an absent
  served or product expiry refuses by its own name. Falsifier: a freeze
  comparing the served field with itself, or a dropped receipt fraction.
- freeze refusals: table rows for every freeze name: each owner null, each
  generation absent, each link empty (and both of each), another owner,
  generation or link, every expiry, receipt and clock absent and zero (a
  served expiry also -1), a differing product expiry, the both-absent and
  both-zero expiry pairs, a receipt and a clock both 0, and
  `cutoff-not-ahead` one ms inside `minimumAhead`, at and past the cutoff,
  and with `minimumAhead` 0 at the cutoff. Controls in the same table freeze
  at the inclusive `minimumAhead` edge and, with `minimumAhead` 0, one ms
  before the cutoff. Every refused freeze has no window, and a watch from it
  is Refused by the same name with zero polls and no `untilBoundaryMs`,
  before and after a poll. Falsifier: a `value_or(0)`-style comparison
  freezes the both-absent and both-zero rows; a wrong name; a Waiting watch.
- plan refusals: reading mutations against a frozen window refuse
  `wrong-owner` (another owner, null), `generation-absent`,
  `obsolete-generation`, `link-absent`, `wrong-link`, the three product names
  and the two clock names, each without `untilCutoffMs`. A fresh watch's
  first poll with the same reading is Refused by the same name with zero
  confirmations, and a later poll at the boundary leaves it Refused with one
  poll. Falsifier: any `untilCutoffMs` or `untilBoundaryMs` on a refusal
  row.
- simultaneous windows: Share, natural (same owner and link, lead 0) and
  render (another owner, generation and link) windows coexist; refreshing the
  render source freezes a second render window while the three snapshots
  stay unchanged with pairwise distinct cutoffs. The old render window
  refuses `obsolete-generation` against the new generation and the new one
  accepts; interleaved Share and render watches polled over one clock
  sequence each reach only at its own boundary. Falsifier: a changed
  snapshot, or one watch reaching at the other's boundary.
- destruction and replacement: an owner destroyed and rebuilt at the same
  address (control: the address is equal) leaves the window's `owner()`
  null. Planning, re-freezing from the kept observation and polling two
  watches (one before the cutoff, one inside the reaction window) refuse
  `owner-not-live` and stay Refused. Another live owner refuses
  `wrong-owner`; a new generation on the same owner refuses
  `obsolete-generation` on the old window and freezes a new one while the old
  keeps its generation. Falsifier: raw-pointer identity accepts the
  same-address object.
- boundary: an absent product expiry one ms before the cutoff refuses
  `product-expiry-absent`; from the cutoff to the boundary an absent product
  expiry, another link and a new generation (the product's reaction) stay
  Waiting, then Reached, with one confirmation; another owner inside that window refuses `wrong-owner`; a
  watch first polled at the cutoff or past the boundary refuses
  `boundary-unconfirmed`; a late poll reaches with `lateMs=2500` and the
  freeze reading as `confirmedServerMs`; a -500 ms offset reaches at exactly
  its boundary and validates the full premise up to it; a poll after Reached
  changes nothing. Falsifier: re-validating the product in the reaction
  window refuses those rows, skipping validation before the cutoff accepts
  the one-ms-before row, and an unbound owner reaches with another owner.
- resynchronization: forward (+5 s) and backward (-3 s) clock jumps
  recompute `untilBoundaryMs` from each reading, while the window keeps its
  served expiry, link, generation and cutoff; the clock-update check that
  once compared `0==0` becomes an accepted plan whose product expiry is the
  positive served one. After a jump, an absent or zero clock, another link
  and a product expiry rebased on the shifted clock refuse `clock-absent`,
  `clock-not-positive`, `wrong-link` and `product-expiry-differs`. Two polls 300 ms apart in one
  server second differ in `untilBoundaryMs` by exactly 300 while the
  whole-second countdown a product would display,
  `(untilCutoffMs + 999) / 1000`, is the same for both. Falsifier: a cached
  first delay, an accepted absent or zero clock, a moved link, a rebased
  product expiry or a display-rounded reading.
- real server clock: first the composition over literal reads, with
  `base::unixtime::now()` 7 s ahead of `time(nullptr)`: a mid-second Qt
  read, one rounded up into the next second (Unix rounds to the nearest ms)
  and one a tick behind `time(nullptr)` (Windows) each keep their
  milliseconds and add exactly 7000. Then one `Test::ReadServerClock()`
  between two `QDateTime::currentMSecsSinceEpoch()` reads is present,
  positive and within `[before + 1000 * shift, after + 1000 * shift]`,
  where `shift` is read independently (`base::unixtime::now()` minus
  `time(nullptr)`, between two equal `now()` reads). The details print every
  value and the reading's `% 1000` fraction on both verdicts. Read-only.
  Falsifier: taking the local second from Qt's milliseconds is 1000 ms off
  in the rounded and the lagging rows, and a wrong shift sign is 14000 off;
  on the real clock, a reading that drops or rounds its fraction falls
  outside the bracket unless the local reads sit on a whole second (about one
  run in a thousand), and so do a unit error and a wrong shift sign while the
  shift is nonzero; an absent or non-positive reading or an unsettled shift
  fails the same Check.

Compile-time immutability proofs: `static_assert`s in the translation unit
state that `ExpiryWindow` is neither copy-constructible nor copy-assignable
and that `ExpiryFreeze::window` is a `std::shared_ptr<const ExpiryWindow>`;
the window's constructor is private, so only `FreezeExpiryWindow` makes one.

Each falsifier above fails a Check in the same run. Proposed, not part of the
self-test, and executed only when a reviewer doubts the in-process
falsifiers: one disposable mutation build whose absence checks compare
`value_or(0)` and whose served check accepts 0, expected to FAIL the
both-absent and both-zero rows. Record executed results in the change's
evidence, and say when the mutation was only proposed.

## Related guidance

[Runner gated groups](test_runner.md#gated-groups-and-recovery-stages);
[test_probe](test_probe.md) (a reading that cannot discriminate refuses);
[test_fixture_transaction](test_fixture_transaction.md) (the nearest harness
API with a deadline and an injected clock);
[failure diagnosis](../diagnostics.md).
