# test_widget_ops

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API, with a focused self-check.
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

Callers: the attack-corpus audit of
`2026/10/24/audit-the-widget-attack-corpus-through-a-validated-ops-tap`, and
next the conformance runner of
`2026/10/24/run-the-widget-conformance-sets-through-the-chat-list-and-report`,
which reads its results from the same ops.

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

The self-check is run from one stage and turned into checks by its caller:

```cpp
runner->add({
	.name = u"widget ops self-check"_q,
	.run = [] {
		for (const auto &row : Test::RunWidgetOpsSelfCheck().rows) {
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
scanner reads. TwidgetCore marks every event it forwards `trusted`.

A reading is final only when `factoriesReleased == factoriesSelected` and
`wrappersDestroyed == wrappersBuilt`: then no factory selected for the message
can still build a renderer, and no tap renderer can still receive a batch.
`wrappersBuilt == wrappersDestroyed == 0` alone is not that: a Build worker may
not have called its factory yet. Readings before that point are diagnostic.

## Threads and lifetime

- `TapWidgetRenderer()`, `ArmWidgetOpsTap()` and `DisarmWidgetOpsTap()` run on
  the main thread, as `Runtime::construct()` does; the armed map, keyed by
  `GlobalMsgId`, is main-only.
- The wrapping factory runs on the host's Build worker and calls the product
  factory there, once, with the same resources, theme and width. It captures
  the product factory and the tap, nothing else: no `WidgetHost` pointer and
  no weak pointer.
- The tap renderer forwards `load()`, `send()`, `setTheme()`, `setWidth()` and
  `destroy()` directly on the host thread, in order, with no queue; `send()`
  scans first and then forwards the very same string. Renderer callbacks are
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
Executed checks: the first executing campaign of this self-check and of the
corpus run is recorded by the evidence of
`2026/10/24/audit-the-widget-attack-corpus-through-a-validated-ops-tap`; until
it is, everything above is a proposed check.

## Related guidance

[Scenario composition](../scenarios.md);
[scenario teardown](../teardown.md);
[chat-list show before input](test_chat_show.md);
[input helpers](test_widgets.md#input-helpers);
[launch fuse](test_launch_fuse.md).
