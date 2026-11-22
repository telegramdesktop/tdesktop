# test_widget_ops

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API, with focused self-checks.
Source contract: [test_widget_ops.h](../../test_widget_ops.h).
Internal to it:
[test_widget_ops_internal.h](../../test_widget_ops_internal.h).

| File | Holds |
| --- | --- |
| `test_widget_ops.h` | The contract: the hosted seams, arming, the readings, the hosted death reading, the helper work and the self-checks. |
| `test_widget_ops_internal.h` | What the five translation units share: the scan and kept-value bounds, `kHelperMirrors`, the tap's record (`WidgetOpsTap`), the armed taps, the scan budget, the reader of effective theme answers, the mapping of helper stamps, the entries of the deaths, finals and exchange legs (`CheckHostedDeaths()`, `CheckHostedFinals()`, `CheckHostedExchanges()`) and `SelfCheckRowName()`. |
| `test_widget_ops.cpp` | The tap's record with its hosted works' records and finals, the scan (the `Upstream` block and its `ScanSink`), the result reader, arming, the readings and `WidgetTapFinished()`; without the harness, nothing. |
| `test_widget_ops_hosted.cpp` | The hosted tap: `TapHostedWidget()` with the final observation it hands the runtime, its observations and registry, `WidgetThemeObservation()`, `WidgetThemeApplied()`, `WidgetPressDispatched()`, `WidgetCoreDetached()`, `WidgetPressObserved()`, the effective theme probe, `WidgetThemeUnobserved()`, `CheckWidgetTheme()`, the helper work records and `CheckedTimeSum()`; without the harness, the hosted seams alone. |
| `test_widget_ops_check.cpp` | `RunWidgetOpsSelfCheck()` with its session, stress, hosted, finals, budget, long-name and scan-gap legs, its exchange leg played within the call or left apart, and `RunWidgetConformanceProbeCheck()` with its fixture replay and replacement legs, every leg on hosted taps. |
| `test_widget_ops_deaths.cpp` | The hosted death reading (`ReadHostedDeath()`, `WidgetHostedDeathText()`, `WidgetTapCharged()`, `WidgetTapHosted()` and the row texts), `WidgetHostedPlay`, and the self-check's deaths and finals legs (`CheckHostedDeaths()`, `CheckHostedFinals()`); without the harness, nothing. |
| `test_widget_ops_exchanges.cpp` | The self-check's exchange leg (`CheckHostedExchanges()`, seven rows): `Tgw::HostedAnswers()` against TwidgetCore's own encodings and `decode()`, frame deltas included, and a widget helper's stream end on two routes played over widget-sandbox's in-memory pair with its `ScriptedEngineEnd`; and `WidgetOpsExchanges`, which plays that leg on a thread of its own; without the harness, nothing. |

## Use when

