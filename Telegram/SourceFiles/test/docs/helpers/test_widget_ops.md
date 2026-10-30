# test_widget_ops

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API, with focused self-checks.
Source contract: [test_widget_ops.h](../../test_widget_ops.h).

## Use when

A scenario needs what a widget message's sandbox host really sends its
renderer: the validated ops, every batch of them, including batches a later
batch removes or replaces and the theme replay that `WidgetHost::setTheme()`
sends straight through `toRenderer()`. It arms one message, by its
session-qualified `GlobalMsgId`, before its host is constructed, and the
product renderer of every host built for that message is wrapped in a
forwarding tap at the `twidget::Renderer::send` seam. Each
`ops` batch is scanned, before it is forwarded, with a verbatim copy of
widget-sandbox's `--attacks` leak definition. The tap also counts the host's
post-load ack, the renderer's answer to it, a fatal `frozen`, renderer
`violation` messages and trusted renderer events, and it says when no factory
call or wrapper of the message is still pending.

The same scan also reads what a conformance widget publishes:
`ReadWidgetOpsLatest()` returns the last `data-conformance` value of the
adopted `<html>`, as widget-sandbox's `packages/conformance/CLIENTS.md`
("Reading the ops") reads it, with the scheme and the `theme` message the
newest host gave its renderer.

Callers: the attack-corpus audit of
`2026/10/24/audit-the-widget-attack-corpus-through-a-validated-ops-tap`, and
the conformance runner of
`2026/10/24/run-the-widget-conformance-sets-through-the-chat-list-and-report`
([test_widget_conformance](test_widget_conformance.md)), which reads its
results, scheme and theme message with `ReadWidgetOpsLatest()` and checks
that reader against the package's `probe-fixture.json` with
`RunWidgetConformanceProbeCheck()`.

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
  armed; the Release branch defines it alone, as that identity.
- **What the scan is.** A leak or a note is exactly what the copied
  `mentionsNetwork()`, `scanCssForLeaks()` and `scanForLeaks()` report for
  each op of a batch, nothing else. A URL in a non-`ops` message, a note and
  a renderer refusal are not leaks; violations are counted apart, and a
  consumer that calls a reading clean needs `violations == 0` as well.
- **Scan gaps.** A host message that is not JSON, or an `ops` message whose
  `ops` is not an array, is forwarded unchanged and counted as a scan gap,
  never as an empty batch. A clean result needs `scanGaps == 0`.
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
  and is counted in `staleUpdates`.
  `generation` above 1 names a run in which more than one host was
  constructed; the latest fields, like `Tgw::Runtime`'s log, cover only the
  last of them.
- **The value is raw.** `conformance` is the attribute text, not checked to
  be JSON; the caller parses it and can cache that by `generation` and
  `serial`. A later `el` op adopting `<html>` without the attribute clears
  it, a `data-conformance` before the adoption or on another element is
  ignored, and so is a `rattr`, as the `CLIENTS.md` reader does.

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
and are never reset; only `trustedEvents`, `trustedClicks` and
`trustedEventTypes` restart at `MarkWidgetOpsEvents()`. Counts are uncapped;
the example lists (`leakExamples`, `noteExamples`, `gapExamples`,
`violationDetails`, `trustedEventTypes`) keep only their first 64 entries, so
compare a list with its count before calling it complete.

