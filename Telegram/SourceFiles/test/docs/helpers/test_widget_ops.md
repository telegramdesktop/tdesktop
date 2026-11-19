# test_widget_ops

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API, with focused self-checks.
Source contract: [test_widget_ops.h](../../test_widget_ops.h).
Internal to it:
[test_widget_ops_internal.h](../../test_widget_ops_internal.h).

| File | Holds |
| --- | --- |
| `test_widget_ops.h` | The contract: the renderer factory and hosted seams, arming, the readings, the helper work and the self-checks. |
| `test_widget_ops_internal.h` | What the three translation units share: the scan and kept-value bounds, `kHelperMirrors`, the tap's record (`WidgetOpsTap`), the armed taps, the scan budget, the reader of effective theme answers and the mapping of helper stamps. |
| `test_widget_ops.cpp` | The tap's record, the scan (the `Upstream` block and its `ScanSink`), the result reader, the wrapping factory (both `TapWidgetRenderer()` forms), the local works' registry and press seams (`WidgetLocalPressDown()`, `WidgetLocalPressReleased()`, `WidgetLocalPressForwarded()`, and the local half of the detach seam), arming and the readings; without the harness, the first five functions alone. |
| `test_widget_ops_hosted.cpp` | The hosted tap: `TapHostedWidget()`, its observations and registry, `WidgetThemeObservation()`, `WidgetThemeApplied()`, `WidgetPressDispatched()` and `WidgetCoreDetached()` (hosted, else the local registry), `WidgetPressObserved()`, the effective theme probe, `WidgetThemeUnobserved()`, `CheckWidgetTheme()`, the helper work records and `CheckedTimeSum()`; without the harness, the hosted seams alone. |
| `test_widget_ops_check.cpp` | `RunWidgetOpsSelfCheck()` and `RunWidgetConformanceProbeCheck()`: the stub renderer and every leg. |

## Use when