A scenario needs what a widget message's sandbox host really sends its
renderer: the validated ops, every batch of them, including batches a later
batch removes or replaces and the batch a theme change re-sanitises the
stylesheets into. It arms one message, by its session-qualified
`GlobalMsgId`, before its host is constructed. A widget's engine,
validator and renderer run in the widget helper
([Hosted widgets](#hosted-widgets)), so the tap is installed on every
hosted `WidgetHost` built for that message: it takes the validated batches
from the host's `onRenderOps` and the rest from the records the helper's
Debug half ([test_widget_helper_tap](test_widget_helper_tap.md)) mirrors to
it. Each `ops` batch is scanned with widget-sandbox's `--attacks` leak
definition, its predicates copied and its examples bounded
([Leak definition and fidelity](#leak-definition-and-fidelity)). The tap also counts the host's
post-load ack, the renderer's answer to it, renderer
`violation` messages, renderer `limit` reports and trusted renderer events,
and, for each hosted work selected for the runtime's accounting, the
final charge that accounting made
([Hosted deaths and final charges](#hosted-deaths-and-final-charges)).
`WidgetTapFinished()` says when no tap or host of the message is still
pending and every such work has its final. A Release
build with the harness, whose helper has no Debug half, reads instead the
theme the widget itself took after each application and the clicks the
helper's renderer completed
([Which reading each build uses](#which-reading-each-build-uses)).

The same scan also reads what a conformance or stress widget publishes:
`ReadWidgetOpsLatest()` returns the last `data-conformance` value of the
adopted `<html>`, as widget-sandbox's `packages/conformance/CLIENTS.md`
("Reading the ops") reads it, every `data-stress` value of it in order with
its receipt time (`packages/stress/CLIENTS.md`, "What a widget tells you"),
the newest host's `ops` batch count, op total and largest batch, and the
scheme and the `theme` message the newest host gave its renderer.

Callers: the attack-corpus audit of
`2026/10/24/audit-the-widget-attack-corpus-through-a-validated-ops-tap`, and
the conformance runner of
`2026/10/24/run-the-widget-conformance-sets-through-the-chat-list-and-report`
([test_widget_conformance](test_widget_conformance.md)), which reads its
results, scheme and theme message with `ReadWidgetOpsLatest()` and checks
that reader against the package's `probe-fixture.json` with
`RunWidgetConformanceProbeCheck()`, which replays each case on a hosted
tap. The stress-kit runner of
`2026/10/30/run-the-widget-stress-kit-at-v0-4-0-on-a-release-build-without-lto`
is the planned reader of the `data-stress` values and the batch counts;
until it lands nothing reads them. The heavy-box evidence of
`2026/11/02/stop-widgets-past-the-heavy-box-limit-with-a-v0-6-0-twidgetcore`
reads `limitReports` and `limitExamples`: one report per strict host, none
under compat. The fixture's removal barrier
([test_widget_fixture](test_widget_fixture.md#readings-and-refusals)) and
the reading after it in both packs and the stress-kit runner decide
finality with `WidgetTapFinished()`; both packs read a hosted widget's
death with `ReadHostedDeath()`.

Use something else when:

- only the final widget DOM matters: that is not the ops, and a transient op
  never shows in it;
- the runtime's load, death and log are enough: `Tgw::Runtime::state()`,
  `interactive()` and `requestLog()` read them without a tap, and they also
  decide whether a card is live now, which the tap's historical counts do not.

## Preconditions and limits

- **Arm before construction.** `ArmWidgetOpsTap(id)` acts on hosts that
  `Tgw::Runtime::construct()` builds from then on, so arm the id a local
  message will have before the message is added: its `FullMsgId` (for
  example with a fresh `nextLocalMessageId()`) plus its session's
  `uniqueId()`. A host built earlier is not tapped. The id is matched
  against `HistoryItem::globalId()` at each construction.
- **One session.** Each `Data::Session` allocates local message ids on its
  own, so two sessions sharing a chat can hold different messages with the
  same `FullMsgId`; only the one whose session-qualified id is armed is
  tapped, and two armed ones get two taps. Keep the armed id for
  `DisarmWidgetOpsTap()`; never rebuild it later from whichever account is
  active then.
- **Active harness.** `TapHostedWidget()` returns an empty function, and
  leaves its `finish` out-parameter untouched, unless `Test::Active()`, and
  for every message id that is not armed; a build
  without the harness defines only the hosted seams: `TapHostedWidget()`
  and `WidgetThemeObservation()`, which return an empty function,
  `WidgetThemeScheduled()`, which returns 0, and `WidgetThemeSelected()`,
  `WidgetThemeApplied()`, `WidgetHostRetired()`, `WidgetPressDispatched()`,
  `WidgetCoreDetached()` and
  `WidgetPressObserved()`, which do nothing.
- **What the scan is.** A leak or a note is exactly what the copied
  `mentionsNetwork()`, `scanCssForLeaks()` and `scanForLeaks()` report for
  each op of a batch, nothing else. A URL in a non-`ops` message, a note and
  a renderer refusal are not leaks; violations are counted apart, and a
  consumer that calls a reading clean needs `violations == 0` as well.
  Every match counts in `leaks` or `notes`; its example is built only while
  that message's examples are fewer than 64 and within 64 KiB together, the
  place it names (an attribute or style name the widget chose) cut to 240
  bytes first, so many matches or long names in one message allocate no
  more than the examples the tap keeps.
- **Scan gaps.** A host message that is not JSON, or an `ops` message whose
  `ops` is not an array, is forwarded unchanged and counted as a scan gap,
  never as an empty batch. A clean result needs `scanGaps == 0`.
- **The scan budget.** Before it parses a host or renderer message the tap
  counts, without allocating, the JSON values it would parse into (keys
  included): a message of more than 262144 (`kMaxScanValues`, over three
  times the stress kit's largest validated batch, style-churn's 8001 ops of
  about 72000 values) is a scan gap, forwarded and never parsed, so a wide
  message within the 32 MiB wire limit cannot make Telegram build a tree of
  gigabytes. A result value or theme message longer than 256 KiB, and a
  helper scheme record longer than 64 bytes, are gaps and not kept. Every
  such gap leaves the reading incomplete: the attack audit's `scan-gaps`
  and the stress runner's level error read `scanGaps`.
- **Limit reports are the renderer's.** `limitReports` counts every
  renderer message of type `limit`, before it is forwarded, whatever its
  `code` and `count`, under any policy and after the widget died: two
  reports count twice even when `WidgetHost` ignores the second. What the
  host made of a report (its diagnostic, the stop, a `renderer-violation`
  for a malformed one, nothing under compat or once dead) is
  [test_widget_host](test_widget_host.md)'s reading. The count covers every
  host built for the message, so compare it with `factoriesSelected` (or the
  latest `generation`) before calling a second report a duplicate: a
  restarted widget's new host reports again.
- **Not measured.** The tap sees no network request: it observes what
  reaches the renderer and what the renderer answers. It does not tell
  whether a card is live, painted or interactive now.
- **The latest result is the newest host's.** Each hosted tap handed out
  for the message starts a new `generation` at once, and the latest fields
  restart with it: its result, html id, value count, scheme and theme message
  are cleared before its host is built, so an older host's `done` never
  completes a newer host's load. Whatever an older host still reports
  afterwards is passed on and, for batches, scanned as always; a result, a
  `theme` message or a `setTheme()` among it leaves the latest fields alone
  and is counted in `staleUpdates`, and none of its batches enters the
  latest `batches`, `ops` or `maxBatchOps`.
  `generation` advances at each selection, also one whose host was never
  installed (a failed or discarded construction), so above 1 it says only
  that the message was selected more than once, and 1 is no proof that a
  host ran: `wrappersBuilt` counts the hosts the tap was installed on, and
  only a result, a batch or another observation of a host shows that it
  ran. The latest fields cover only the newest selection. A runtime log is
  not tied to it: its summary can still be an older host's, which only
  that summary's own work and eligibility tell (`summaryWork`,
  `WidgetLogSummaryOwn()`,
  [test_widget_fixture](test_widget_fixture.md#readings-and-refusals)).
- **The value is raw.** `conformance` and each `stress` value are the
  attribute text, not checked to be JSON; the caller parses them and can
  cache a parsed `conformance` by `generation` and `serial`. A later `el` op
  adopting `<html>` without the attribute clears `conformance`; any later
  adoption clears `stress` and `stressDropped`. A value before the adoption
  or on another element is ignored, and so is a `rattr`, as the
  `CLIENTS.md` reader does.
- **Stress values are kept, not replaced.** `stress` holds every
  `data-stress` value of this host's adopted `<html>`, in the order the host
  sent them, each with `receivedAt`: `crl::profile()` (monotonic
  microseconds, the clock of [test_widget_host](test_widget_host.md)'s
  stamps) taken on the host thread when the host handed the batch to the
  tap, before the tap parsed it. That is before the renderer applies the
  batch on the widget render thread, and it is not the widget's own `t`.
  The list keeps the first `kWidgetStressValuesLimit` (4096) values, about
  34 minutes of the kit's 500 ms publications, within 4 MiB of values
  together (`stressBytes`); each later value only counts in
  `stressDropped`, and a nonzero `stressDropped` makes the list incomplete,
  never silently shortened.
- **Batch counts are the newest host's.** `batches`, `ops` and
  `maxBatchOps` count every `ops` message with an `ops` array that the
  newest host sent, with or without a result attribute, from its selection
  on: the stress kit's `maxBatchOps` "since the host was created". A scan gap
  is not a batch. `WidgetOpsReading`'s `batches` and `ops` stay the
  cumulative counts over every host of the message.

## Minimal use

Arm, inject, mark before the click, then disarm, remove and wait until the
tap is final before reading it. The wait's bound ends it too, so the
example reads absences only from a finished reading: an unfinished one
proves none.

```cpp
runner->add({
	.name = u"arm the next local message and inject it"_q,
	.run = [=] {
		state->id = GlobalMsgId{
			FullMsgId(
				history->peer->id,
				history->owner().nextLocalMessageId()),
			history->session().uniqueId(),
		};
		state->tap = Test::ArmWidgetOpsTap(state->id);
		// Add the local widget message with id state->id.itemId.msg here.
	},
});
// ... show the card, then right before the click:
//     Test::MarkWidgetOpsEvents(*state->tap);
// and deliver the click's own mouse-down inside
//     Test::DeliverMarkedPress(*state->tap, [&] { /* sendEvent */ });
runner->add({
	.name = u"remove the message and wait for its hosts"_q,
	.run = [=] {
		Test::DisarmWidgetOpsTap(state->id);
		// Destroy the local message here.
		state->removedAt = crl::now();
	},
	.until = [=] {
		return Test::WidgetTapFinished(Test::ReadWidgetOps(*state->tap))
			|| (crl::now() - state->removedAt > kJoinBound);
	},
	.then = [=] {
		const auto r = Test::ReadWidgetOps(*state->tap);
		const auto finished = Test::WidgetTapFinished(r);
		Test::Check(
			finished,
			u"the message's hosts are gone and its hosted works are final"_q);
		if (!finished) {
			return;
		}
		Test::Check(
			(r.batches > 0) && !r.scanGaps && !r.leaks,
			u"every validated batch was scanned and none leaks"_q);
	},
	.timeout = kJoinBound + Test::kDefaultStageTimeout,
});
```

A conformance widget's result is waited for on the latest reading, parsed by
the caller and kept with the generation it came from. Only the result
shows that a host ran: the wait can end at its bound, and a selection
whose host was never installed has generation 1 too:

```cpp
runner->add({
	.name = u"wait for the widget's result"_q,
	.run = [=] { state->loadedAt = crl::now(); },
	.until = [=] {
		const auto latest = Test::ReadWidgetOpsLatest(*state->tap);
		if (latest.serial != state->serial) {
			state->serial = latest.serial;
			state->generation = latest.generation;
			state->result = latest.conformance
				? QJsonDocument::fromJson(
					QByteArray::fromStdString(*latest.conformance)).object()
				: QJsonObject();
		}
		const auto status = state->result.value(u"status"_q).toString();
		return (status == u"done"_q)
			|| (crl::now() - state->loadedAt > kResultBound);
	},
	.then = [=] {
		const auto latest = Test::ReadWidgetOpsLatest(*state->tap);
		const auto status = state->result.value(u"status"_q).toString();
		Test::Check(
			(status == u"done"_q)
				&& (state->generation == 1)
				&& (latest.generation == 1),
			u"the message's one selected host ran the widget to done"_q,
			u"status=%1 generation=%2"_q.arg(status).arg(latest.generation));
	},
	.timeout = kResultBound + Test::kDefaultStageTimeout,
});
```

A stress widget's values are read once, after a measuring window whose
endpoints are `crl::profile()` stamps; the receipts place each value, and
only values in the window show that the host published there. The
example reports what it found and `stressDropped` rather than claiming
every value:

```cpp
runner->add({
	.name = u"read the window's data-stress values"_q,
	.run = [=] {
		const auto latest = Test::ReadWidgetOpsLatest(*message->tap);
		for (const auto &entry : latest.stress) {
			if (entry.receivedAt >= state->windowStart
				&& entry.receivedAt <= state->windowEnd) {
				state->windowValues.push_back(entry.value);
			}
		}
		state->maxBatchOps = latest.maxBatchOps;
		Test::Check(
			(latest.generation == 1) && !state->windowValues.empty(),
			u"the message's one selected host published in the window"_q,
			u"values=%1 dropped=%2"_q
				.arg(int(state->windowValues.size()))
				.arg(latest.stressDropped));
	},
});
```

The self-checks are run from one stage each and turned into checks by their
caller; the probe check gets the bytes of the pinned `probe-fixture.json`.
`RunWidgetOpsSelfCheck()` with no argument plays its exchange leg within
the call, about 5 s with the event loop blocked. A stage can leave the leg
apart and play it on a `WidgetOpsExchanges` that `onFinish` stops, as both
packs do. Every row is a Check of its own: reaching `then` passes nothing.

```cpp
const auto exchanges = std::make_shared<Test::WidgetOpsExchanges>();
runner->onFinish([=] { exchanges->stop(); });
runner->add({
	.name = u"widget ops self-check"_q,
	.run = [=] {
		const auto check = Test::RunWidgetOpsSelfCheck(
			Test::WidgetOpsExchangeLeg::Apart);
		for (const auto &row : check.rows) {
			Test::Check(row.passed, row.name, row.details);
		}
		exchanges->start();
	},
	.until = [=] { return exchanges->ready(); },
	.then = [=] {
		for (const auto &row : exchanges->take()) {
			Test::Check(row.passed, row.name, row.details);
		}
	},
	.timeout = Test::kWidgetOpsExchangesTimeout,
});
runner->add({
	.name = u"widget conformance probe check"_q,
	.run = [=] {
		const auto rows = Test::RunWidgetConformanceProbeCheck(
			state->probeFixture);
		for (const auto &row : rows) {
			Test::Check(row.passed, row.name, row.details);
		}
	},
});
```

## Readings and refusals

`ReadWidgetOps(tap)` returns a `WidgetOpsReading` snapshot, safe from any
thread. Counts cover every host built for the message while it was armed
and are never reset; only `trustedEvents`, `trustedClicks`,
`trustedEventTypes` and the press readings (`markedGeneration`,
`markedWork`, `markedSerial`, `markedPress`, `downsConfirmed`,
`upsConfirmed`, `clicksCompleted`, `pressStepsElsewhere`,
`markSuperseded`, `markDetached`) restart at `MarkWidgetOpsEvents()`, which also marks the
generation it finds newest as the interaction they read. Its press is the
first primary press the chat dispatches to a tapped work of that generation
while `DeliverMarkedPress()` delivers the fixture's own mouse-down
(`WidgetPressDispatched()`, bound to its work and pointer serial at that
dispatch); the binding is sealed when that delivery returns, bound or not.
Counts are uncapped;
the example lists (`leakExamples`, `noteExamples`, `gapExamples`,
`violationDetails`, `limitExamples`, `trustedEventTypes`) keep only their
first 64 entries, so
compare a list with its count before calling it complete; `hostedRuns`
keeps the newest 32 (`kWidgetHostedRuns`), and no count is read from it.

| Field | Meaning |
| --- | --- |
| `factoriesSelected` | Hosted taps handed out at the main-thread seam (`TapHostedWidget()`). |
| `factoriesCompleted`, `factoriesThrew` | Hosted taps installed on their host or released uninstalled; a hosted tap never throws, so `factoriesThrew` stays 0. |
| `factoriesReleased` | Hosted taps whose last copy is gone, called or not. |
| `wrappersBuilt`, `wrappersDestroyed` | A host's observers, built when the tap installs them and destroyed with the host, whether or not the runtime kept its renderer ([Hosted widgets](#hosted-widgets)). |
| `hostMessages` | Host to renderer messages of every type: the validated batches and, as the helper's tap mirrored them, the served host's messages its renderer got. |
| `batches`, `ops` | `ops` messages with an `ops` array, and their ops. |
| `scanGaps`, `gapExamples` | Observations not taken, each with its reason and the first 160 bytes: unscanned host messages, host and renderer messages past the scan budget, result values and theme messages too long to keep, helper scheme records too long. |
| `leaks`, `notes` and examples | The copied scanner's own counts, every match, and texts: per message the first 64 within 64 KiB, each place cut to 240 bytes. |
| `violations`, `violationDetails` | Renderer `violation` messages and their `detail`. |
| `limitReports`, `limitExamples` | Renderer `limit` reports, each counted before it is forwarded, and their `code` and `count`: a string code bare, any other code and every count as JSON text, a missing member as `null` (`paint-limit 17`, `paint-limit "17"`). |
| `trustedEvents`, `trustedClicks`, `trustedEventTypes` | Renderer `event` messages with `trusted` true since the mark, those whose `ev.type` is `click`, and their types; of the marked generation alone once a mark was made, and only while no newer generation was selected. |
| `markedGeneration`, `markedWork`, `markedSerial`, `markedPress` | The generation the mark found newest; the work and pointer serial of the first primary press the chat dispatched to a tapped work of that generation inside `DeliverMarkedPress()`, the synchronous delivery of the fixture's own mouse-down (`WidgetPressDispatched()`, called by `RendererCore::pointerPress()`; 0 when none was, and then for good, because the binding is sealed when the delivery returns): the press the fixture played, whatever press the work still had queued; and the number that press got when its work sent it (`WidgetPressObserved()`'s `Sent` carrying that serial; 0 before it): the marked interaction. A press dispatched before or after the delivery, a genuine later click of the same work after a mouse-down that reached no core included, and one to another generation's work bind nothing. |
| `downsConfirmed`, `upsConfirmed`, `clicksCompleted` | Of the marked interaction, while the marked generation is the newest: its down and its up answered by the helper's renderer while the route was still active (delivery only), and the renderer's click decision for that up, which the helper reports with the `Clicked` control after the up's reply, counted only after the up. A down or up, even both answered, is never a click: an up whose target the down's handler replaced gets its reply and no decision. `clicksCompleted` is the click reading of a Release harness, whose `trustedClicks` stays 0; with the Debug tap `trustedClicks` counts the generation's trusted clicks whatever press made them, and only `clicksCompleted` is the played press's own. |
| `pressStepsElsewhere` | Steps of every other press since the mark: a press the chat dispatched before the mark that the work sends after it, an earlier press of the same work that completes after it, a later press (also after a sealed, unbound mark), an older generation's press on the same tap, a click before its up, every step of the marked press after a newer generation was selected, and every step of it after its core was detached or its host retired (`markDetached`). |
| `markSuperseded` | The generation whose selection superseded the marked one, 0 while the marked one is the newest: the mark's later steps and trusted events stop counting, and what it counted before stays. |
| `markDetached` | Whether the core of the work the mark is bound to was detached (`WidgetCoreDetached()`, first in `RendererCore::detach()`, before it closes the host link and before the runtime releases the route or retires the host) before the played press's click completed, or the runtime retired that work's host (`WidgetHostRetired()`) before the helper reported its renderer's click for the played press, while the widget render thread, which learns of the detach only later, can still report the up and click of a reply it already took. Either way the mark is revoked, its later steps count elsewhere and no click of it completes it, also one a lookup that held the registration brings later, while what it counted before stays. A click completed before keeps it false. |
| `ackQueries`, `ackResults` | The host's post-load `query` and the renderer's `result`, each with numeric `qid` -1 (`WidgetHost`'s `kRenderAckQid`). |
| `fatalFrozen` | Hosted works whose final the runtime's accounting charged as an ordinary or a stalled death. Never a death a host reported by itself (its `onDeath` or its diagnostics' `dead`), nor the served host's mirrored `frozen` ([Hosted deaths and final charges](#hosted-deaths-and-final-charges)). |
| `hostedSelected`, `hostedFinal` | Hosted works selected for the runtime's accounting (`TapHostedWidget()` with its `finish` out-parameter), and those whose final reached the tap, each once, after the runtime's accounting applied it. |
| `desktopCharges`, `sparedRuns`, `unaffectedRuns` | The other finals: a refusal Telegram itself made of the widget's own traffic (`Refused`), also on a route the stream spared; a run the stream spared that nothing charged (`Spared`); a run nothing charged or spared (`None`: withdrawn first, or never part of an end). Each final counts in exactly one of these or `fatalFrozen`. |
| `hostedRuns` | The newest 32 hosted selections (`WidgetHostedRun`), in selection order, for attribution: a work's verdict is never read from its record's absence. |
| `renderedHosts` | Hosted hosts whose own `WidgetDiagnostics::rendered` the tap saw, each once: the served host's post-load query answered by the helper's renderer, as the hosted client recorded it. The hosted twin of an `ackQueries` / `ackResults` pair, and the one a Release harness has. |

The message shapes are the ones `WidgetHost` sends and reads in widget-sandbox's
`widget_host.cpp` (`toRenderer()`, `onLoaded()`, `onDied()`,
`handleRendererMessage()`), with `docs/protocol.md` for `RenderOp`: a
validated `el` carries `attrs` as `[name, value]` pairs, which is what the
scanner reads. TwidgetCore marks every event it forwards `trusted` except the
`pointerdown` and `mousedown` of a real finger's press (widget-sandbox
v0.3.0's finger rule); mouse presses and a tap's `click` stay trusted.

A reading is final only when `WidgetTapFinished(reading)` holds:
`factoriesReleased == factoriesSelected`, `wrappersDestroyed ==
wrappersBuilt` and `hostedFinal == hostedSelected`. Then no hosted tap
selected for the message can still be installed, every host it was
installed on is destroyed, so none of its observers can record more, and
every hosted work selected for the runtime's accounting had its final; a
renderer the helper kept past that host is outside the reading.
`wrappersBuilt == wrappersDestroyed == 0` alone is not that: a Build worker
may not have installed its tap yet. Nor are balanced factories and
wrappers: a work's final can come after its host is gone, since the final
observation holds neither. Readings before that point are diagnostic. A
wait's bound decides nothing by itself: a host can finish between the
wait's last poll and the caller's next snapshot, so the snapshot read
after the wait decides, one taken at or past the bound included, and
only one that is not final is incomplete; the fixture's removal barrier
reports it with `WidgetTapCounts()`
([test_widget_fixture](test_widget_fixture.md#readings-and-refusals)).

### The latest result

`ReadWidgetOpsLatest(tap)` returns a `WidgetOpsLatest` snapshot taken under
the same lock, safe from any thread. Every field but `serial` and
`staleUpdates` describes the host of `generation` alone. `stress` is copied
whole, under the lock the host thread merges into: a level's few hundred
short values cost little, but a reader polling far more often than the
widget publishes adds that copy each time.

| Field | Meaning |
| --- | --- |
| `generation` | The 1-based count of hosted taps selected for the message when the newest was; 0 before any. |
| `adopted`, `htmlId` | Whether an `el` op with `"adopt":"html"` reached this host's renderer, and its `id`. |
| `conformance` | The last `data-conformance` value: the adopting op's attribute, replaced by each later `attr` op on `htmlId`. Empty before the first value, from a newer selection on, and after a later adoption without one. |
| `updates` | `data-conformance` values this host published; the probe fixture's `updates`. A clear is not a value. |
| `serial` | Advances on every change of `conformance`, a clear included; never reset. A `data-stress` value never moves it. |
| `stress` | Every `data-stress` value on `htmlId` (`value`, raw) with its `receivedAt`, in order: the adopting op's attribute, then each later `attr` op. Cleared by a later adoption and a newer selection. |
| `stressBytes` | The bytes of the kept `stress` values, at most 4 MiB. |
| `stressDropped` | `data-stress` values that found `stress` holding `kWidgetStressValuesLimit` (4096) or 4 MiB; nonzero means `stress` is incomplete. |
| `batches`, `ops`, `maxBatchOps` | This host's `ops` messages, every one counted; their ops (`int64`); the ops of the largest. Restart with a newer selection only. |
| `scheme` | The helper tap's report of the scheme this host's renderer was built with, then of each `setTheme()` (way `b`, then `t`); empty before it. In a Release harness `effectiveScheme` of the newest kept effective reading, never the theme the runtime asked for: empty before the first. |
| `schemeChanges` | `setTheme()` calls of this host that changed `scheme`, as the helper's tap reported them; a repeated scheme is not counted. In a Release harness, kept effective readings that changed it (the first sets it uncounted). |
| `themeMessage` | The raw `{"type":"theme",...}` message the served host last sent its renderer (way `h`): the bubble's palette, button and text tokens. In a Release harness `{"type":"theme","theme":{"colorScheme":...,"params":...}}` built from the newest kept effective reading, with no button or text tokens, which a widget cannot read back. |
| `appliedScheme`, `appliedThemeMessage` | The theme the runtime applied to this host, recorded once the application returned: the one its constructor got, then each one `ApplyTheme()` gave it. The scheme as the host keeps it (`dark`, else `light`) and the theme message that theme gives the helper's renderer, built with the SDK's normalization as `WidgetHost::themeMessage()` builds it (the checked palette under the 15 theme keys in their order, then the set button and text tokens). The expectation, never a reading of the widget. |
| `appliedThemes` | Themes the runtime applied to this hosted host, its construction's included: the serial of the newest. |
| `effectiveScheme`, `effectiveParams` | Of a hosted host, what the widget itself read after the runtime applied a theme, probed in it (`TelegramWidget.colorScheme` and the JSON of `TelegramWidget.themeParams`): for the constructor's theme by the runtime's job `WidgetThemeObservation()` gives it, once the widget loaded, for a later one right after `WidgetThemeApplied()`'s application ([The effective theme](#the-effective-theme)). Kept only for the newest applied theme. |
| `effectiveMismatch` | Empty when the kept reading has the scheme of the theme it was probed for and, for each key of that theme's checked palette, its value; else the first difference. |
| `effectiveError`, `effectiveMissed` | The last probe that kept no reading, with its reason and the start of the answer, and how many there were; an application or a reading its host published after the runtime retired it counts here too. |
| `effectiveThemes`, `effectiveSerial` | Effective readings kept for this hosted host, and the `appliedThemes` serial the newest was probed for. |
| `themePending` | Of a hosted host, the token of the application of the theme the runtime wants it to have and has not yet recorded, from the moment the runtime selected that theme (`WidgetThemeSelected()` while another theme's job runs, else `WidgetThemeScheduled()`), 0 when none is underway. The host may already read part of the new theme while it is set, or still be given an older one, so the generation's theme is unobserved however good its last reading was ([The effective theme](#the-effective-theme)). |
| `hostRetired` | Of a hosted host, whether the runtime retired it (`WidgetHostRetired()`): no application or reading its jobs publish after that is kept. |
| `staleUpdates` | Messages with an adoption, a `data-conformance` or `data-stress` attribute or a theme, `setTheme()` calls and the helper tap's scheme reports of an older host after a newer one was selected; ignored above. An older host's batch without such an op is not counted here. |

The result fields follow `CLIENTS.md` and twidget's reference runner
(`tools/conformance`, `OffscreenRenderer::probe()`), apart from two
deliberate clears: a newer host's selection and a later value-less adoption
leave no value, where a reader that only remembers the last published value
would keep the older one. Neither happens in a healthy run; a caller that
sees `generation` above 1 names it rather than trusting a result or a
runtime log to cover every host.

## Hosted widgets

`Tgw::Runtime::construct()` builds a hosted `WidgetHost` for every widget:
its engine, validator, served host and twidget renderer run in the
widget helper (macOS, Linux and Windows, the Mac App Store and Windows
Store builds included;
[Remote engines](test_widget_fixture.md#remote-engines)), and Telegram
keeps only the hosted client and its checked Qt work. The runtime
calls `TapHostedWidget(_item->globalId(), _live->workId(), *_theme,
&run->observe)` in `Runtime::construct()`
on main, with the `RendererCore::workId()`
of the core it builds the host for (counted up from 1 on main and never
reused, so no later core shares it), the theme its `Job` carries into
the host's constructor and the final observation slot of the
construction's run. For
an armed id it selects the next generation at once, starts that
selection's record, sets the slot to the selection's final observation
([Hosted deaths and final charges](#hosted-deaths-and-final-charges)) and
returns a function
that `Build()` calls on its worker, once, with the hosted host's shared
pointer, after its constructor returned and the runtime set its
observers, before the host is bound or started. That call records that
theme as the generation's first applied theme (`appliedScheme`,
`appliedThemeMessage`, serial 1), registers the host and the work id for
the runtime's three other hosted seams below, and
wraps three observers of that host, each recording first and then calling
the runtime's own. The host's `onDeath` stays the runtime's own,
unwrapped: the tap counts no death a host reports, only the runtime's
final decision of the work
([Hosted deaths and final charges](#hosted-deaths-and-final-charges)).

| Observer | What the tap records |
| --- | --- |
| `onRenderOps` | Every validated batch, as the helper's validator passed it, rebuilt into the host's own `{"type":"ops","ops":...}` message and scanned: leaks, notes, results, `data-stress` values with their receipts and the newest host's batch counts. Set before `start()`, it is also the hosted start's ask for the batches, which a host without the observer never gets. The served host's `WidgetHost::localSetTheme()` reports the batch a theme change re-sanitises the stylesheets into as well, when it is not empty. |
| `onPlatformMessage` | The helper tap's `Mirrored` records, consumed only when well-formed and only after the tap asked for them: way `b` (the scheme the renderer was built with, the latest `scheme`, once), `h` (every served-host message to the renderer but `ops`, such as the theme message, the ack query, layout queries, `requestFrame` and `frozen`), `r` (every renderer message to the served host, such as the ack's `result`, events, `violation`, `limit` and `size` reports) and `t` (each `setTheme()`). Every other message, a malformed `Mirrored` record included, goes to the runtime's observer unchanged, in order, inside the same call, so `WidgetHost::platformDelivery()` still gives it its delivery. |
| `onChange` | The host's own rendered acknowledgment: the first change whose diagnostics have `rendered` counts one `renderedHosts`; the observer probes nothing ([The effective theme](#the-effective-theme)). The first change whose diagnostics have `dead` keeps that death raw, cut by `Tgw::ClipLogLine()`, in the selection's record (`WidgetHostedRun::died`), also after the final and also when the host's owner kept or destroyed it so that its `onDeath` stays silent; it counts nothing. Every change is passed on unchanged; the watch of [test_widget_host](test_widget_host.md), when armed, wraps this observer in turn. |

The runtime's other hosted seams reach the installed tap through a
registry of the hosts and work ids it was installed for, each entry living
as long as the host's observers:

| Seam | Production call | What the tap records |
| --- | --- | --- |
| `WidgetThemeObservation(host)` | `Runtime::constructed()` in `tgw_runtime.cpp`, on main, for each host it starts; the function it returns, kept in the runtime's `Host`, runs once as one of that host's jobs on a worker after the widget loaded (`Runtime::syncHostTheme()`), never beside a theme application or a keep of that host, and is dropped unrun when a newer theme or the host's retirement makes it obsolete | An empty function for a host with no installed tap. Run, the widget's own reading of the theme the host was built with, for serial 1 ([The effective theme](#the-effective-theme)); a reading after a newer theme was applied is superseded, and one that comes back after the host's retirement is revoked: neither is kept. |
| `WidgetThemeSelected(host, theme)` | `Runtime::syncHostTheme()` in `tgw_runtime.cpp`, on main, instead of starting a job while the host's theme job still runs (its busy guard), with the theme the runtime now wants; the job for it starts when the running one ends, or never when the running one gives it | Nothing for a host with no installed tap or a retired one; else the application that gives `theme` is pending from now on (`themePending`): the running job's when that job gives `theme` (none when it already recorded its application), else a new token, which `WidgetThemeScheduled()` hands to the job for `theme` when it starts. An application of another theme, the running job's included, never ends it. |
| `WidgetThemeScheduled(host, theme)` | `Runtime::syncHostTheme()` in `tgw_runtime.cpp`, on main, right before it starts the job that gives the host a new theme (`ApplyTheme()`), which carries the returned token | 0 for a host with no installed tap or a retired one; else the token, unique in the tap, of an application now pending (`themePending`, with the theme message it gives; the token `WidgetThemeSelected()` made pending for that message, when it still is): the theme is unobserved from now on until that application is recorded. |
| `WidgetThemeApplied(host, theme, token)` | `ApplyTheme()` in `tgw_runtime.cpp`, on its worker, right after it gave the host the new theme behind its barrier (an apply, a probe, an apply again), with the job's token | The applied theme, one more `appliedThemes` (ending the pending application only when `token` is the pending one and `theme` gives its message, so an older job never ends a newer one), then, on that worker with no lock held, the widget's own reading of it for that serial ([The effective theme](#the-effective-theme)); after the host's retirement neither; a host with no installed tap (another message's) records nothing. |
| `WidgetHostRetired(host)` | `Runtime::retire()` and `~Runtime()` in `tgw_runtime.cpp`, on main, when they give up the live host, before its last jobs end (the keep still waits for an active theme job) | `hostRetired`, under the tap's lock: what a job of that host records later, an application or an effective reading, is not kept and counts in `effectiveMissed`, so a probe already running at the retirement cannot make the generation's theme observed. Under the same lock it revokes a mark bound to the host's work whose click was not reported yet (`markDetached`): a step `WidgetPressObserved()` reports later counts in `pressStepsElsewhere` alone. Readings kept before stay, a completed click included; another generation's host changes no mark. |
| `WidgetPressDispatched(work, serial)` | `RendererCore::pointerPress()` in `tgw_renderer.cpp`, on main, for every primary press it dispatches to its work, right before it posts it, with the core's `workId()` and the pointer serial it gave the press; the chat's press handling reaches it inside the mouse-down's own `sendEvent()` | While `DeliverMarkedPress()` delivers the mark's mouse-down, the first such press to a work of the marked generation binds `markedWork` and `markedSerial`: the press the fixture played, whatever press that work still had queued. Outside that delivery nothing binds; the binding is sealed when it returns. |
| `WidgetPressObserved(work, press, step)` | `HostedInput` in `tgw_renderer_input.cpp`, through the delegate `RendererWork::makeHosted()` gives it, on `widget.render`, with the work's id and the press's number (the count of downs both Telegram and the helper's glue keep, `RendererPlatform::lastDown()` at its down): `Sent` in `press()` once the primary down went out on the live route, carrying the pointer serial the chat dispatched it with (`Tgw::PressStep::serial`), `Down` and `Up` in `pointer()` when it takes the helper's Pointer reply to that down and to the up that ended it while the route is still active, `Click` in `clicked()` when the helper's `Clicked` control names that press right after its up's reply | The step of the tap installed for that work id, counted only for the marked interaction while its generation is the newest (`markedPress` from the `Sent` with the bound serial, then `downsConfirmed`, `upsConfirmed`, `clicksCompleted`) and otherwise in `pressStepsElsewhere`. A down sent but never answered, a press cancelled or taken by the chat, a finger or secondary press, and a reply or report taken after the route closed, detached or stopped give no step. |

In Debug the call then sends one `Mirror` control through `sendPlatform()`
(`tgw/tgw_hosted_wire.h`), or `MirrorWork` when `TimeWidgetHelperWork()`
asked ([Helper renderer work](#helper-renderer-work)): the helper's tap
swallows it and from then on mirrors. Platform messages sent before the
hosted start reach the helper's glue thread before its renderer loads, so
the tap is armed before the first served-host message. A Release build with
the harness starts a Release helper, which has no tap and would end the
route at a `Mirror`, so it asks nothing: its readings have the validated
batches, the leaks, `renderedHosts`, the applied and effective themes, the
marked interaction's press steps, the raw deaths and the final charges
only, and
`ackQueries`, `ackResults`, trusted events, `violations` and
`limitReports` stay 0.

#### Which reading each build uses

`WidgetHelperMirrors()` says which build this is: true in Debug, false in a
Release build with the harness.

| Reading | Debug (the helper's tap mirrors) | Release with the harness |
| --- | --- | --- |
| Load | The mirrored ack pair (`ackQueries`, `ackResults`) or `renderedHosts`: the attack audit's `Loaded()` and the conformance runner's `HostLoaded()` take either. | `renderedHosts`. |
| Theme | `scheme`, `schemeChanges` and `themeMessage` from the mirror (what the renderer in the helper got), with the effective readings beside them; every hosted reader judges the effective reading with `CheckWidgetTheme()`, and the stress runner also requires the mirror to say the same. | The effective readings alone fill `scheme`, `schemeChanges` and `themeMessage`; the applied theme is only their expectation. `CheckWidgetTheme()` is the one predicate of the stress runner, the conformance runner's load reading and its theme switch: Unobserved without a reading of the newest applied theme (`WidgetThemeUnobserved()`), superseded by a newer generation, with an application still pending (`themePending`) or, for a switch, with no newer application; Wrong when the runtime applied another scheme or the reading differs from the applied theme, a wrong palette key with the right scheme included. |
| Click | `trustedClicks` of the marked generation from the mirrored renderer events and `clicksCompleted`, the marked press's click decision, both, while no newer generation was selected (the attack audit's `observed`). | `clicksCompleted` of the press played to the clicked core (`markedWork`, `markedSerial`, `markedPress`): the attack audit's `decided` outcome (`click.source` `click-decision`), which completes a live card's click as `observed` does; `downsConfirmed` and `upsConfirmed` never do. |

What the readings rest on:

- **Finality.** The installed observers count as one wrapper built at the
  install and as destroyed with the host, in `~WidgetHost` on its
  `HostDeleter` worker after its host thread was joined, whether or not
  the runtime kept the helper's renderer before that: the generation's
  observations end with the host. A kept route's own observers belong to
  the runtime; any record the helper's tap still mirrors for the kept
  document reaches the kept route's platform end, which drops it without a
  parse, and never the tap. The tap's function, its last copy gone, counts
  as released, and when it was never called (a build that threw or was
  discarded before the install) also as completed; `factoriesThrew` stays 0.
  A hosted work's final is no observation of its host and can come after
  the host is gone
  ([Hosted deaths and final charges](#hosted-deaths-and-final-charges)).
- **Order and time.** Each record crosses the stream after the helper did
  the work, so a renderer message that a batch caused can precede that
  batch's `onRenderOps` record, and every receipt (`receivedAt` of a
  `data-stress` value) is taken on Telegram's `twidget-host` thread after
  the helper's validation and both stream hops.
- **What is not seen.** An empty re-themed batch is neither reported nor
  mirrored, so it never counts as a batch. A served-host or renderer message too
  large for one `Mirrored` record (past `kMaxMirroredTextBytes`, 32 MiB
  less the record's 7 bytes) is not mirrored. The tap never sees the
  client's grant decisions, which `WidgetHost::sendPlatformPress()` makes on
  the client's dispatch thread when the stream's writer admitted the down
  ([Remote engines](test_widget_fixture.md#remote-engines), *Presses and
  grants*); the press readings count the helper renderer's answers to the
  downs and ups Telegram sent and its click decisions, never a grant.
- **Cost.** The mirror copies every non-`ops` served-host message and
  every renderer message once more over the stream, counted against the
  route's client bounds like any platform message, and parses each again
  on Telegram's host thread. It runs only for armed messages in Debug. An
  asked tap adds one 48-byte record per helper operation and two clock reads
  per operation and per backend request in the helper, inside the timed
  intervals.

#### The effective theme

A hosted widget's theme counts only as the widget itself read it, after
the runtime applied it, in that generation. The tap asks the widget with
`host->probe("JSON.stringify([TelegramWidget.colorScheme, TelegramWidget.themeParams])")`
on the runtime's per-host job worker, where one job at a time reads or
re-themes the host and a keep waits for it (`Runtime::syncHostTheme()` and
`Runtime::Sample()` in `tgw_runtime.cpp`): for the constructor's theme by the
function `WidgetThemeObservation()` gave the runtime, which runs once after
the widget loaded unless a newer theme or the retirement dropped it first,
and for each later theme right after the application in `ApplyTheme()`.
The answer is kept, for that theme's serial, only when it is a
`[string, object]` pair within 64 KiB, the serial is still the newest
applied one and the runtime has not retired the host; an `ERROR` or `THREW`
answer, any other shape, a longer answer, a reading a newer application
superseded and one that comes back after `WidgetHostRetired()` keep nothing
and count in `effectiveMissed`, with the reason in `effectiveError`. The
retirement is recorded under the tap's lock and the commit checks it under
the same lock, so a probe still running when the runtime retired its host
(the keep waits for that job, but nothing else stops it) cannot certify the
retired generation: without a newer factory its selection and applied
serial would still be current. An application is pending from the moment
the runtime selects its theme, on main, until the job with that token
records it after the barrier: `WidgetThemeScheduled()` when the runtime
starts the job at once, `WidgetThemeSelected()` when its busy guard holds a
newer theme back because another theme's job still runs. The host may have
taken the new theme's first apply already, or still be given the older
theme, so meanwhile no earlier reading counts, even one of the same scheme
with no mismatch and the fresh serial of the running job's application; the
next reading kept that ends it is the selected application's own. An older
job's application, whose token is not the pending one, records its theme
but never ends the newer application. A selection that returns to the
running job's theme makes that job's application the pending one again, and
none is pending when that job already recorded it, since no job is still to
start. The comparison is
built with the SDK's normalization: the theme's scheme as a host keeps it,
and every key of its palette after `sdk::checkThemeParams()`; the widget
completes the other keys from its scheme's defaults, which the C++ SDK does
not expose, so those are not compared. A kept reading that differs is still
kept, with the first difference in `effectiveMismatch`, and in a Release
harness it still sets `scheme`, so a dropped application shows as a scheme
error. `WidgetThemeUnobserved(latest)` is empty only when `effectiveSerial
== appliedThemes`, at least one applied, and no application is pending;
otherwise it returns `theme-pending: ...` while one is, and
`theme-unobserved: ...` with the counts and the last error for a missing,
delayed, closed, superseded or revoked probe alike. `CheckWidgetTheme(latest,
scheme, before)` is the one predicate every hosted reader applies to that
reading (the stress runner, the conformance runner's load reading and its
theme switch): Unobserved when `WidgetThemeUnobserved()` is not empty or,
with `before` (the reading a switch began with), when a newer generation
replaced the one it began with (`theme-superseded`) or no theme was applied
after `before` (`theme-unapplied`); Wrong when the runtime applied another
scheme than `scheme`, or the kept reading differs from the applied theme
(`effectiveMismatch`: a wrong palette key with the right scheme too);
Matches otherwise. An application request, an app theme switch or an
earlier good reading never makes it Matches.

### Hosted deaths and final charges

Every host of an ended widget helper reports the same raw death, `the
widget engine process ended`, whether the stream spared its widget or
charged it, so the tap counts no death a host reports. It counts the
runtime's own decision for each hosted work; what decides it and what it
does to the widget is the fixture's
([Remote engines](test_widget_fixture.md#remote-engines), *Deaths* and
**Runtime log and deaths**).

- **Selection.** Each `TapHostedWidget()` selection of an armed id starts
  its record in `hostedRuns` (`WidgetHostedRun`: `work`, and `generation`,
  the selection's index), the oldest dropped past `kWidgetHostedRuns` (32).
  With the `finish` out-parameter, which `Runtime::construct()` passes for
  every hosted construction and a check that only installs leaves out, it
  also counts one `hostedSelected` and sets `*finish` to the selection's
  final observation.
- **The final.** The runtime keeps that observation with the
  construction's run (`HostedRun::observe` in `tgw_runtime.cpp`), takes it
  once and calls it on main from the run's one final accounting
  (`Runtime::FinishRun()`) with the run's `Tgw::HostedDecision`, installed
  or not, started or not, and never from the host's `onDeath`. The call
  follows the update of the message's crash history when one applies
  (`Runtime::ChargeRun()`, which a run whose message memory is gone skips
  altogether, and which leaves the count alone for HTML an edit replaced
  or a count at the two-crash cutoff); it counts the decision either way:
  one `hostedFinal` and exactly one of `fatalFrozen` (`Ordinary`,
  `Stalled`), `desktopCharges` (`Refused`), `sparedRuns` (`Spared`) and
  `unaffectedRuns` (`None`). It holds the tap weakly, the selection's
  number and the work id, and nothing of the installer, the host or its
  observers; a second call of it or of any copy does nothing
  (`HostedFinish()` in `test_widget_ops_hosted.cpp`), the tap's own guard
  and no proof of the runtime's once-only accounting.
- **Per work and cumulative.** While its record is kept, the final also
  fills it: `outcome`, the charge, and apart from it `transport`, what the
  deciding event read of the stream's decision for the route, so a
  refusal on a route the stream spared reads `refused/spared` and counts
  in `desktopCharges`, never in `sparedRuns`; `reason`, the decision's
  own (the stopped-responding text, an ordinary death's label, Telegram's
  refusal); `claimed`, a charge decided with no end of the stream to read
  (transport `None`); and `final`. An engine's own death on a live helper
  reads transport `Ordinary` as an ordinary end does, so an `Ordinary`
  transport is no proof that the stream published an end. `died`, the
  first death the host's `onChange` carried, is kept raw beside them.
  Texts are cut by `Tgw::ClipLogLine()`, as a summary's `Died: ` line is.
  Eviction drops a record and never a count: a charge whose record is gone
  still counts.

`ReadHostedDeath(reading, work, deathLine, earlier)` is the one hosted
reading of a death, the conformance runner's and the attack audit's:

- **Verdict.** Dead when any selected work was charged: `fatalFrozen` or
  `desktopCharges` positive (`WidgetTapCharged()`), or the sampled work's
  record final with a charge. Read from a final reading
  (`WidgetTapFinished()`) it is final, and no eviction reverses it. A raw
  `Died: ` line is never a verdict.
- **Sample.** `work` is the work the summary was sampled from
  (`WidgetRuntimeLog::summaryWork`) and `deathLine` that summary's raw
  `Died: ` line, kept verbatim in `WidgetHostedDeath::deathLine` beside the
  sampled record (`sampled`). Both packs pass them only for a `Fresh` or
  `Final` summary (`WidgetLogSummaryOwn()`,
  [test_widget_fixture](test_widget_fixture.md#readings-and-refusals)),
  and otherwise work 0 and no line, keeping a `Stale` summary's line and
  work in their rows as diagnostics.
- **Cause.** Only the sampled work's own record gives one, final and
  charged: a `Stalled` or `Refused` final the reason recorded with it, also
  over a healthy sample or a sample that shows the end of a helper that
  spared it; an `Ordinary` final that host's own raw diagnosis, the trimmed
  reason of `deathLine` or, when that is empty, the record's `died`,
  unless what it takes is empty or `terminated`. A spared, unaffected,
  unfinished, evicted or absent sample gives none, so a dead reading stays
  unmeasured: `WidgetHostedDeathText()` gives
  `charged, reason unmeasured`. `earlier`, a reading of the same nonzero
  `work` made while its record was kept, keeps its measured reason once
  the record is evicted; another work's reason is never used.

`WidgetTapHosted(reading)` (`hostedSelected` positive) says whether a
message's death is read this way, from its own selections rather than the
runtime's engine placement. `WidgetHostedOutcomeText()` and
`WidgetHostedRunText()` give the rows' texts: a record reads
`<generation>:<outcome>/<transport>`, then `/claimed` and `/open` (before
its final) where they apply, its work, its reason and its raw death (`-`
for none).

`WidgetHostedPlay` plays a hosted construction as `Runtime::construct()`
makes one, for self-checks only: selected with its final observation,
installed or not on a stub host of its own (no channel, never started, its
`onDeath` standing for the runtime's), whose `change()` reports a raw death
as a host of an ended helper does; `finish()` takes the observation once
and calls it, `observation()` copies it as a second holder would, and
`drop()` releases the installer and the host. `selected()`, `install()`
and `drop()` read the tap around their step, so a play whose step did not
happen says so. The finals leg below, the conformance runner's death
reading checks and the attack audit's death verdicts use it; it never
reads a real widget.

### Helper renderer work

The work a hosted widget's renderer does in the helper (its receive, style,
layout, ticks and display-list builds) is outside Telegram's work log
([test_widget_host](test_widget_host.md#the-helpers-share)). The
helper's Debug tap times it when asked, and this tap keeps it:

- **Ask before construction.** `TimeWidgetHelperWork(*tap)` on main, after
  arming and before the message's host is constructed, makes the install
  send `MirrorWork` instead of `Mirror`. A host built before the call, a
  Release helper and an unasked tap send no record: the unarmed control.
- **Reserve per window.** `ReserveWidgetHelperWork(*tap, capacity)` forgets
  the events and drops and makes room at once; a record that finds the room
  full counts in `dropped`, a malformed one in `dropped` and `malformed`,
  and a nonzero `dropped` leaves the reading incomplete.
- **Malformed.** A record that does not decode (`DecodeHelperWork()`:
  exactly 41 bytes, a known kind, `0 <= started <= finished`, at most an
  hour, `kMaxHelperInterval`, between them, `0 <= waited <=` that
  interval; [test_widget_renderer_forward](test_widget_renderer_forward.md#the-helper-work-record)),
  that finished after its own receipt (the helper reads the same steady
  clock before it sends a record, and the tap compares `finished` with its
  receipt's steady reading before it maps anything, so a short, ordered
  record that ends in the future is malformed rather than filtered out of a
  window later), or whose stamps do not map onto Telegram's clock (the
  offset between the two clocks and each moved stamp are checked against
  the `int64` range), is malformed and never kept, so `finished - started`
  of a kept event is exact, 0 to an hour.
- **Read.** `ReadWidgetHelperWork(*tap, since)` copies the events that
  finished at or after `since`, under the tap's lock. Each
  `WidgetHelperWorkEvent` is one outermost operation of the glue thread
  ([test_widget_helper_tap](test_widget_helper_tap.md), way `w`): its
  `kind`, `started` and `finished` mapped to `crl::profile()` at the
  record's receipt (the helper's steady clock is the system-wide uptime
  clock on macOS and `QueryPerformanceCounter` on Windows, both read by
  Telegram too, so the mapping is the two processes' offset taken then),
  `waited` and `waits`, the part spent waiting for Telegram's answers to
  backend requests, `seq` of a tick or paint, `frameBytes` and the tap
  `generation`; `waits` and `frameBytes` stay unsigned 32-bit values as
  the record carried them. A reader that sums the events' times adds them
  with `CheckedTimeSum()`, which returns nullopt instead of an overflow, as
  the stress runner does.
- **Attributing it.** Telegram's own events of the same window (its
  answers on the render thread and the replay of each checked list it
  paints, when an overlay times them) overlap the helper's `waited`
  intervals: a total of both processes counts the helper's
  `finished - started - waited` and Telegram's events, or the helper's
  wall time without Telegram's answers, never both. A `Paint` with `frameBytes` above 0 sent the `Frame` or `FrameDelta` whose
  replay is the next `Frame` event of that host's work log after its
  `finished`, its `seq` the one Telegram's request carried. A stress level
  attributes the renderer's work only from a known nontrivial workload with
  both readings nonempty, against an unasked level of the same widget.

## Threads and lifetime

- `TapHostedWidget()`, `ArmWidgetOpsTap()` and `DisarmWidgetOpsTap()` run on
  the main thread, as `Runtime::construct()` does; the armed map, keyed by
  `GlobalMsgId`, is main-only.
- The tap's record is one mutex shared by every thread that records.
  Parsing and scanning happen outside it, and nothing calls out while
  holding it, so no lock taken before it has a reverse path.
- A hosted tap's function runs on the Build worker before the host is
  published, so its `sendPlatform()` meets no other sender and no keep. The
  observers it installs run on the hosted host's `twidget-host` thread, in
  the client's arrival order, and share one record of that host, which only
  that thread touches until the host's destructor releases it; they hold
  the tap and that record, no `WidgetHost` pointer. They call the runtime's
  own observer on the same thread, after recording, and never take a
  runtime lock.
- The registry of tapped hosts and works has its own mutex, never destroyed
  (a host's deleter worker or the render thread may still reach it at
  exit). The install registers the host's address and the core's work id
  under it (work 0 is never registered), and the record's destructor
  erases each entry only while it is still its own; the theme seams (the
  observation function and `WidgetThemeApplied()`, on the runtime's
  per-host job worker) look the host's address up and the press seam
  (on `widget.render`) the work id, each holds the record past the lookup
  and then takes only the tap's mutex, so a record they held across the
  host's destruction is released, and its wrapper counted destroyed, on
  their thread. A work id is never reused, so a late install for a core
  that is already gone takes no newer core's entry and its erase removes
  none, whichever of two constructions installs first. The host's address
  is a key only, never dereferenced, and is registered only while that
  host lives; a lookup without an installed tap takes the registry mutex
  once and records nothing.
- The record keeps no host. Both probes run through the host the runtime's
  job holds (the observation function keeps the record only weakly), one
  job at a time per host, never beside another probe of it or a keep that
  moves its engine; no lock of the tap or the registry is held while a
  probe waits.
- A hosted work's selection and its final observation run on main, the
  first death a host's `onChange` carries on that host's thread; each takes
  only the tap's mutex, any text cut before it. The final
  observation holds the tap weakly: it records while anything still holds
  the tap (the caller, the armed map, an installer or an installed host's
  observers) and nothing once none does.
- `WidgetOpsExchanges` owns the exchange leg's thread: `start()`, `ready()`,
  `take()` and `stop()` run on main; `ready()` acquires what the leg's
  thread wrote before it, `take()` joins that thread once ready, `stop()`
  raises the leg's halt without waiting, and the destructor stops and
  joins it.

## Lifetime and cleanup

`ArmWidgetOpsTap()` returns a shared tap that the armed map, every hosted
tap's function and every installed host's observers keep.
`DisarmWidgetOpsTap()` only stops future
selections; keep the caller's tap until its reading is final, the last
hosted final included, since the runtime's final observation holds no
strong reference to it. Nothing is
written to the account, the disk or the clipboard. A scenario that arms ids
disarms them on its finish path (`Runner::onFinish`); an armed id that is never
constructed holds only its empty record. A stage that plays the exchange
leg apart keeps its `WidgetOpsExchanges` in its captures and calls `stop()`
from `Runner::onFinish`: once stopped, the leg starts no stream, each open
stream's waits give up within 50 ms and it closes, and its rows are never
given.

## Leak definition and fidelity

The scanner is widget-sandbox's leak definition in
`packages/desktop/harness/main.cpp` at the `v0.1.2` tag, from its "Anything
in the validated ops" comment to the end of `scanForLeaks()`, in the
`Upstream` namespace of `test_widget_ops.cpp`: its predicates, prefixes and
the 160- and 120-byte cuts are that file's, `static` kept, with `using
namespace twidget` as in that file. Telegram does not build that console
tool. Only the output differs: each match goes to a `ScanSink` as string
views instead of into a vector, and each place built from a widget's name
goes through `Where()`, cut to 240 bytes, so the tap builds no example past
the bounds above. Compare the predicates with
`git -C Telegram/ThirdParty/widget-sandbox show v0.1.2:packages/desktop/harness/main.cpp`
at every widget-sandbox pin move, and carry a change over into this form. The scan
sits at the hosted host's `onRenderOps`, as the reference `--attacks` run's
does, and also sees the theme-replay batches the reference never sees,
because the served host reports every non-empty validated batch there.

## Release and inactive identity

The production calls are in `Tgw::Runtime::construct()` (`tgw_runtime.cpp`),
one per construction, each passing `_item->globalId()`:
`TapHostedWidget()` with the core's work id, the theme and its run's final
observation slot, whose function `Build()` calls once on the constructed
host when it is not empty, and whose observation `Runtime::FinishRun()`
takes and calls once; then
`WidgetThemeObservation()` once per started host in `Runtime::constructed()`,
`WidgetThemeScheduled()` in `Runtime::syncHostTheme()` right before each
theme job starts and `WidgetThemeSelected()` there when a running theme job
holds the next one back, `WidgetThemeApplied()` once at the end of every
`ApplyTheme()` (`tgw_runtime.cpp`), after its barrier, with that job's
token, `WidgetHostRetired()` in `Runtime::retire()` and `~Runtime()` when
they give up the live host, `WidgetPressDispatched()` in
`RendererCore::pointerPress()` (`tgw_renderer.cpp`) for every primary press
it posts, `WidgetPressObserved()` from the hosted input's delegate
(`RendererWork::makeHosted()` in `tgw_renderer_work.cpp`), and
`WidgetCoreDetached()` in `RendererCore::detach()` (`tgw_renderer.cpp`)
for every core. Without the
harness, in a build that is neither Debug nor configured with
`TDESKTOP_RELEASE_TEST_HARNESS`
([Release builds with the harness](../launch.md#release-builds-with-the-harness)),
the module defines only these nine, in `test_widget_ops_hosted.cpp`:
`TapHostedWidget()` and `WidgetThemeObservation()`, returning an empty
function (`TapHostedWidget()` leaving the run's observation slot empty, so
no run calls anything), `WidgetThemeScheduled()`, returning 0, and
`WidgetThemeSelected()`, `WidgetThemeApplied()`, `WidgetHostRetired()`,
`WidgetPressDispatched()`, `WidgetCoreDetached()` and
`WidgetPressObserved()`, doing nothing. `test_widget_ops.cpp`,
`test_widget_ops_check.cpp`, `test_widget_ops_deaths.cpp` and
`test_widget_ops_exchanges.cpp` compile to
nothing there, and every other declaration of the header is left
undefined. With the harness, an
inactive harness and an unarmed id return the same before any observation
state is allocated, and the later seams find no registered host or work.

## Verification

`Test::RunWidgetOpsSelfCheck()` reads the tap through hosted taps under
message ids of peer 0, which no real message of any session has, and
disarms them before it returns. Its first row says the harness is active.
Every leg but the exchange leg drives hosted `WidgetHost`s built without a
channel and never started, calling the observers the tap installed on them
directly, on main, as a host thread would: a validated batch's `ops` array
through `onRenderOps`, which every build reads, and the helper's mirrored
records through `onPlatformMessage`, which a Debug build consumes and a
Release build with the harness (`kHelperMirrors` false: its helper has no
tap) passes on unread to the stand-in for the runtime's own observer. A
row whose claim holds only with a Debug helper has a Release name of its
own, starting `hosted: without a Debug helper`; only a Release build with
the harness compiles those branches. The legs, in the order they run:

- the session leg (`hosted: another session's message ...`, `hosted: with
  both sessions' messages ...`, `hosted: disarming one session's message
  ...`), one `FullMsgId` under two session values, each tap selected with
  no work id: with only the first armed, the second gets no tap from
  `TapHostedWidget()`, so the runtime installs nothing on its host (the
  hosted form of the product factory itself), and the first tap counts
  nothing; with both armed and installed, two taps read the control batch
  (2 leaks) and a removal batch (one `rm`, no leak) apart; disarming the
  first gives it no tap again while the second's selects a new generation
  and its installed host's next batch still counts (2 batches);
- the stress leg (rows named `hosted: stress: ...`), every batch through
  `onRenderOps` in both builds: an older host adopts `<html>` (id 2) with a
  `data-stress` value, then a newer selection, read before its host is
  built, clears that host's values and counts. The newer host gets a
  one-op adopting batch with a value and batches of 3 and 5 ops with an
  `attr` value each (the 5 also hold a value on another element and a
  `rattr`, both ignored): 3 batches, 9 ops, a largest of 5, the three
  literal values in order with receipts non-decreasing and inside the
  sends' own `crl::profile()` bounds, `conformance`, `updates` and `serial`
  untouched. The older host's late 6-op batch with a value on id 2 reaches
  its stand-in once and is scanned (the cumulative reading grows by 1 batch
  and 6 ops) but changes none of the newer host's counts or values and adds
  one `staleUpdates`. A 2-op batch without a value counts (4 batches, 11
  ops). 4094 more single-op values fill `stress` to 4096
  (`kWidgetStressValuesLimit`) with exactly 1 dropped, the kept values
  unchanged and in order. A 2-op re-adoption as id 7 followed by a value on
  id 2 clears the values and the drop, keeps counting the batch, and
  ignores the value on the earlier id;
- the hosted leg (the other `hosted: ...` rows) builds a hosted `WidgetHost`
  without a channel, never started, gives it stand-ins for the runtime's
  three observers and calls the observers the tap installed directly, on
  main, as its host thread would: an unarmed id gets no tap; an armed one is
  selected at once as generation 1; a second install does nothing, and the
  function's last copy releases it while the reading is not final yet; the
  install records the theme the host was built with (dark, an upper-case
  colour and a key of no theme, one button and one text token) as applied
  theme 1, its message equal to a literal written out from
  `WidgetHost::themeMessage()`, and leaves the renderer's scheme and theme
  message to a reading, the theme unobserved until then; the scanner
  control batch's `ops` array (one `img` with an `https://` `src` and a
  `url(https://...)` style, a `data:` attribute holding an `http://`
  namespace, and `url(data:...)` and `url(#g)` styles) gives exactly two
  leaks (the `src` and the style) and one note (the `data:` attribute),
  the two negatives nothing, as one host message of 4 ops, and reaches the
  stand-in unchanged; a plain message and a
  `Mirrored` record with a byte too many reach the stand-in in order with
  their bytes, while well-formed records of every way are consumed: the
  build's scheme, the served host's ack query, theme message and `frozen`
  (4 host messages with the batch, no `fatalFrozen`), the renderer's ack
  `result`, violation, `limit` report and trusted click after the mark, a
  `setTheme()` to the other scheme (one change) and a repeated build that
  changes nothing; the host's rendered acknowledgment counts one
  `renderedHosts` however often a change repeats it and probes nothing
  itself, while the function `WidgetThemeObservation()` gives for the
  tapped host probes the built theme once when the leg runs it (a widget
  with no runtime gives no reading: one `effectiveMissed`, the theme still
  unobserved); in Debug a room of one keeps one helper work record with
  its stamps on the profile clock, two valid records past it, one of
  exactly an hour (malformed instead while the clock has run less than an
  hour), count as dropped, and unordered, negative, over-an-hour,
  overwaited, stray-byte records and one that ends a minute after its
  receipt as dropped and malformed, while a Release build passes them on
  unread; the stamp mapping keeps full 32-bit `waits` and `frameBytes`,
  maps a record that ends at its receipt onto the profile reading taken
  with it, refuses one that ends after its receipt however short and
  ordered (a record of 995 to 1003 µs received at 1000) before mapping it,
  refuses both offsets that do not fit, and `CheckedTimeSum()` refuses an
  overflow at either limit; an `onDeath` reading `terminated` and the next
  death both reach the stand-in and count nothing (`hosted: every death the
  host reports reaches the runtime's own observer ...`), and the first
  change that carries a death keeps it raw in the selection's record, a
  later one changing nothing, neither making the record final (`hosted:
  the host's first change that carries its death ...`); of the effective
  readings, one for a
  superseded theme, one past 64 KiB and one that is no `[scheme, palette]`
  pair are not kept, one in the other scheme or with another colour for a
  theme key is kept with its exact difference, and a matching one is kept
  clean with the palette as the widget completed it; a later `sepia`
  theme scheduled through `WidgetThemeScheduled()` is pending with the
  returned token, and `WidgetThemeApplied()` with that token ends it,
  records light and its own literal message and is probed (no runtime, no
  reading), never as the renderer's scheme, while selecting, scheduling,
  applying and retiring on another host without a tap change nothing
  (token 0) and that host has no theme observation; of the tapped work's
  presses through `WidgetPressDispatched()` and `WidgetPressObserved()`,
  each played press dispatched inside `DeliverMarkedPress()`, a press
  dispatched and sent before a mark whose up and click come after it
  counts only in `pressStepsElsewhere` (2) and binds nothing, the press
  dispatched inside the next delivery is the marked one, bound to its work
  and serial, and its answered down and up with no click decision read 1,
  1, 0 (a target its down handler replaced); a press dispatched before
  another mark whose `Sent`, down, up and click decision all come after it
  never takes the mark (4 elsewhere), while the press dispatched inside the
  delivery is bound and, deciding no click, reads 1, 1, 0; after another
  mark a press dispatched before the delivery, a delivery that reached no
  core, then two genuine clicks of the same work and a second delivery bind
  nothing (`hosted: a press the chat dispatched after the mark but before
  ...`: 8 elsewhere, no marked work or serial); then after another mark the
  marked press's click
  before its up counts elsewhere, its up and click count once, a later
  press's four steps count elsewhere, another work's and work 0's reach no
  tap, and the next mark restarts every press reading; destroying the host
  makes the reading final, and that reading, with the control batch's 2
  leaks and 1 note in its 1 batch, is the self-check's `control`; no step
  of its work's presses counts after it; a tap dropped uninstalled counts
  as completed and released with nothing built; two messages' taps
  installed in both completion orders
  (the older construction's late install after the newer one's, then the
  reverse) see each work's press steps in their own tap alone (4 and 8),
  and after the older host is destroyed the newer's registration still
  counts (0 and 16); one message's tap with an older and a newer hosted
  generation, marked on the newer, counts the older generation's late up
  and click elsewhere, the newer's answered down and up as no click, and
  the newer's stable click as complete; marked again, a third generation
  selected between the newer's marked down and its up supersedes the mark
  (`markSuperseded` 3): the late up and click decision count elsewhere and,
  in Debug, the newer host's mirrored trusted click counts before the
  selection (1) and not after it; a light palette changed to another light
  one on a fresh host (`hosted: a light palette changed ...`) is Unobserved
  `theme-pending` from its scheduling, though its old reading has the same
  scheme and no mismatch, stays pending when an older job's application
  (another token) is recorded, and Matches once the pending job's
  application and its probe after the barrier (held by
  `ReplaceEffectiveProbe()`) are recorded; a fresh switch to dark whose
  reading kept the light link colour is Wrong with exactly that
  difference; and on another fresh host (`hosted: a held constructor
  observation ...`, `hosted: a switch's probe held across ...`) a probe
  held on a worker thread and released while the host is live is kept, the
  control, while a switch's probe held across `WidgetHostRetired()`, with
  no newer factory selected, keeps its application and no reading (one
  more `effectiveMissed`, a `retired` error), the switch staying
  Unobserved; after it a selection leaves nothing pending, scheduling
  returns 0 and a late application and observation record nothing; on a
  third fresh host (`hosted: a light palette selected while ...`) a light
  palette C selected through `WidgetThemeSelected()` while light palette
  B's job runs is pending under a token of its own: after B's application
  (a fresh serial 2) and its clean reading, the load reading and a switch
  begun before B are both Unobserved `theme-pending`, C's job started later
  gets the selection's token from `WidgetThemeScheduled()`, and its
  application and reading make both Matches (serial 3); then (`hosted: a
  selection that returns ...`) a selection away from the running job's
  palette and back to it before the job records makes that job's token the
  pending one again, its application ends it and Matches, and one away and
  back after the next job recorded leaves none pending, Matches; a
  disarmed id gets no tap again. In a Release build with the harness the same leg
  expects every message to reach the stand-in and only the batch to be
  recorded, no scheme before the widget's own reading, and the kept
  effective readings to give the scheme, its change and the theme message;
- the finals leg (`CheckHostedFinals()` in `test_widget_ops_deaths.cpp`),
  on two messages of its own, every work played with `WidgetHostedPlay`:
  `hosted finals: ...`, five works selected as the runtime constructs
  them, two installed on stub hosts and three never installed, all
  awaited; a host's raw death reaches the runtime's stand-in and charges
  nothing; a refusal on a route the stream spared, a refusal whose claim
  found no end, a spared and an unaffected final count once each in their
  own field, a second call of a copy of one observation counts nothing,
  and a raw death after a final fills its record alone; dropping every
  installer and host balances the factories and wrappers while the first
  work's `Stalled` final, left last, keeps `WidgetTapFinished()` false
  until it comes; each record, in selection order, keeps its outcome and
  transport apart from its raw death. `hosted eviction: ...`, a charged
  first selection evicted by 33 later uncharged ones stays counted, and
  `ReadHostedDeath()` reads its work, and a spared later sample printing
  the same raw line, dead and unmeasured, keeping a reason measured while
  the record was kept for that work alone. `hosted reading: ...`,
  `ReadHostedDeath()` over nine literal readings of one sampled work: a
  stalled final and a refusal measured from their records over a healthy
  or collateral line, an ordinary one from its own line or raw death and
  unmeasured at `terminated`, a spared sample dead and unmeasured beside
  another work's charge and alive without one, an unfinished or absent
  sample unmeasured;
- the exchange leg (rows named `hosted exchanges: ...`, in
  `test_widget_ops_exchanges.cpp`) reads TwidgetCore's own encodings
  through `Tgw::HostedAnswers()`, the classifier the widget helper's
  stream times hosted renderers with
  ([Remote engines](test_widget_fixture.md#remote-engines), *A renderer
  that stops answering*), in seven rows. Its first row reads single
  messages. A frame request, painting or ticking, reads `whenVisible`,
  timed only when sent visible, and expects the frame of its sequence,
  painted or unchanged.
  `Touch` and `Hover` expect a `Pointer`, with or without a claim, and a
  claim query the claim answer of its sequence, which the classifier
  reads with TwidgetCore's own `decode()`; none of the three reads
  `whenVisible`, so each is timed whatever the visibility. Each pair has
  a family of its own, three in all, and a frame or a claim answer of
  another sequence keys apart. The renderer's `Stopped` is its last word.
  A width, a state, a frame request cut inside its sequence and another
  control read as nothing. These malformed answers earn no credit: a
  claim answer cut short, with a flag that is no flag or with a byte past
  its end, which TwidgetCore's `decode()` refuses, and a painted frame or
  a pointer with a claim, each cut short.

  The second row checks painted frames. A painted frame earns its
  request's credit only when it passes the stream thread's checks of a
  constant cost (`FrameComplete()`, `ListAdmitted()`, `DamageComplete()`
  in `tgw_remote.cpp`): its list and its damage need their form and
  version, the list a command count within the cap and no larger than
  its bytes can hold at 5 bytes a command (an empty one nothing after its
  count), the damage a valid flag and exactly the rectangles it counts,
  within the cap, nothing may follow the two values, and the frame must
  be within Telegram's 8 MiB frame cap (`HostedWire::kMaxFrameBytes`,
  past which the render thread refuses a frame before decoding it). The
  row writes that cap out itself rather than taking the constant, so its
  cases at the cap and one byte past it fail when the cap moves. The
  stream never reads the list's commands, so these checks are not
  TwidgetCore's whole acceptance: its `wire::decode()` also reads every
  command and requires the list consumed whole, and a frame that passes
  the checks earns its credit even when that decoder would refuse its
  commands. The row's cases:
  - No credit, each failing those checks and also refused by
    TwidgetCore's own `decode()`, which the row runs on it: empty values,
    an empty damage, a list of another form, commands that do not fit or
    are past the cap, a byte after an empty list, a damage flag that is
    no flag, a damage of another version, a rectangle cut short, a byte
    after the rectangles, rectangles past the cap and a byte after an
    unchanged frame. A frame one byte past the frame cap earns none
    either.
  - Credit: a drawn frame and an empty list with no damage or with no
    rectangle, each also decoded by TwidgetCore's `decode()`, and a frame
    exactly at the cap, its list one command of filler bytes, which the
    row deliberately does not decode: only the render thread does.

  The third row plays a widget helper's stream end on two routes over
  widget-sandbox's in-memory pair (`harness/memory_stream.h`), the host's
  end opened with `Tgw::HostedAnswers()`. The engine process's end is the
  framework's shared `ScriptedEngineEnd` (`tests/test_stream_frames.h`),
  the end the native suite's scripted peers wrap. Two real hosted clients
  (`SandboxEngine::hostedClient()`) attach as routes 1 and 2, start and
  send the same request, each visible from its start unless the case
  hides it. Once the engine process's end has read the opening and each
  route's `open`, hosted start and exact request, it gives each route the
  case's answer, if any, and closes. The stream ends of that close: the
  host's end reports `the engine process closed the stream` and each
  client dies of `the widget engine process ended`, both required in
  every case. A route's death is the stream's decision as its channel's
  `death()` gives it, the decision Telegram's runtime reads for each run
  ([Remote engines](test_widget_fixture.md#remote-engines), **Runtime log
  and deaths**).
  - A route keeps its `Ordinary` death while the legally answered route
    is `Spared` when its request stayed unanswered. Its answer can be an
    impossible painted frame, one past the frame cap, a claim answer cut
    short or a valid frame of another sequence. It can also get no answer
    to a frame it requested while hidden, which no deadline times.
  - When both answers earned their credit, neither route had a request
    unanswered and both keep `Ordinary`. That is the fallback for an end
    with nothing unanswered. Both also keep it when neither answer earned
    credit.

  The fourth row reads frame deltas, which a renderer sends for a painted
  frame whenever that is shorter than the whole frame. A `FrameDelta`
  answers the frame request of its sequence as a frame does: it reads
  `Answers` in the frame's family (64), keyed by its own full 64-bit
  sequence, without `whenVisible`. The stream credits it only within the
  frame exchange's 8 MiB bound, past TwidgetCore's `delta_scan()` under
  `HostedWire::EmbeddingCaps()`, which walks its header, state, base,
  length, runs (copies within the 256 KiB delta base) and damage value to
  the last byte, making nothing, and checks the frame it stands for against
  the caps, then only when the list length that scan gives is one a list
  `ListAdmitted()` takes can have (`ListLengthAdmitted()`: exactly 6 bytes
  for an empty list, at least 11 for one with a command), and past
  `DamageComplete()` on the damage it views (`DeltaKey()` in
  `tgw_remote.cpp`). Neither rebuilds its list, which needs the kept base
  only the platform end holds. The row runs each delta through TwidgetCore's
  `decode()` under the same caps too, which takes every credited one with
  its sequence, so each malformed case it refuses shows its change hit a
  field the codec checks, and every fixture must encode before it is
  changed. `decode()` also takes a delta of a list length no list can have,
  which only the platform end's decode of the rebuilt list refuses; such a
  delta earns no credit, as a whole frame of such a list earns none. The
  row's cases:
  - Credit: the frame request of sequence 7 (`Expects`, timed while
    visible) and, answering it, a whole painted frame of 7, a delta of 7
    with copies, a literal and a damage rectangle, the same delta without
    damage, a delta of an empty list (6 bytes) and one of 11 bytes, ten in
    a literal and a copy of the delta base's last byte. A delta of a
    sequence whose low 32 bits are 7 and whose high bits are not keys
    apart, with all 64 bits.
  - No credit though `decode()` takes it: a delta of a one-byte list and
    one of a ten-byte list; the whole painted frame of the same one-byte
    list earns none either.
  - No credit, each also refused by `decode()`: the delta cut right after
    its sequence, state and base (which `delta_ids()` still reads), cut by
    its last byte, with a byte after it, with a run of an unknown tag,
    with the 11-byte delta's copy a byte past the delta base (encoded under
    a base a byte longer), with a damage flag that is no flag, and padded
    to one byte past Telegram's frame cap.

  The fifth row reads the fields of a delta that the fourth leaves
  unread, through the classifier and TwidgetCore's `decode()` alike. Its
  malformed cases are laid out by hand, as the codec would not write them,
  beside a plain copy laid out the same way, which must equal
  TwidgetCore's own encoding. The row writes both caps out, the 256 KiB
  delta base (`HostedWire::kMaxDeltaBaseBytes`) and the run cap (one run
  for the list's head and one per command of the list cap), so its cases
  at a cap and past it fail when the cap moves. The row's cases:
  - Credit, each also taken by `decode()`: a copy of the whole base, a
    length at the base cap and runs at the run cap.
  - No credit, each also refused by `decode()`: a state flag that is no
    flag, no runs, an empty rebuild, more runs than follow, runs past the
    run cap, an empty copy, an empty literal, runs that rebuild short of or
    past their length, a literal past the message, a length past the base
    cap, and a damage missing, of another form or with rectangles past the
    cap.

  The sixth row is the equivalence the delta rows rest on, and the
  refusal that stays with the render thread: a platform end on Telegram's
  caps, as that thread keeps one, asked for two forced frames and given
  the painted base for the first, takes the whole second frame and the
  delta that copies it as one and the same frame, while a delta the stream
  credits alike that cannot rebuild on the base that end kept (none
  painted, another base, a copy past the base, bytes that make no list) is
  that end's own refusal and gives no frame.

  The seventh row plays a frame delta at a stream's end as the third row
  plays one, on two visible routes that each painted their base whole
  first and hold their answers to the second forced frame request at
  their consumer, so that only the answers' arrival can pay. Route 1's
  delta that copies its base pays as the equivalent whole frame of the
  same sequence does: with route 2 still owing its own, the close spares
  route 1 and keeps route 2's `Ordinary` death; with both answered both
  keep it, also when the stream stays open past the 3000 ms answer
  deadline (`kOpenBound`, that deadline and 1.5 s more), which then ends
  nothing. A delta of a later sequence, the delta sent on route 2 after
  its own answer, one cut short, one whose runs rebuild short of their
  length and one past the base cap pay nothing: route 1 keeps its
  `Ordinary` death and route 2 is spared.

  Every stream of the leg runs inside `OnStream()`, and each wait for a
  step (the opening, each route's frames, a held answer, the stream's end,
  each client's death) is bounded at 5 s (`kEndBound`), looking in slices
  of at most 50 ms (`kHaltSlice`, `WaitFor()`) and checking before each
  whether the leg stopped; past its bound that case's reading names the
  step and the row fails instead of hanging. Then `OnStream()` lets every
  held message go, drops the clients and calls `finish()`, which first
  stops the host's end, whose `stop()` joins that end's threads with no
  deadline, and only then waits 5 s for the engine process's end to stop
  reading, past which the row fails too and the framework's destructor
  waits up to its own 5 s (`kFinish`) and then leaves a reader still
  reading to return on its own. The whole leg shares one deadline
  (`Halt`): 20 s after it began (`kLegBound`, `kWidgetOpsExchangesTimeout`
  less twice `kEndBound`), or `WidgetOpsExchanges::stop()`, whichever comes
  first. From then on it starts no stream, each open stream's waits give
  up within a slice and it closes, and each case it did not finish fails
  its row with `the leg ran past its 20000 ms` or `the leg was stopped`.
  Nothing else in the leg has a deadline of its own: building the stream
  and the clients, writing the answers (`ScriptedEngineEnd::send()`, whose
  `MemoryStream::write()` waits for room on the 1 MiB pair), the clients'
  teardown and the host's end's `stop()` can each hold it as long as they
  take, so a 5 s failure names a step that did not come in time and says
  nothing of those.

  `RunWidgetOpsSelfCheck()` plays the leg within the call by default (59
  rows in the Debug build, the seven exchange rows among them). With
  `WidgetOpsExchangeLeg::Apart` it skips the leg (52 rows), and a
  `WidgetOpsExchanges` plays it on a thread of its own: `start()` once,
  `ready()` as a stage's until, `take()` for its rows, named as the call
  names them (`SelfCheckRowName()`), and `stop()` from `Runner::onFinish`.
  A leg thread that does not start, or a leg that throws, gives one failed
  row instead, `hosted exchanges: the leg played to its end on a thread of
  its own`, with why. Both packs play the leg apart in their first stage,
  which waits up to `kWidgetOpsExchangesTimeout` (30 s) with the event
  loop running and then takes its rows after every other row of that
  stage; a stage that times out fails the scenario and takes none;
- the budget leg (`hosted: a host and a renderer message past the scan
  budget ...`; without a Debug helper `hosted: without a Debug helper a
  validated batch past the scan budget ...`) on a host of its own, with the
  same inputs in both builds: on each route the hosted tap has, one input
  one value past `kMaxScanValues`, then one exactly at it. They are a
  validated batch of one `anim` op whose `pad` zeros make the wrapped batch
  that many values, through `onRenderOps`; the served host's `anim`
  message the in-process leg sent, as a mirrored served-host record; and a
  renderer `violation` (`over`, then `at`), as a mirrored renderer record,
  both records through `onPlatformMessage`. The row requires
  `WithinScanBudget()` to put each pair, as the reader gets it, on either
  side of the budget, so a miscounted padding fails it (`counted=0`) rather
  than passing, and each gap example to name its input past the budget by
  its route (`host message` or `renderer message`) and its byte count, in
  the order sent (`named`). In Debug the three inputs past the budget are
  the only gaps, never parsed, and the ones at it are read: the batch as
  one batch of one op, the `anim` message as a host message with no gap
  (4 host messages in all) and the violation as one violation `at`, every
  record consumed; without a Debug helper only the batch past the budget
  is a gap, the one at it is read the same, and the four records reach the
  stand-in unread, in order. Then the long-name row (`hosted: a batch with
  100 KiB attribute ...`) on the same host: a batch with 100 KiB attribute
  and style names and 164 leaks counts all 164, keeps exactly 64 examples
  within 64 KiB, each place cut to 240 bytes, the first one's text checked
  exactly, and adds no gap. It reads few JSON values in many bytes, so of
  the rows only the budget row reads a message near the budget;
- the scan-gap leg (`hosted: an unparsable message and an ops message
  without an array are scan gaps, forwarded unchanged`) on a host of its
  own, the same in both builds: two validated batches through
  `onRenderOps`, an array cut off (`[`), then an object in its place
  (`{"op":"rm","id":5}`), which the tap, wrapping each as `WidgetHost`
  wraps a batch, reads as `{"type":"ops","ops":[}`, which does not parse,
  and `{"type":"ops","ops":{"op":"rm","id":5}}`, an `ops` message without
  an array. The row requires 2 host messages, 2 scan gaps, no batch and
  no op, the gap examples `unparsable host message: ` and
  `ops message without an ops array: `, each followed by its whole
  wrapped message, in the order sent, and the stand-in's `ops` list
  exactly the two inputs, in the same order. The inputs take the
  validated-batch route alone: the helper's tap never mirrors a
  served-host message that starts `{"type":"ops",`, as both wrapped
  inputs do, so no helper mirrors either as a served-host record.

Every expected value is a literal of the self-check, not derived from the tap.

`Test::RunWidgetConformanceProbeCheck(fixture)` checks the latest reading
the same way, on hosted taps under other peer-0 ids, and disarms them
before it returns. Its first two rows say that the harness is active and
that the `probe-fixture.json` bytes its caller read from the pinned package
parse into their cases (the byte and case counts and the first error);
then:

- when they parse, every case (rows named `hosted: case N (name): ...`, N
  counted from 1) on a hosted tap and host of its own, its messages handed
  over one by one and the reading taken after each, as a runner polls it.
  Each message is routed as the helper's tap splits a served host's
  messages, by its type as Qt's JSON parser reads it rather than the tap's
  own parser: an `ops` message's array through `onRenderOps`, as the
  fixture's own bytes inside its `{"type": "ops", "ops": [...]}` envelope,
  with no re-serialization, once Qt reads those bytes as the very array
  the message holds; and anything else (the first case's
  `query`) as a mirrored served-host record through `onPlatformMessage`,
  once the record decodes back to the message's bytes. The html id and the
  count of values equal the case's `htmlId` and `updates`, the last value
  parses to its `final`, and the targets of the last `awaiting` value
  equal its `awaitingTargets` where the case has them. The forwarding row
  requires every message routed, so one that fails those checks fails it
  instead of vanishing from both sides of its comparison, and each
  dispatch to change just what its route reads, against the reading and
  the stand-in's lists taken before it: an array one host message and one
  batch, no gap, and the fixture's bytes onto the stand-in's `ops`; a
  record in Debug one host message, no batch and no gap and neither of the
  stand-in's lists (consumed), and without a Debug helper the record onto
  the stand-in's `platform`, unread, the reading unchanged (`step` names
  the first message whose dispatch changed anything else). At the end the
  stand-in's `ops` must be those fixture arrays in order, its `platform`
  empty in Debug and the records in order without a Debug helper, and the
  tap's host messages the arrays and, in Debug, the records. Every
  expected value is read from the fixture, never from the tap; the cases
  at the pinned package are a value on the adopting op replaced by two
  `attr` ops around a query (html id 2, 3 values, final `done`, targets of
  the `awaiting` one), the same attribute on another element ignored, and
  an `attr` before the adoption ignored: 4, 3 and 3 rows;
- whatever the parse gave, the replacement leg (`hosted: selecting a newer
  host ...` to `hosted: the final reading ...`) with literal expectations,
  both hosts adopting `<html>` as id 2 as fresh sandboxes of one widget do
  and each host's build mirrored right after its install (the older's
  light, the newer's dark): selecting a second generation clears the first
  host's `done` before the second host is built; the second reads its own
  adoption and value from 1, and in Debug its mirrored scheme, although the
  first adopted again with a value after the selection; then the first
  host's late `done` on id 2, the scanner's control batch, a mirrored
  `setTheme()` to the other scheme and a mirrored `theme` message come: the
  batches reach the first host's stand-in and are scanned (2 leaks), and
  the newer reading keeps its value, scheme and empty theme message, with
  `staleUpdates` 4 in Debug (two late results, two theme records) and 2
  without a Debug helper, whose three records reach the first host's
  stand-in unread (that row named `hosted: without a Debug helper the older
  host's late value ...` there); a later value-less adoption clears the
  value and the earlier html id stops matching; both hosts end final with
  all 8 batches scanned.

The re-driven rows keep the names their in-process legs had, after
`hosted: `: "gets the product factory itself", "an older wrapper",
"forwarded" and "setTheme()" are that wording. In a hosted row a message is
forwarded when it reaches the observer the tap wrapped or, in Debug, when
it is a mirror the tap consumed in its place, and `setTheme()` is the
helper's mirrored theme record.

**Evidence.** Executed on macOS arm64 Debug in the evidence campaign of
`2026/10/24/audit-the-widget-attack-corpus-through-a-validated-ops-tap` (its
`work/test.md`), at widget-sandbox `v0.1.2` and twidget "Document
widget-sandbox v0.1.2's gesture policy and 166/1/0": all 22 self-check rows
passed in the real binary, and the control read 2 leaks, 1 note, 2 batches
and 5 ops. Through the seam the same launch tapped one product renderer for
each of the 99 attack widgets, scanned 7330 validated batches with no scan
gap, and every factory and wrapper finished before its final reading. The
copied scanner functions compared byte for byte with the tag. Windows and
all other platforms have not run it; the code is portable.

The latest reading and `RunWidgetConformanceProbeCheck()` were added by
`2026/10/24/run-the-widget-conformance-sets-through-the-chat-list-and-report`
and compiled in the macOS arm64 Debug build, with the copied scanner block
still byte for byte the tag's. Outside the app, this file built with stub
session and harness headers passed all 22 self-check rows and all 20 probe
rows against the `v0.1.2` fixture, and the expected rows failed for a fixture
whose first case expects 4 values and for five mutations of the reader: no
generation check, no clear on a newer selection, none on a re-adoption,
every `setTheme()` a change, and values from any element. The rows run in
the real binary in that task's conformance campaign, which records them.

The `data-stress` values, their receipts, the newest host's batch counts
and the stress leg were added by
`2026/10/30/run-the-widget-stress-kit-at-v0-4-0-on-a-release-build-without-lto`.
Executed so far, on Windows Win32 with MSVC 14.44: a Debug single-file
compile of `test_widget_ops.cpp` through MSBuild and of `tgw_runtime.cpp`,
which includes the header, and standalone compiles of
`test_widget_ops.cpp` with the Release command line both with
`TDESKTOP_RELEASE_TEST_HARNESS` and without it. When that task recorded
this, no row of the stress leg, and no row since its change, had run in
the app: they run where a scenario calls `RunWidgetOpsSelfCheck()`. The
earlier rows' expectations are unchanged, as the conformance reading and
the cumulative counts are.

`limitReports`, `limitExamples` and their self-check row were added by
`2026/11/02/stop-widgets-past-the-heavy-box-limit-with-a-v0-6-0-twidgetcore`
and compiled in the macOS Debug build. The row runs in that task's evidence
campaign, which records it; the earlier rows' expectations are unchanged
apart from the two reports' places in the forwarding sequence.

`TapHostedWidget()`, the hosted readings and the hosted leg were added by
`2026/11/14/run-widget-validators-and-renderers-in-the-macos-helper` and
compiled in the macOS Debug build (arm64 and x86_64), together with the
helper's half; no hosted row had run in the app when that was recorded,
and its evidence campaign (the attack, conformance and stress parity
legs) is where the hosted readings are compared with the `in-process`
ones. The earlier rows' expectations are unchanged. The applied and
effective themes,
`downsConfirmed` and `clicksCompleted`, their two seams, the registry by
work id, the bounded scan sink, the checked helper stamps and their rows
came with that task's Release readings and its second review round:
compiled in the macOS Debug build and syntax-checked with the Release
command line with and without `TDESKTOP_RELEASE_TEST_HARNESS`. Outside the
app the two theme literals equalled what the widget-sandbox functions they
are written from build for the leg's two themes, and the sink, the reader
of effective answers and the stamp mapping, taken unchanged from
`test_widget_ops.cpp` into a scratch harness linked with widget-sandbox's
`sdk_policy.cpp`, passed, while the scanner before the sink kept megabytes
of examples for the long-name batch. Since the sink, the copied block is
compared with the tag by its predicates, not byte for byte.

The same task then split `test_widget_ops.cpp`, past 4000 lines, into the
files above without a change of behaviour: every removed line reappears in
the new files apart from namespace and include scaffolding and the shared
declarations (`health.py moves`), the module defines the same functions in
Debug and in both Release variants (with and without
`TDESKTOP_RELEASE_TEST_HARNESS`), plus the shared internals, and the macOS
Debug build and the Release syntax checks of the three files pass. When
the split was recorded, no row had run in the app since.

The task's third review round moved the built theme's probe from a
`crl::async()` worker onto the runtime's per-host job
(`WidgetThemeObservation()`), shared the theme predicate
(`CheckWidgetTheme()`), replaced `WidgetPressAcknowledged()` with
`WidgetPressObserved()` and the marked interaction, and made a helper work
record that ends after its receipt malformed; compiled in the macOS Debug
build and syntax-checked with the Release command line with and without
`TDESKTOP_RELEASE_TEST_HARNESS`. Outside the app `pressObserved()` and
`markEvents()`, taken unchanged from `test_widget_ops.cpp` into a scratch
harness, gave the expected readings for the late, replaced, stable and
two-generation sequences, which the pre-change reading (an answered up
counted as a click) got wrong in all eight; a model of the runtime's
per-host jobs over a fake host found no probe beside another or a keep in
2000 random interleavings, against 1390 overlapping probes and 216 keeps
under a probe with the earlier independent probe. When that round was
recorded, no row had run in the app since.

The task's fourth review round bound the marked interaction to the press
the chat dispatched after the mark (`WidgetPressDispatched()`, the pointer
serial carried by `Sent`), superseded a mark at a newer selection,
recorded a pending theme application from its scheduling
(`WidgetThemeScheduled()`, the token `WidgetThemeApplied()` carries) and
revoked a retired host's publications (`WidgetHostRetired()`); compiled in
the macOS Debug build and syntax-checked with the Release command line
with and without `TDESKTOP_RELEASE_TEST_HARNESS`. Outside the app the
tap's press and theme methods and its theme predicates, taken unchanged
from the sources into a scratch harness under ASan, UBSan and TSan, gave
the expected readings for a press dispatched before the mark, a selection
between the marked down and its decision, the trusted-event filter, a
same-scheme palette change across the barrier, an older job's
application, probes held across a retirement and 2000 retirements racing
a commit; the same harness over the methods before the change failed 9 of
its 10 sequences, all but the control. When that round was recorded, no
row had run in the app since.

The task's fifth review round opened the press binding only around the
synchronous delivery of the fixture's own mouse-down (`DeliverMarkedPress()`,
sealed when it returns, so a press that never reached the core leaves the
mark unbound for a later genuine click of the same work), bound it to the
marked generation's works alone, and made the theme the runtime selects
while another theme's job runs pending at that selection
(`WidgetThemeSelected()` from the busy guard of `Runtime::syncHostTheme()`,
the token `WidgetThemeScheduled()` hands its job later); compiled in the
macOS Debug build and syntax-checked with the Release command line with and
without `TDESKTOP_RELEASE_TEST_HARNESS`. Outside the app the tap's methods,
taken unchanged from the sources into a scratch harness with a model of
the runtime's busy guard, gave the expected readings for a missed delivery
followed by a genuine click, a press bound inside its delivery and decided
after it, a palette selected while another's job runs and read after that
job ended, and selections returning to the running job's palette; the
methods before the change certified the missed press and the busy-guard
palette. When that round was recorded, no row had run in the app since.

The task's sixth review round bound a local widget's click to the press the
fixture played as well: the runtime
registers the core's work with the generation `TapWidgetRenderer()`
selects, the same dispatch binds it, and the work's render thread reports
the down its renderer took and the release that ends it with the renderer's
answer, so only the trusted click of the played press's own release counts
in `clicksCompleted`; compiled in the macOS Debug build and syntax-checked
with the Release command line with and without
`TDESKTOP_RELEASE_TEST_HARNESS`. Outside the app the tap's press methods
and the attack audit's `ClickOutcome()`, taken unchanged from the sources
into a scratch harness, gave the expected readings and outcomes for the
missed, replaced, queued, withheld, overtaken, superseded and stable
sequences of both placements; the reading before the change certified the
missed press followed by a genuine click locally. When that round was
recorded, no row had run in the app since.

The seventh round tied that click to its forwarding and to the core's
retirement: `clicksCompleted` counts the
trusted click of the played press's own release only when the work's
forwarding hands it to the wrapper before `WidgetLocalPressForwarded()`,
and the detach seam (then `WidgetLocalDetached()`), which
`RendererCore::detach()` called on main right after it closed the host link,
revokes a mark whose click has not reached the host (`markDetached`), so an up queued across a retirement,
while a host sample still holds the wrapper, completes nothing beside an
earlier press's trusted click; compiled in the macOS Debug build and
syntax-checked with the Release command line with and without
`TDESKTOP_RELEASE_TEST_HARNESS`. Outside the app the tap's methods, the
renderer message reading, the registry, the wrapper's observer and the
seams, taken unchanged from the sources into a scratch harness with a
model of the host link, gave the expected readings for each forwarding and
retirement case, the attack audit's verdict sequence and a race of
retirements against a real render thread; the methods before the change
certified the retired press. When that round was recorded, no row had
run in the app since.

The eighth round applied the same cutoff to a hosted host:
`WidgetHostRetired()` revokes, under the tap's lock, a mark bound to the
retired host's work whose click the helper had not reported
(`markDetached`), since the widget render thread can still report the up
and click of a reply it took before the work's detach reached it; a click
reported before the retirement stays. The local retirement self-check row
now reads its wrapper while it lives, so the final reading after the
wrappers are gone no longer fails it. Compiled in the macOS Debug build;
outside the app the tap's methods and the attack audit's verdict sequence,
taken unchanged from the sources into a scratch harness, gave
`click-not-observed` for the queued hosted steps through both hosted
readers and kept the completed click; the methods before the change
certified it. When that round was recorded, no row had run in the app
since.

The ninth round moved the cutoff to the core's detach for both placements:
`RendererCore::detach()` calls `WidgetCoreDetached()` (formerly
`WidgetLocalDetached()`, local works alone, after the link closed) first,
on main, so a hosted mark is revoked before the link closes and before a
full kept pool makes the runtime release the route, while
`WidgetHostRetired()` still seals what the host publishes afterwards.
Compiled in the macOS Debug build; outside the app the tap's methods and
the attack audit's verdict sequence, taken unchanged into a scratch
harness, gave `click-not-observed` through both hosted readers for a
played down bound before the detach whose up and click, and in Debug the
mirror, came between the detach and the host's retirement, kept a click
completed before the detach, and found no wrong outcome in races of the
detach against the render thread's steps; the tap before the change read
that interval `observed` in Debug and `decided` in Release. When that
round was recorded, no row had run in the app since.

The exchange leg was added by
`2026/11/20/kill-the-widget-helper-when-a-renderer-leaves-a-request-unanswered`,
whose evidence campaign (its `work/test.md`, Attempt 1, Test 1) ran
`RunWidgetOpsSelfCheck()` as registered in the macOS arm64 Debug client,
built with that campaign's disposable evidence overlay, in Run 1 and
again in Run 2: all 78 rows passed both times, the three `hosted
exchanges:` rows among them. In Run 1 the painted-frame row credited the
frame of 8388608 bytes and not the one of 8388609, and the end row read
`ordinary/spared` for each of the five unanswered cases and
`ordinary/ordinary` for both answered and for neither, every end `the
engine process closed the stream`. Each note above saying that no row had
run in the app holds as of its own round; these two runs ran every row of
the self-check, on this task's commit with that overlay. The earlier
rows' expectations are unchanged. The campaign did not run the leg in a
Release build with the harness or on another platform.

Since `2026/11/14/build-release-telegram-without-the-in-process-widget-renderer`
the self-check's control is the hosted leg's final reading (1 batch of 4
ops): the row counts and the 2-batch, 5-op control recorded above include
the local legs that task removed. It removed the session, stress, budget
and long-name legs and the probe check's case replay and replacement with
them, leaving the probe check its first two rows. Since
`2026/11/20/re-drive-the-widget-ops-self-checks-shared-reader-legs-through-hosted-taps`
those six legs run again, through hosted taps as described above, each row
named as before with `hosted: ` in front. The local legs (the unarmed and
throwing product factories, the local presses and the local retirement),
the wrapping factory's leg (`CheckArmed()` with `ExerciseWrapper()`) and
the probe check's factory theme leg (`CheckProbeTheme()`) stay removed:
their subject was the in-process renderer. When this was recorded the
re-driven legs had compiled in the Windows Win32 Debug build and none of
their rows had run in the app; their Release branches had been read, not
compiled. That task's evidence campaign is to run them in a Debug
conformance launch, and again with the scan budget made one value short,
where only the budget row should fail.

Since `2026/11/21/re-drive-the-widget-ops-self-checks-scan-gap-row-through-a-hosted-tap`
one row of the wrapping factory's leg runs again, as the scan-gap leg
above, named as before with `hosted: ` in front; the reader is unchanged.
Of the messages its inputs give the reader, the unparsable one is the
removed leg's with the wrapping's closing brace after it and the other is
byte-equal. The wrapping factory's other rows stay removed: their subject
was the local factory, or a hosted row already pins their reader (the
violation detail, by the budget row), or no consumer reads what they
pinned (the limit-report examples). That task's evidence campaign is to
run the row in a Debug conformance launch, and again with the reader
taking an `ops` message without an array for an empty batch, the
deviation the row is there to detect, where only this row should fail.

The final charges (`hostedSelected` to `hostedRuns` and the final
observation of `TapHostedWidget()`), `WidgetTapFinished()` with its hosted
balance (moved here from the fixture), `ReadHostedDeath()` and its
helpers, `WidgetHostedPlay`, the deaths and finals legs, the exchange
leg's delta field, rebuild and delta-end rows, `WidgetOpsExchanges` and
the leg's shared deadline were added by
`2026/11/20/count-a-widget-helpers-death-only-against-widgets-with-work-in-flight`.
Its evidence campaign (its `work/test.md`, Attempt 1, which holds the
measurements) ran them in the macOS arm64 direct-download Debug client and
its embedded Debug helper, built with its disposable evidence overlay.
Both packs' first stages gave all 57 `widget ops self-check:` rows,
passing, the six exchange rows taken from the leg's thread, and the
packs' removal barriers closed with every selected hosted work final.
That campaign ran before the task moved onto the scan-gap leg above and
onto
`2026/11/21/count-a-hosted-widgets-frame-differences-as-answers-to-its-frame-requests`,
whose credit of frame deltas in the classifier replaced the task's own
and whose frame delta row, the fourth, took the cases the two rows
shared; the task's row keeps the rest, as the fifth. What
each pack then read of its widgets' deaths is on its own page
([test_widget_attacks](test_widget_attacks.md#verification),
[test_widget_conformance](test_widget_conformance.md#verification)). No
Release build with the harness and no other platform ran them.

## Related guidance

[Scenario composition](../scenarios.md);
[scenario teardown](../teardown.md);
[chat-list show before input](test_chat_show.md);
[input helpers](test_widgets.md#input-helpers);
[launch fuse](test_launch_fuse.md);
[widget attack audit](test_widget_attacks.md);
[widget conformance runner](test_widget_conformance.md);
[the helper's half of the tap](test_widget_helper_tap.md).