| Field | Meaning |
| --- | --- |
| `factoriesSelected` | Wrapping factories handed out at the main-thread seam. |
| `factoriesCompleted`, `factoriesThrew` | Product factory calls that returned or threw; a throw is counted and rethrown to `WidgetHost`, never swallowed. |
| `factoriesReleased` | Wrapping factories whose last copy is gone, called or not. |
| `wrappersBuilt`, `wrappersDestroyed` | Tap renderers; one counts as destroyed only after its product renderer is. |
| `hostMessages` | Host to renderer messages of every type. |
| `batches`, `ops` | `ops` messages with an `ops` array, and their ops. |
| `scanGaps`, `gapExamples` | Unscanned host messages, with their reason and first 160 bytes. |
| `leaks`, `notes` and examples | The copied scanner's own counts and texts. |
| `violations`, `violationDetails` | Renderer `violation` messages and their `detail`. |
| `trustedEvents`, `trustedClicks`, `trustedEventTypes` | Renderer `event` messages with `trusted` true since the mark, those whose `ev.type` is `click`, and their types. |
| `ackQueries`, `ackResults` | The host's post-load `query` and the renderer's `result`, each with numeric `qid` -1 (`WidgetHost`'s `kRenderAckQid`). |
| `fatalFrozen` | Host `frozen` messages with `fatal` true, sent when the runtime died for any reason but `terminated`. |

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

### The latest result

`ReadWidgetOpsLatest(tap)` returns a `WidgetOpsLatest` snapshot taken under
the same lock, safe from any thread. Every field but `serial` and
`staleUpdates` describes the host of `generation` alone.

| Field | Meaning |
| --- | --- |
| `generation` | The 1-based count of wrapping factories selected for the message when the newest was; 0 before any. |
| `adopted`, `htmlId` | Whether an `el` op with `"adopt":"html"` reached this host's renderer, and its `id`. |
| `conformance` | The last `data-conformance` value: the adopting op's attribute, replaced by each later `attr` op on `htmlId`. Empty before the first value, from a newer selection on, and after a later adoption without one. |
| `updates` | Values this host published; the probe fixture's `updates`. A clear is not a value. |
| `serial` | Advances on every change of `conformance`, a clear included; never reset. |
| `scheme` | The theme argument of this host's factory, then each `setTheme()`; empty until its wrapper is built. |
| `schemeChanges` | `setTheme()` calls of this host that changed `scheme`; a repeated scheme is forwarded but not counted. |
| `themeMessage` | The raw `{"type":"theme",...}` message last sent to this host's renderer: the bubble's palette, button and text tokens. |
| `staleUpdates` | Messages with an adoption, a `data-conformance` attribute or a theme, `setTheme()` calls and wrapper builds of an older host after a newer one was selected; ignored above. |

The result fields follow `CLIENTS.md` and twidget's reference runner
(`tools/conformance`, `OffscreenRenderer::probe()`), apart from two
deliberate clears: a newer host's selection and a later value-less adoption
leave no value, where a reader that only remembers the last published value
would keep the older one. Neither happens in a healthy run; a caller that
sees `generation` above 1 names it rather than trusting a result or a
runtime log to cover every host.

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
- The tap renderer forwards `load()`, `send()`, `setTheme()`, `setWidth()` and
  `destroy()` directly on the host thread, in order, with no queue; `send()`
  scans first and then forwards the very same string, and `setTheme()`
  records the scheme first and then forwards it. Renderer callbacks are
  forwarded on whatever thread the product renderer calls them from
  (`HostLink` calls them on main, under its own mutex), reading the tap's own
  `onMessage` / `onDiag` at call time, because `WidgetHost` assigns those
  after the factory returned.
- The tap's record is one mutex shared by those threads. Parsing and scanning
  happen outside it, and nothing calls out while holding it, so the lock order
  `HostLink` mutex, then tap mutex, has no reverse path.
- `WidgetHost::destroy()` calls the tap's `destroy()` on the host thread, which
  forwards it; `HostRenderer::destroy()` detaches from its link. The host is
  deleted on a `HostDeleter` worker: the tap's destructor destroys the product
  renderer first, then counts the wrapper as destroyed.

## Lifetime and cleanup

`ArmWidgetOpsTap()` returns a shared tap that the armed map, every selected
factory and every wrapper keep. `DisarmWidgetOpsTap()` only stops future
selections; keep the caller's tap until its reading is final. Nothing is
written to the account, the disk or the clipboard. A scenario that arms ids
disarms them on its finish path (`Runner::onFinish`); an armed id that is never
constructed holds only its empty record.

## Leak definition and fidelity

The scanner is a verbatim block of widget-sandbox's
`packages/desktop/harness/main.cpp` at the `v0.1.2` tag, from its "Anything in
the validated ops" comment to the end of `scanForLeaks()`, `static` kept, with
`using namespace twidget` as in that file. Telegram does not build that console
tool. Compare the block mechanically with
`git -C Telegram/ThirdParty/widget-sandbox show v0.1.2:packages/desktop/harness/main.cpp`,
and copy the new block whole at every widget-sandbox pin move. The scan
differs from the reference `--attacks` run only in where it sits: at
`Renderer::send`, so it also sees the theme-replay batches that the reference's
`onRenderOps` never sees.

## Release and inactive identity

The production call is the single `.factory` initializer in
`Tgw::Runtime::construct()` (`tgw_runtime.cpp`), which passes
`_item->globalId()`. The Release branch of
`test_widget_ops.cpp` defines only `TapWidgetRenderer()`, returning its
`factory` argument; every other declaration of the header is left undefined
there. In Debug, an inactive harness and an unarmed id return the same
`factory` before any observation state is allocated.

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
- destroying the wrapper destroys the stub first, and releasing the factory
  completes the counts; a disarmed id gets the stub factory itself again; a
  throwing stub factory is counted and its exception reaches the caller;
- one `FullMsgId` under two session values: with only the first armed, the
  second gets the stub factory itself and the first tap counts nothing;
  with both armed, two taps read the control batch (2 leaks) and a removal
  batch (no leak) apart; disarming the first leaves the second wrapping new
  factories and its wrapper recording.

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

## Related guidance

[Scenario composition](../scenarios.md);
[scenario teardown](../teardown.md);
[chat-list show before input](test_chat_show.md);
[input helpers](test_widgets.md#input-helpers);
[launch fuse](test_launch_fuse.md);
[widget attack audit](test_widget_attacks.md);
[widget conformance runner](test_widget_conformance.md).