A scenario needs what a widget message's sandbox host really sends its
renderer: the validated ops, every batch of them, including batches a later
batch removes or replaces and the theme replay that `WidgetHost::setTheme()`
sends straight through `toRenderer()`. It arms one message, by its
session-qualified `GlobalMsgId`, before its host is constructed, and the
product renderer of every host built for that message is wrapped in a
forwarding tap at the `twidget::Renderer::send` seam. Each
`ops` batch is scanned, before it is forwarded, with widget-sandbox's
`--attacks` leak definition, its predicates copied and its examples bounded
([Leak definition and fidelity](#leak-definition-and-fidelity)). The tap also counts the host's
post-load ack, the renderer's answer to it, a fatal `frozen`, renderer
`violation` messages, renderer `limit` reports and trusted renderer events,
and it says when no factory call or wrapper of the message is still pending.

On macOS a widget's engine, validator and renderer run in the widget helper
by default ([Hosted widgets](#hosted-widgets)), and there is no renderer in
Telegram to wrap: the tap is then installed on the hosted `WidgetHost`
itself, takes the validated batches from its `onRenderOps` and the rest
from the records the helper's Debug half
([test_widget_helper_tap](test_widget_helper_tap.md)) mirrors to it, into
the same readings. A Release build with the harness, whose helper has no
Debug half, reads instead the theme the widget itself took after each
application and the clicks the helper's renderer completed
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
`RunWidgetConformanceProbeCheck()`. The stress-kit runner of
`2026/10/30/run-the-widget-stress-kit-at-v0-4-0-on-a-release-build-without-lto`
is the planned reader of the `data-stress` values and the batch counts;
until it lands nothing reads them. The heavy-box evidence of
`2026/11/02/stop-widgets-past-the-heavy-box-limit-with-a-v0-6-0-twidgetcore`
reads `limitReports` and `limitExamples`: one report per strict host, none
under compat.

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
  wrapped, and two armed ones get two taps. Keep the armed id for
  `DisarmWidgetOpsTap()`; never rebuild it later from whichever account is
  active then.
- **Active harness.** `TapWidgetRenderer()` returns the product factory
  unchanged unless `Test::Active()`, and for every message id that is not
  armed, and then registers no work; a build without the harness defines
  both its forms, as that identity, the local press seams
  `WidgetLocalPressDown()`, `WidgetLocalPressReleased()` and
  `WidgetLocalPressForwarded()`, which do nothing, and the hosted seams:
  `TapHostedWidget()`, which returns an empty function there and for an
  unarmed id, `WidgetThemeObservation()`, which returns an empty function,
  `WidgetThemeScheduled()`, which returns 0, and `WidgetThemeSelected()`,
  `WidgetThemeApplied()`, `WidgetHostRetired()`, `WidgetPressDispatched()`,
  `WidgetCoreDetached()` and
  `WidgetPressObserved()`, which do nothing.
- **Which seam taps.** The runtime takes one of the two per construction,
  as its placement says: `TapWidgetRenderer()` for a local host (the
  `in-process` and `helper-engines` values of `TDESKTOP_WIDGET_ENGINES`, and
  Windows and Linux always), `TapHostedWidget()` for a hosted one (macOS by
  default). Both select a generation of the same tap at the same point, so
  every reading and its finality read the same way in either arrangement;
  what differs is where each reading comes from
  ([Hosted widgets](#hosted-widgets)).
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
- **The latest result is the newest host's.** Each wrapping factory handed
  out for the message starts a new `generation`, and the latest fields
  restart with it: its result, html id, value count, scheme and theme message
  are cleared before it builds its renderer, so an older host's `done` never
  completes a newer host's load. Whatever an older host's wrapper still sends
  afterwards is forwarded and, for batches, scanned as always; a result, a
  `theme` message or a `setTheme()` among it leaves the latest fields alone
  and is counted in `staleUpdates`, and none of its batches enters the
  latest `batches`, `ops` or `maxBatchOps`.
  `generation` above 1 names a run in which more than one host was
  constructed; the latest fields, like `Tgw::Runtime`'s log, cover only the
  last of them.
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
tap is final before reading it.

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
		const auto r = Test::ReadWidgetOps(*state->tap);
		const auto final = (r.factoriesReleased == r.factoriesSelected)
			&& (r.wrappersDestroyed == r.wrappersBuilt);
		return final || (crl::now() - state->removedAt > kJoinBound);
	},
	.then = [=] {
		const auto r = Test::ReadWidgetOps(*state->tap);
		Test::Check(
			(r.factoriesReleased == r.factoriesSelected)
				&& (r.wrappersDestroyed == r.wrappersBuilt),
			u"the message's hosts are gone"_q);
		Test::Check(
			(r.batches > 0) && !r.scanGaps && !r.leaks,
			u"every validated batch was scanned and none leaks"_q);
	},
	.timeout = kJoinBound + Test::kDefaultStageTimeout,
});
```

A conformance widget's result is waited for on the latest reading, parsed by
the caller and kept with the generation it came from:

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
		Test::Check(
			latest.generation == 1,
			u"one host ran the widget"_q,
			u"generation=%1"_q.arg(latest.generation));
	},
	.timeout = kResultBound + Test::kDefaultStageTimeout,
});
```

A stress widget's values are read once, after a measuring window whose
endpoints are `crl::profile()` stamps; the receipts place each value:

```cpp
runner->add({
	.name = u"read the window's data-stress values"_q,
	.run = [=] {
		const auto latest = Test::ReadWidgetOpsLatest(*message->tap);
		Test::Check(
			(latest.generation == 1) && !latest.stressDropped,
			u"one host, every data-stress value kept"_q);
		for (const auto &entry : latest.stress) {
			if (entry.receivedAt >= state->windowStart
				&& entry.receivedAt <= state->windowEnd) {
				state->windowValues.push_back(entry.value);
			}
		}
		state->maxBatchOps = latest.maxBatchOps;
	},
});
```

The self-checks are run from one stage each and turned into checks by their
caller; the probe check gets the bytes of the pinned `probe-fixture.json`:

```cpp
runner->add({
	.name = u"widget ops self-check"_q,
	.run = [] {
		for (const auto &row : Test::RunWidgetOpsSelfCheck().rows) {
			Test::Check(row.passed, row.name, row.details);
		}
	},
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
thread. Counts cover every renderer built for the message while it was armed
and are never reset; only `trustedEvents`, `trustedClicks`,
`trustedEventTypes` and the press readings (`markedGeneration`,
`markedWork`, `markedSerial`, `markedPress`, `downsConfirmed`,
`upsConfirmed`, `clicksCompleted`, `pressStepsElsewhere`,
`markSuperseded`, `markDetached`) restart at `MarkWidgetOpsEvents()`, which also marks the
generation it finds newest as the interaction they read. Its press is the
first primary press the chat dispatches to a tapped work of that generation,
hosted or local ([Local presses](#local-presses)), while
`DeliverMarkedPress()` delivers the fixture's own mouse-down
(`WidgetPressDispatched()`, bound to its work and pointer serial at that
dispatch); the binding is sealed when that delivery returns, bound or not.
Counts are uncapped;
the example lists (`leakExamples`, `noteExamples`, `gapExamples`,
`violationDetails`, `limitExamples`, `trustedEventTypes`) keep only their
first 64 entries, so
compare a list with its count before calling it complete.

| Field | Meaning |
| --- | --- |
| `factoriesSelected` | Wrapping factories handed out at the main-thread seam. |
| `factoriesCompleted`, `factoriesThrew` | Product factory calls that returned or threw; a throw is counted and rethrown to `WidgetHost`, never swallowed. |
| `factoriesReleased` | Wrapping factories whose last copy is gone, called or not. |
| `wrappersBuilt`, `wrappersDestroyed` | Tap renderers; one counts as destroyed only after its product renderer is. |
| `hostMessages` | Host to renderer messages of every type. |
| `batches`, `ops` | `ops` messages with an `ops` array, and their ops. |
| `scanGaps`, `gapExamples` | Observations not taken, each with its reason and the first 160 bytes: unscanned host messages, host and renderer messages past the scan budget, result values and theme messages too long to keep, helper scheme records too long. |
| `leaks`, `notes` and examples | The copied scanner's own counts, every match, and texts: per message the first 64 within 64 KiB, each place cut to 240 bytes. |
| `violations`, `violationDetails` | Renderer `violation` messages and their `detail`. |
| `limitReports`, `limitExamples` | Renderer `limit` reports, each counted before it is forwarded, and their `code` and `count`: a string code bare, any other code and every count as JSON text, a missing member as `null` (`paint-limit 17`, `paint-limit "17"`). |
| `trustedEvents`, `trustedClicks`, `trustedEventTypes` | Renderer `event` messages with `trusted` true since the mark, those whose `ev.type` is `click`, and their types; of the marked generation alone once a mark was made, and only while no newer generation was selected. |
| `markedGeneration`, `markedWork`, `markedSerial`, `markedPress` | The generation the mark found newest; the work and pointer serial of the first primary press the chat dispatched to a tapped work of that generation, hosted or local, inside `DeliverMarkedPress()`, the synchronous delivery of the fixture's own mouse-down (`WidgetPressDispatched()`, called by `RendererCore::pointerPress()`; 0 when none was, and then for good, because the binding is sealed when the delivery returns): the press the fixture played, whatever press the work still had queued; and, hosted only, the number that press got when its work sent it (`WidgetPressObserved()`'s `Sent` carrying that serial; 0 before it, and always 0 locally): the marked interaction. A press dispatched before or after the delivery, a genuine later click of the same work after a mouse-down that reached no core included, and one to another generation's work bind nothing. |
| `downsConfirmed`, `upsConfirmed`, `clicksCompleted` | Of the marked interaction, while the marked generation is the newest. Hosted: its down and its up answered by the helper's renderer while the route was still active (delivery only), and the renderer's click decision for that up, which the helper reports with the `Clicked` control after the up's reply, counted only after the up. Local: its down taken by the work's renderer, the release that ended that press, and the trusted `click` the renderer answered that release with, counted when the work's forwarding of that answer handed it to the wrapper, so the click the host got, while its core was not yet detached ([Local presses](#local-presses)). Locally a generation-wide trusted click is informative only: only the forwarded click of the played press's own release completes it. A down or up, even both answered, is never a click: an up whose target the down's handler replaced gets its reply and no decision, a release its answer and no click. `clicksCompleted` is the hosted click reading of a Release harness, whose `trustedClicks` stays 0; with the Debug tap, and locally, `trustedClicks` counts the generation's trusted clicks whatever press made them, and only `clicksCompleted` is the played press's own. |
| `pressStepsElsewhere` | Steps of every other press since the mark: a press the chat dispatched before the mark that the work sends (or locally takes) after it, an earlier press of the same work that completes after it, a later press (also after a sealed, unbound mark), an older generation's press on the same tap, a click before its up, locally the release of a later down of the same work and its click, every step of the marked press after a newer generation was selected, and every step of it after its core was detached or, hosted, its host retired (`markDetached`). |
| `markSuperseded` | The generation whose selection superseded the marked one, 0 while the marked one is the newest: the mark's later steps and trusted events stop counting, and what it counted before stays. |
| `markDetached` | Whether the core of the work the mark is bound to, hosted or local, was detached (`WidgetCoreDetached()`, first in `RendererCore::detach()`, before it closes the host link and before the runtime releases the route or retires the host) before the played press's click completed, or, hosted, the runtime retired that work's host (`WidgetHostRetired()`) before the helper reported its renderer's click for the played press, while the widget render thread, which learns of the detach only later, can still report the up and click of a reply it already took. Either way the mark is revoked, its later steps count elsewhere and no click of it completes it, also one a lookup that held the registration brings later, while what it counted before stays. A click completed before keeps it false. |
| `ackQueries`, `ackResults` | The host's post-load `query` and the renderer's `result`, each with numeric `qid` -1 (`WidgetHost`'s `kRenderAckQid`). |
| `fatalFrozen` | Host `frozen` messages with `fatal` true, sent when the runtime died for any reason but `terminated`; for a hosted host every `onDeath` instead, whatever its reason's text ([Hosted widgets](#hosted-widgets)). |
| `renderedHosts` | Hosted hosts whose own `WidgetDiagnostics::rendered` the tap saw, each once: the served host's post-load query answered by the helper's renderer, as the hosted client recorded it. The hosted twin of an `ackQueries` / `ackResults` pair, and the one a Release harness has. |

The message shapes are the ones `WidgetHost` sends and reads in widget-sandbox's
`widget_host.cpp` (`toRenderer()`, `onLoaded()`, `onDied()`,
`handleRendererMessage()`), with `docs/protocol.md` for `RenderOp`: a
validated `el` carries `attrs` as `[name, value]` pairs, which is what the
scanner reads. TwidgetCore marks every event it forwards `trusted` except the
`pointerdown` and `mousedown` of a real finger's press (widget-sandbox
v0.3.0's finger rule); mouse presses and a tap's `click` stay trusted.

A reading is final only when `factoriesReleased == factoriesSelected` and
`wrappersDestroyed == wrappersBuilt`: then no factory selected for the message
can still build a renderer, and no tap renderer can still receive a batch.
`wrappersBuilt == wrappersDestroyed == 0` alone is not that: a Build worker may
not have called its factory yet. Readings before that point are diagnostic.
For a hosted host the same equality means that no hosted tap can still be
installed and that every host it was installed on is destroyed, so none of
its observers can record more; a renderer the helper kept past that host is
outside the reading.

### Local presses

Where the renderer runs in Telegram (`in-process`, `helper-engines`,
Windows and Linux), `Tgw::Runtime::construct()` calls
`TapWidgetRenderer(_item->globalId(), _live->workId(), _live->hostFactory())`
on main: for an armed id the selection also registers the work of the core
it builds the host for with the generation it made, from that selection on
and for as long as the wrapping factory or the wrapper it built lives (the
host releases the factory once it built its renderer, and the wrapper dies
with the host). A work id is never reused, so a late registration cannot
take a newer core's place. The two-argument form, which the self-checks and
the conformance probe check use, registers none. Five product seams reach
a registered work's tap, and the wrapper reports each renderer message it
forwards to the host with the work it was built for:

| Seam | Production call | What the tap records |
| --- | --- | --- |
| `WidgetPressDispatched(work, serial)` | `RendererCore::pointerPress()` in `tgw_renderer.cpp`, on main, as for a hosted work ([Hosted widgets](#hosted-widgets)) | The same binding: while `DeliverMarkedPress()` delivers the mark's mouse-down, the first press to a work of the marked generation binds `markedWork` and `markedSerial`. |
| `WidgetLocalPressDown(work, serial)` | `RendererWork::pointerPress()` in `tgw_renderer_work.cpp`, on `widget.render`, when the work took a primary mouse down and gave it to its renderer, with the serial the chat dispatched it with | For the bound work of the marked generation while it is the newest, the press the work's next release ends, and `downsConfirmed` when `serial` is `markedSerial`; any other down counts in `pressStepsElsewhere`. |
| `WidgetLocalPressReleased(work, out)` | `RendererWork::releasePointer()` in `tgw_renderer_work.cpp`, on `widget.render`, when that primary press ends with an up, or with a cancel when the chat took it, with the renderer's answer before the work forwards it | When the press it ends is the bound one, `upsConfirmed`, and, when the answer holds a trusted `click` event (read as `trustedClicks` reads one), that release's forwarding opens: the first trusted click the work's wrapper then hands the host counts in `clicksCompleted`. Every other release counts one in `pressStepsElsewhere`, and its click one more. |
| `WidgetLocalPressForwarded(work)` | `RendererWork::releasePointer()` right after it forwarded that answer through the host link (to the live host's renderer callbacks, where the wrapper sees it, or nowhere once the link closed), for every mouse release the renderer took | Ends the release's forwarding: a click of the played press that did not reach the wrapper by then, the link closed or the work not yet ready, never counts, and a trusted click handed the wrapper later is no click of that press. |
| `WidgetCoreDetached(work)` | `RendererCore::detach()` in `tgw_renderer.cpp`, on main, first, before it closes the core's host link, for every core (`Runtime::retire()` and `~Runtime()` detach a live core before dropping, keeping or releasing it); a hosted work's tap takes it too ([Hosted widgets](#hosted-widgets)) | Under the tap's lock, revokes a mark bound to that work whose click has not completed (`markDetached`): a down or release of the work reported later counts in `pressStepsElsewhere` alone, also one that a lookup made before the detach brings to the tap after it, and no click completes the mark. A click completed before the detach stays; another work's detach changes nothing. |

A press the work drops without a release (detached, stopped, off screen)
reports nothing, and its next press starts over. So only the played
press's own release can complete the mark: not the release of a later down
of the same work, not a press the chat dispatched before the mark and not a
genuine click of the same work after a mouse-down that reached no core. A
new mark, a newer selection and the end of the registration each leave an
earlier down unable to complete it. `markedPress` stays 0 locally.

The click counts only as the host got it. The release's forwarding hands
each message to the wrapper on `widget.render` under the host link's lock
(`HostLink::toHost()`), and `RendererCore::detach()` calls
`WidgetCoreDetached()` on main, under the tap's lock, before it closes the
link: a click handed the wrapper before that call completed the mark, one
handed it between the call and the close no longer counts, and once the
link is closed nothing reaches the wrapper. So when retirement closes the link
while the played press's up is still queued, and a host sample still holds
the wrapper and with it the registration, that up finds the mark revoked
and its click reaches no host: the mark stays incomplete whatever trusted
click an earlier press of the work sent. `trustedClicks` still counts every
trusted click the generation's renderer sent its host, whichever press
made it, and is informative only: the attack audit's `renderer` reader
needs both ([test_widget_attacks](test_widget_attacks.md)). The seams grant
nothing and send the helper nothing.

### The latest result

`ReadWidgetOpsLatest(tap)` returns a `WidgetOpsLatest` snapshot taken under
the same lock, safe from any thread. Every field but `serial` and
`staleUpdates` describes the host of `generation` alone. `stress` is copied
whole, under the lock the host thread merges into: a level's few hundred
short values cost little, but a reader polling far more often than the
widget publishes adds that copy each time.

| Field | Meaning |
| --- | --- |
| `generation` | The 1-based count of wrapping factories selected for the message when the newest was; 0 before any. |
| `adopted`, `htmlId` | Whether an `el` op with `"adopt":"html"` reached this host's renderer, and its `id`. |
| `conformance` | The last `data-conformance` value: the adopting op's attribute, replaced by each later `attr` op on `htmlId`. Empty before the first value, from a newer selection on, and after a later adoption without one. |
| `updates` | `data-conformance` values this host published; the probe fixture's `updates`. A clear is not a value. |
| `serial` | Advances on every change of `conformance`, a clear included; never reset. A `data-stress` value never moves it. |
| `stress` | Every `data-stress` value on `htmlId` (`value`, raw) with its `receivedAt`, in order: the adopting op's attribute, then each later `attr` op. Cleared by a later adoption and a newer selection. |
| `stressBytes` | The bytes of the kept `stress` values, at most 4 MiB. |
| `stressDropped` | `data-stress` values that found `stress` holding `kWidgetStressValuesLimit` (4096) or 4 MiB; nonzero means `stress` is incomplete. |
| `batches`, `ops`, `maxBatchOps` | This host's `ops` messages, every one counted; their ops (`int64`); the ops of the largest. Restart with a newer selection only. |
| `scheme` | The theme argument of this host's factory, then each `setTheme()`; empty until its wrapper is built. Of a hosted host the helper tap's report of its renderer's (way `b`, then `t`), and in a Release harness `effectiveScheme` of the newest kept effective reading, never the theme the runtime asked for: empty before the first. |
| `schemeChanges` | `setTheme()` calls of this host that changed `scheme`; a repeated scheme is forwarded but not counted. In a Release harness, kept effective readings that changed it (the first sets it uncounted). |
| `themeMessage` | The raw `{"type":"theme",...}` message last sent to this host's renderer: the bubble's palette, button and text tokens. Of a hosted host the served host's (way `h`), and in a Release harness `{"type":"theme","theme":{"colorScheme":...,"params":...}}` built from the newest kept effective reading, with no button or text tokens, which a widget cannot read back. |
| `appliedScheme`, `appliedThemeMessage` | Of a hosted host, the theme the runtime applied to it, recorded once the application returned: the one its constructor got, then each one `ApplyTheme()` gave it. The scheme as the host keeps it (`dark`, else `light`) and the theme message that theme gives the helper's renderer, built with the SDK's normalization as `WidgetHost::themeMessage()` builds it (the checked palette under the 15 theme keys in their order, then the set button and text tokens). The expectation, never a reading of the widget. Empty for a local host. |
| `appliedThemes` | Themes the runtime applied to this hosted host, its construction's included: the serial of the newest. |
| `effectiveScheme`, `effectiveParams` | Of a hosted host, what the widget itself read after the runtime applied a theme, probed in it (`TelegramWidget.colorScheme` and the JSON of `TelegramWidget.themeParams`): for the constructor's theme by the runtime's job `WidgetThemeObservation()` gives it, once the widget loaded, for a later one right after `WidgetThemeApplied()`'s application ([The effective theme](#the-effective-theme)). Kept only for the newest applied theme. |
| `effectiveMismatch` | Empty when the kept reading has the scheme of the theme it was probed for and, for each key of that theme's checked palette, its value; else the first difference. |
| `effectiveError`, `effectiveMissed` | The last probe that kept no reading, with its reason and the start of the answer, and how many there were; an application or a reading its host published after the runtime retired it counts here too. |
| `effectiveThemes`, `effectiveSerial` | Effective readings kept for this hosted host, and the `appliedThemes` serial the newest was probed for. |
| `themePending` | Of a hosted host, the token of the application of the theme the runtime wants it to have and has not yet recorded, from the moment the runtime selected that theme (`WidgetThemeSelected()` while another theme's job runs, else `WidgetThemeScheduled()`), 0 when none is underway. The host may already read part of the new theme while it is set, or still be given an older one, so the generation's theme is unobserved however good its last reading was ([The effective theme](#the-effective-theme)). |
| `hostRetired` | Of a hosted host, whether the runtime retired it (`WidgetHostRetired()`): no application or reading its jobs publish after that is kept. |
| `staleUpdates` | Messages with an adoption, a `data-conformance` or `data-stress` attribute or a theme, `setTheme()` calls and wrapper builds of an older host after a newer one was selected; ignored above. An older host's batch without such an op is not counted here. |

The result fields follow `CLIENTS.md` and twidget's reference runner
(`tools/conformance`, `OffscreenRenderer::probe()`), apart from two
deliberate clears: a newer host's selection and a later value-less adoption
leave no value, where a reader that only remembers the last published value
would keep the older one. Neither happens in a healthy run; a caller that
sees `generation` above 1 names it rather than trusting a result or a
runtime log to cover every host.

## Hosted widgets

Where the widget helper renders (macOS, the Mac App Store build included,
with no switch, `helper` or a probe value; [Remote engines](test_widget_fixture.md#remote-engines)),
`Tgw::Runtime::construct()` builds a hosted `WidgetHost`: its engine,
validator, served host and TwidgetCore renderer run in the helper, and
Telegram keeps only the hosted client and its checked Qt work. The runtime
then calls `TapHostedWidget(_item->globalId(), _live->workId(), *_theme)`
on main instead of `TapWidgetRenderer()`, with the `RendererCore::workId()`
of the core it builds the host for (counted up from 1 on main and never
reused, so no later core shares it) and the theme its `Job` carries into
the host's constructor. For
an armed id it selects the next generation at once and returns a function
that `Build()` calls on its worker, once, with the hosted host's shared
pointer, after its constructor returned and the runtime set its
observers, before the host is bound or started. That call records that
theme as the generation's first applied theme (`appliedScheme`,
`appliedThemeMessage`, serial 1), registers the host and the work id for
the runtime's three other hosted seams below, and
wraps four observers of that host, each recording first and then calling
the runtime's own:

| Observer | What the tap records |
| --- | --- |
| `onRenderOps` | Every validated batch, as the helper's validator passed it, rebuilt into the host's own `{"type":"ops","ops":...}` message and scanned as the wrapper's `send()` scans one: leaks, notes, results, `data-stress` values with their receipts and the newest host's batch counts. Set before `start()`, it is also the hosted start's ask for the batches, which a host without the observer never gets. The served host's `WidgetHost::localSetTheme()` reports the batch a theme change re-sanitises the stylesheets into as well, when it is not empty. |
| `onPlatformMessage` | The helper tap's `Mirrored` records, consumed only when well-formed and only after the tap asked for them: way `b` (the scheme the renderer was built with, the latest `scheme`, once), `h` (every served-host message to the renderer but `ops`, such as the theme message, the ack query, layout queries, `requestFrame` and `frozen`), `r` (every renderer message to the served host, such as the ack's `result`, events, `violation`, `limit` and `size` reports) and `t` (each `setTheme()`). Every other message, a malformed `Mirrored` record included, goes to the runtime's observer unchanged, in order, inside the same call, so `WidgetHost::platformDelivery()` still gives it its delivery. |
| `onDeath` | The hosted host's death as its diagnostics recorded it, once: every one counts one `fatalFrozen`, whatever its reason's text (a widget helper can report `terminated` for a death nobody asked for), a kill of the client's own (the helper ended, a flood, a platform request left unanswered) included. `WidgetHost` itself never reports the end its owner asked for (`destroy()`, `keepRenderer()`), so a retirement counts none. The served host's own `frozen`, mirrored as way `h`, is a host message but never a death, so a death is never counted twice. |
| `onChange` | The host's own rendered acknowledgment: the first change whose diagnostics have `rendered` counts one `renderedHosts`; the observer probes nothing ([The effective theme](#the-effective-theme)). Every change is passed on unchanged; the watch of [test_widget_host](test_widget_host.md), when armed, wraps this observer in turn. |

The runtime's other hosted seams reach the installed tap through a
registry of the hosts and work ids it was installed for, each entry living
as long as the host's observers:

| Seam | Production call | What the tap records |
| --- | --- | --- |
| `WidgetThemeObservation(host)` | `Runtime::constructed()` in `tgw_runtime.cpp`, on main, for each host it starts; the function it returns, kept in the runtime's `Host`, runs once as one of that host's jobs on a worker after the widget loaded (`Runtime::syncHostTheme()`), never beside a theme application or a keep of that host, and is dropped unrun when a newer theme or the host's retirement makes it obsolete | An empty function for a host with no installed tap. Run, the widget's own reading of the theme the host was built with, for serial 1 ([The effective theme](#the-effective-theme)); a reading after a newer theme was applied is superseded, and one that comes back after the host's retirement is revoked: neither is kept. |
| `WidgetThemeSelected(host, theme)` | `Runtime::syncHostTheme()` in `tgw_runtime.cpp`, on main, instead of starting a job while the host's theme job still runs (its busy guard), with the theme the runtime now wants; the job for it starts when the running one ends, or never when the running one gives it | Nothing for a host with no installed tap or a retired one; else the application that gives `theme` is pending from now on (`themePending`): the running job's when that job gives `theme` (none when it already recorded its application), else a new token, which `WidgetThemeScheduled()` hands to the job for `theme` when it starts. An application of another theme, the running job's included, never ends it. |
| `WidgetThemeScheduled(host, theme)` | `Runtime::syncHostTheme()` in `tgw_runtime.cpp`, on main, right before it starts the job that gives the host a new theme (`ApplyTheme()`), which carries the returned token | 0 for a host with no installed tap or a retired one; else the token, unique in the tap, of an application now pending (`themePending`, with the theme message it gives; the token `WidgetThemeSelected()` made pending for that message, when it still is): the theme is unobserved from now on until that application is recorded. |
| `WidgetThemeApplied(host, theme, token)` | `ApplyTheme()` in `tgw_runtime.cpp`, on its worker, right after it gave the host the new theme behind its barrier (an apply, a probe, an apply again), with the job's token | The applied theme, one more `appliedThemes` (ending the pending application only when `token` is the pending one and `theme` gives its message, so an older job never ends a newer one), then, on that worker with no lock held, the widget's own reading of it for that serial ([The effective theme](#the-effective-theme)); after the host's retirement neither; a host with no installed tap (a local one, another message's) records nothing. |
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
marked interaction's press steps and the death only, and
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

How the readings differ from the local tap:

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
- **Order and time.** Each record crosses the stream after the helper did
  the work, so a renderer message that a batch caused can precede that
  batch's `onRenderOps` record, and every receipt (`receivedAt` of a
  `data-stress` value) is taken on Telegram's `twidget-host` thread after
  the helper's validation and both stream hops. Compare receipts and
  counts only between runs of one arrangement.
- **What is not seen.** An empty re-themed batch is neither reported nor
  mirrored, while in process `{"type":"ops","ops":[]}` reaches the renderer
  and counts as a batch of no ops. A served-host or renderer message too
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

### Helper renderer work

The work a hosted widget's renderer does in the helper (its receive, style,
layout, ticks and display-list builds) is outside Telegram's work log
([test_widget_host](test_widget_host.md#in-the-hosted-arrangement)). The
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
  clock on macOS, so the mapping is the two processes' offset taken then),
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
  wall time without Telegram's answers, never both. A `Paint` with `frameBytes` above 0 sent the `Frame` whose
  replay is the next `Frame` event of that host's work log after its
  `finished`, its `seq` the one Telegram's request carried. A stress level
  attributes the renderer's work only from a known nontrivial workload with
  both readings nonempty, against an unasked level of the same widget.

## Threads and lifetime

- `TapWidgetRenderer()`, `ArmWidgetOpsTap()` and `DisarmWidgetOpsTap()` run on
  the main thread, as `Runtime::construct()` does; the armed map, keyed by
  `GlobalMsgId`, is main-only.
- The wrapping factory runs on the host's Build worker and calls the product
  factory there, once, with the same resources, theme and width. It captures
  the product factory, the tap and its selection's index, nothing else: no
  `WidgetHost` pointer and no weak pointer. Selecting it on main starts the
  latest reading's new generation; the wrapper it builds records the theme
  argument as that generation's scheme.
- The tap renderer is the shared `ForwardingRenderer`
  (`test_widget_renderer_forward.h`), which the helper's Debug tap wraps its
  glue's renderer with too: one implementation of the forwarding, the
  callback chaining and the destruction order, with each tap's recording in
  its own observer. It forwards `load()`, `send()`, `setTheme()`,
  `setWidth()` and `destroy()` directly on the host thread, in order, with
  no queue; `send()` scans first and then forwards the very same string, and
  `setTheme()` records the scheme first and then forwards it. Renderer callbacks are
  forwarded on whatever thread the product renderer calls them from
  (`HostLink` calls them on the widget render thread, where
  `Tgw::RendererWork` forwards its answers, under its own mutex), reading
  the tap's own `onMessage` / `onDiag` at call time, because `WidgetHost`
  assigns those after the factory returned.
- The tap's record is one mutex shared by those threads. Parsing and scanning
  happen outside it, and nothing calls out while holding it, so the lock order
  `HostLink` mutex, then tap mutex, has no reverse path.
- `WidgetHost::destroy()` calls the tap's `destroy()` on the host thread, which
  forwards it; `HostRenderer::destroy()` detaches from its link. The host is
  deleted on a `HostDeleter` worker: the forwarding's destructor destroys the
  product renderer first, then its observer, which counts the wrapper as
  destroyed.
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

## Lifetime and cleanup

`ArmWidgetOpsTap()` returns a shared tap that the armed map, every selected
factory and every wrapper keep. `DisarmWidgetOpsTap()` only stops future
selections; keep the caller's tap until its reading is final. Nothing is
written to the account, the disk or the clipboard. A scenario that arms ids
disarms them on its finish path (`Runner::onFinish`); an armed id that is never
constructed holds only its empty record.

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
differs from the reference `--attacks` run only in where it sits: at
`Renderer::send`, so it also sees the theme-replay batches that the reference's
`onRenderOps` never sees. A hosted host's scan sits at `onRenderOps`, as the
reference's does, and sees those batches too, because the served host now
reports every non-empty validated batch there.

## Release and inactive identity

The production calls are in `Tgw::Runtime::construct()` (`tgw_runtime.cpp`),
one per construction, each passing `_item->globalId()`: the `.factory`
initializer's `TapWidgetRenderer()` with the core's work id for a local
host, and for a hosted one
`TapHostedWidget()` with the core's work id and the theme, whose function
`Build()` calls once on the constructed host when it is not empty; then
`WidgetThemeObservation()` once per started host in `Runtime::constructed()`,
`WidgetThemeScheduled()` in `Runtime::syncHostTheme()` right before each
theme job starts and `WidgetThemeSelected()` there when a running theme job
holds the next one back, `WidgetThemeApplied()` once at the end of every
`ApplyTheme()` (`tgw_runtime.cpp`), after its barrier, with that job's
token, `WidgetHostRetired()` in `Runtime::retire()` and `~Runtime()` when
they give up the live host, `WidgetPressDispatched()` in
`RendererCore::pointerPress()` (`tgw_renderer.cpp`) for every primary press
it posts, `WidgetPressObserved()` from the hosted input's delegate
(`RendererWork::makeHosted()` in `tgw_renderer_work.cpp`),
`WidgetLocalPressDown()` in `RendererWork::pointerPress()` there and
`WidgetLocalPressReleased()` and `WidgetLocalPressForwarded()` in
`RendererWork::releasePointer()` for a local work, and
`WidgetCoreDetached()` in `RendererCore::detach()` (`tgw_renderer.cpp`)
for every core. Without the
harness, in a build that is neither Debug nor configured with
`TDESKTOP_RELEASE_TEST_HARNESS`
([Release builds with the harness](../launch.md#release-builds-with-the-harness)),
the module defines only these fourteen: in `test_widget_ops.cpp` both forms
of `TapWidgetRenderer()`, returning their `factory` argument, and
`WidgetLocalPressDown()`, `WidgetLocalPressReleased()` and
`WidgetLocalPressForwarded()`, doing nothing, and in
`test_widget_ops_hosted.cpp` `TapHostedWidget()` and
`WidgetThemeObservation()`, returning an empty function,
`WidgetThemeScheduled()`, returning 0, and `WidgetThemeSelected()`,
`WidgetThemeApplied()`, `WidgetHostRetired()`, `WidgetPressDispatched()`,
`WidgetCoreDetached()` and `WidgetPressObserved()`, doing nothing. `test_widget_ops_check.cpp`
compiles to nothing there, and every other declaration of the header is
left undefined. With the harness, an inactive harness and an unarmed id
return the same before any observation state is allocated, and the later
seams find no registered host or work.

## Verification

`Test::RunWidgetOpsSelfCheck()` drives `TapWidgetRenderer()` with a stub
product renderer under message ids of peer 0, which no real message of any
session has, and disarms them before it returns:

- the unarmed id gets the stub factory itself (`std::function::target`), and
  the armed id a wrapping factory that calls it once, with the same arguments,
  on its caller's thread;
- the wrapper installs the stub's callbacks and leaves its own for the caller
  to assign after the factory returned;
- every method and both callbacks forward once, in order, with the exact
  bytes and the same string object, on the caller's thread; a reply the stub
  sends from inside `send()` arrives before `send()` returns; callbacks from a
  joined `std::thread` arrive on that thread;
- the scanner control: one `img` with an `https://` `src` and a
  `url(https://...)` style gives exactly the two leaks, a `data:` attribute
  holding an `http://` namespace exactly one note, and the `url(data:...)` and
  `url(#g)` negatives nothing; a following `rm` batch keeps those findings with
  two batches and five ops;
- an unparsable message and an `ops` message without an array are two scan
  gaps; the ack query, its `result`, a fatal `frozen`, a `violation` (not a
  leak) and trusted events since the mark (an untrusted click and a press
  before the mark excluded) are read;
- two `limit` reports the stub sends after a reading of none,
  `paint-limit` 17 then `backdrop-limit` 3, count twice with the examples
  `paint-limit 17` and `backdrop-limit 3`, are not violations, and reach the
  caller's callback once each, in order, with their exact bytes (also part
  of the forwarding sequence above);
- destroying the wrapper destroys the stub first, and releasing the factory
  completes the counts; a disarmed id gets the stub factory itself again; a
  throwing stub factory is counted and its exception reaches the caller;
- one `FullMsgId` under two session values: with only the first armed, the
  second gets the stub factory itself and the first tap counts nothing;
  with both armed, two taps read the control batch (2 leaks) and a removal
  batch (no leak) apart; disarming the first leaves the second wrapping new
  factories and its wrapper recording;
- the stress leg (rows named `stress: ...`): an older wrapper adopts
  `<html>` (id 2) with a `data-stress` value, then a newer factory is
  selected, which clears that host's values and counts before the newer
  wrapper is built. The newer wrapper gets a one-op adopting batch with a
  value and batches of 3 and 5 ops with an `attr` value each (the 5 also
  hold a value on another element and a `rattr`, both ignored): 3 batches,
  9 ops, a largest of 5, the three literal values in order with receipts
  non-decreasing and inside the sends' own `crl::profile()` bounds,
  `conformance`, `updates` and `serial` untouched. The older wrapper's late
  6-op batch with a value on id 2 is forwarded once and scanned (the
  cumulative reading grows by 1 batch and 6 ops) but changes none of the
  newer host's counts or values and adds one `staleUpdates`. A 2-op batch
  without a value counts (4 batches, 11 ops). 4094 more single-op values
  fill `stress` to 4096 with exactly 1 dropped, the kept values unchanged
  and in order. A 2-op re-adoption as id 7 followed by a value on id 2
  clears the values and the drop, keeps counting the batch, and ignores the
  value on the earlier id;
- the hosted leg (rows named `hosted: ...`) builds a hosted `WidgetHost`
  without a channel, never started, gives it stand-ins for the runtime's
  three observers and calls the observers the tap installed directly, on
  main, as its host thread would: an unarmed id gets no tap; an armed one is
  selected at once as generation 1; a second install does nothing, and the
  function's last copy releases it while the reading is not final yet; the
  install records the theme the host was built with (dark, an upper-case
  colour and a key of no theme, one button and one text token) as applied
  theme 1, its message equal to a literal written out from
  `WidgetHost::themeMessage()`, and leaves the renderer's scheme and theme
  message to a reading, the theme unobserved until then; the control
  batch's `ops` array gives the control's 2 leaks and 1 note as one host
  message and reaches the stand-in unchanged; a plain message and a
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
  overflow at either limit; an `onDeath` reading `terminated` and the next death count one
  each, both reaching the stand-in; of the effective readings, one for a
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
  makes the reading final and no step of its work's presses counts after
  it; a tap dropped uninstalled counts as completed and released with
  nothing built; two messages' taps installed in both completion orders
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
- the local press leg (`local: ...`): an unarmed id's work registers for no
  tap; one message's two local generations, their works registered by
  `TapWidgetRenderer()` with the work, their wrappers built and the
  factories released first, marked on the newer and driven through
  `WidgetPressDispatched()`, `WidgetLocalPressDown()`,
  `WidgetLocalPressReleased()` and `WidgetLocalPressForwarded()`, each
  played press dispatched inside `DeliverMarkedPress()` and each clicked
  release's trusted click handed the wrapper inside its forwarding where
  the row sends one: the older's press taken before the mark and
  clicked after it counts elsewhere (2) and binds nothing, and the newer's
  played press released without a click reads 1, 1, 0; after another mark
  a press dispatched before it, taken and clicked after it, counts 3
  elsewhere beside the generation's trusted click (1) while the played
  press that clicks none reads 1, 1, 0; after another a press dispatched
  before the delivery, a delivery that reached no core, two genuine clicks
  of the same work and a second delivery bind nothing (6 elsewhere, one
  trusted click); after another the played press taken with no release
  reads 1, 0, 0, and a later down of the same work makes the next clicked
  release that press's (3 elsewhere, still 1, 0, 0); after another the
  played stable click reads 1, 1, 1 with its trusted click through the
  wrapper, and a release after it 2 elsewhere; after another a third
  selection between the played down and its release supersedes the mark
  (`markSuperseded` 3, the release and its click 2 elsewhere, no trusted
  click); last a later generation's release after its wrapper was
  destroyed and a factory's down and release after its last copy was
  dropped (it bound inside its delivery before any build) count nowhere,
  and the reading is final;
- the local retirement leg (`local retirement: ...`, then `local: a played
  release whose click reached no wrapper ...`): one message, a generation
  per case, each the newest when marked, its wrapper held while the
  product's `WidgetCoreDetached()` stands for `RendererCore::detach()`
  (nothing is handed the wrapper after it): a press
  taken before the mark and clicked after it gives the generation's one
  trusted click and 2 steps elsewhere, then the played down is taken, the core
  is detached and the played up, queued across it, reaches the seam
  through the registration the held wrapper keeps (one wrapper built, none
  destroyed): 1, 0, 0 with 4 elsewhere and `markDetached`; on the next
  generation the release that a lookup made before the detach brings
  straight to the tap after it, with a trusted click handed the wrapper
  after the detach, reads 1, 0, 0, 2 elsewhere and `markDetached` beside
  one trusted click; on the next a played release taken before the detach,
  whose click had not reached the host when the link closed, reads 1, 1, 0
  and `markDetached`; on the next the detach of the first generation's work
  and of a work no tap registered changes nothing, and the played click
  forwarded to the wrapper before its core's detach stays 1, 1, 1 with
  `markDetached` false; last a played release whose click reached no
  wrapper during its forwarding (the link closed or the work not ready)
  reads 1, 1, 0 even with a trusted click handed the wrapper afterwards,
  and the reading is final once the wrappers are gone;
- the budget leg (`a host and a renderer message past the scan budget ...`):
  a host message and a renderer `violation` one value past
  `kMaxScanValues` are two gaps, never parsed (no violation counted), and
  the same messages exactly at it are read (one violation, `at`); then a
  batch with 100 KiB attribute and style names and 164 leaks counts all
  164, keeps exactly 64 examples within 64 KiB, each place cut to 240
  bytes, the first one's text checked exactly, and adds no gap.

Every expected value is a literal of the self-check, not derived from the tap.

`Test::RunWidgetConformanceProbeCheck(fixture)` checks the latest reading the
same way, through the real wrapper around the stub, under other peer-0 ids,
and disarms them before it returns:

- every case of the `probe-fixture.json` bytes its caller read from the
  pinned package, its messages sent one by one and the reading taken after
  each, as a runner polls it: the html id and the count of values equal the
  case's `htmlId` and `updates`, the last value parses to its `final`, the
  targets of the last `awaiting` value equal its `awaitingTargets` where the
  case has them, and every message reaches the stub once with its exact
  bytes. Every expected value is read from the fixture, never from the tap;
  the cases at `v0.1.2` are a value on the adopting op replaced by two `attr`
  ops (html id 2, 3 values, final `done`, targets of the `awaiting` one), the
  same attribute on another element ignored, and an `attr` before the
  adoption ignored;
- a replacement host, with literal expectations, both hosts adopting
  `<html>` as id 2 as fresh sandboxes of one widget do: selecting a second
  factory clears the first host's `done` before the second builds; the
  second reads its own adoption and value from 1 although the first adopted
  again with a value after the selection; then the first wrapper's late
  `done` on id 2, the scanner's control batch, a `setTheme()` to the other
  scheme and a `theme` message are all forwarded and the batch scanned
  (2 leaks), but the newer reading keeps its value, scheme and empty theme
  message, with `staleUpdates` 4; a later value-less adoption clears the
  value and the earlier html id stops matching; both hosts end final with
  all 8 batches scanned;
- the factory's theme becomes the new host's scheme, with no change counted;
  each `setTheme()` is forwarded once, a new scheme counts one change and a
  repeated one none; a `theme` message is kept byte for byte, forwarded once
  and counted as a host message but not as a batch.

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
`TDESKTOP_RELEASE_TEST_HARNESS` and without it. No row of the stress leg,
and no row since this change, has run in the app yet: they run where a
scenario calls `RunWidgetOpsSelfCheck()`. The earlier rows' expectations
are unchanged, as the conformance reading and the cumulative counts are.

`limitReports`, `limitExamples` and their self-check row were added by
`2026/11/02/stop-widgets-past-the-heavy-box-limit-with-a-v0-6-0-twidgetcore`
and compiled in the macOS Debug build. The row runs in that task's evidence
campaign, which records it; the earlier rows' expectations are unchanged
apart from the two reports' places in the forwarding sequence.

`TapHostedWidget()`, the hosted readings and the hosted leg were added by
`2026/11/14/run-widget-validators-and-renderers-in-the-macos-helper` and
compiled in the macOS Debug build (arm64 and x86_64), together with the
helper's half; no hosted row has run in the app yet, and its evidence
campaign (the attack, conformance and stress parity legs) is where the
hosted readings are compared with the `in-process` ones. The earlier rows'
expectations are unchanged. The applied and effective themes,
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
Debug build and the Release syntax checks of the three files pass. No row
has run in the app since.

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
under a probe with the earlier independent probe. No row has run in the
app since.

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
its 10 sequences, all but the control. No row has run in the app since.

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
palette. No row has run in the app since.

The task's sixth review round bound a local widget's click to the press the
fixture played as well ([Local presses](#local-presses)): the runtime
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
missed press followed by a genuine click locally. No row has run in the app
since.

The seventh round tied that click to its forwarding and to the core's
retirement ([Local presses](#local-presses)): `clicksCompleted` counts the
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
certified the retired press. No row has run in the app since.

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
certified it. No row has run in the app since.

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
that interval `observed` in Debug and `decided` in Release. No row has run
in the app since.

## Related guidance

[Scenario composition](../scenarios.md);
[scenario teardown](../teardown.md);
[chat-list show before input](test_chat_show.md);
[input helpers](test_widgets.md#input-helpers);
[launch fuse](test_launch_fuse.md);
[widget attack audit](test_widget_attacks.md);
[widget conformance runner](test_widget_conformance.md);
[the helper's half of the tap](test_widget_helper_tap.md).
