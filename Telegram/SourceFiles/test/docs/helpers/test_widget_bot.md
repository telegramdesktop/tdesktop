# test_widget_bot

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API, with focused self-checks.
Source contract: [test_widget_bot.h](../../test_widget_bot.h).

## Use when

A scenario needs a widget message's requests (`Telegram.Widget.request()`)
answered while its message is a local client-id fixture message, which
`Tgw::Bridge::request()` refuses as `Failed` before any RPC. The played bot
arms one message, by its session-qualified `GlobalMsgId`, with a table of
answers; each request of every host built for that message is then recorded
and answered from the table at the runtime's request seam, synchronously,
on the main thread. The request still goes through the widget's own gate in
the engine with the client's context, the host's request handler, the hop to
the main thread and the core and runtime checks; only the bridge, the
server and the bot are played.

Caller: the conformance runner of
`2026/10/26/report-the-desktop-conformance-at-widget-sandbox-v0-2-2`
([test_widget_conformance](test_widget_conformance.md)), which arms every
injected run with its manifest entry's `bot` table, empty for an entry
without one, and writes each record into the run's `bot` extra.

Use something else when:

- the real request path is the question: `messages.getBotWidgetQueryAnswer`,
  its errors, its 30 s deadline and the bot's answer need a message the
  server knows and a real bot, which this helper never reaches;
- a bot's state edits, shared-state edits or repeated edits matter: nothing
  here edits a message or polls;
- a controlled RPC reply of another request is wanted:
  [test_rpc_fixture](test_rpc_fixture.md).

## Preconditions and limits

- **Arm before the first host can start.** A request the runtime handled
  before `ArmWidgetBot()` went to the product's bridge and is in no record.
  A local message's host is built only after a paint, an asynchronous
  `Build()` and `Runtime::constructed()`, and requests reach the main thread
  through `crl::on_main`, so arming in the same call that added the message,
  with no event-loop turn between, is early enough.
- **One session.** Only the session-qualified id that was armed is played;
  another session's message with the same `FullMsgId` reaches the product.
  Keep the armed id for `DisarmWidgetBot()`; never rebuild it later.
- **Active harness.** `PlayWidgetBot()` returns false unless
  `Test::Active()` and the id is armed; it then neither calls the responder
  nor records anything.
- **Answers.** A request's exact `data` found in the table is answered with
  its `result`, where every `{n}` becomes the count of requests with that
  data for the message so far, this one included, `{from}` `Conformance`
  and `{time}` `12:00` (the texts of twidget's `tools/conformance` runner;
  `CLIENTS.md` lets them come out as any text). A table entry without a
  result, and data not in the table, are answered without result, as the
  reference `scripts/conformance/test-bot.ts` answers unknown data; the record
  says which (`listed`). No request is ever failed, timed out or rate
  limited here.
- **Wallet flag.** `PlayWidgetBot()`'s last argument is the request's
  `WidgetRequest::wallet`, true for a `request(data, { wallet: true })`
  (default `false`). It is recorded per request as given (`wallet`) and never
  changes the answer: a table entry answers a wallet request as any other,
  and nothing here plays a wallet, a transaction or a `with_wallet` query.
- **Message lifetime.** The record belongs to the message, not to one host:
  a host restarted after a death or re-created adds to the same record, and
  its counts continue. A reading never says which host made a request.
- **Main thread.** Arming, disarming, playing and reading all run on the main
  thread, where `Runtime::botRequested()` runs; the armed map and the
  records are main-only and hold no lock.

## Minimal use

Arm in the injection's own call, disarm at the removal, read the record
afterwards.

```cpp
runner->add({
	.name = u"inject the widget message and arm its bot"_q,
	.run = [=] {
		// Add the local widget message here, keeping its HistoryItem's
		// globalId() in state->id.
		state->bot = Test::ArmWidgetBot(state->id, {
			{ "conf:ping", "pong {n}" },
			{ "conf:quiet", std::nullopt },
		});
	},
});
// ... show the card and tap what makes the widget ask ...
runner->add({
	.name = u"remove the message"_q,
	.run = [=] {
		Test::DisarmWidgetBot(state->id);
		// Destroy the local message here.
		const auto requests = Test::ReadWidgetBot(*state->bot);
		Test::Check(
			(requests.size() == 1)
				&& (requests[0].data == "conf:ping")
				&& (requests[0].result == std::string("pong 1")),
			u"the tap asked once and was answered"_q,
			QString::number(int(requests.size())));
	},
});
runner->onFinish([=] {
	Test::DisarmWidgetBot(state->id);
});
```

## Readings and refusals

`ReadWidgetBot()` returns every played request in arrival order:

| Field | Meaning |
| --- | --- |
| `data` | The request's exact data. |
| `listed` | Whether the table had it. |
| `wallet` | Whether the request asked for the wallet. |
| `n` | The record's requests with this data, this one included. |
| `result` | The answer's result, templates filled, or none. |
| `ms` | Milliseconds since the record was armed. |

An empty reading means no request reached the runtime's seam while the id
was armed: the widget asked nothing, or its engine gate refused the request
(no tap, no `canQuery`, a copy without the flag) before the client saw it.
Those refusals are the framework's and reach the widget as its own
`WidgetRequestError`; this helper does not observe them. There are no
refusals of its own.

## Lifetime and cleanup

`ArmWidgetBot()` returns a shared record that the armed map and the caller
hold; arming the same id again replaces the armed record with a fresh one.
`DisarmWidgetBot()` only erases the armed lookup, so later requests go to the
product; the caller's record keeps what it played and stays readable. Nothing
keeps the responder, a runtime pointer or any other product object, and
nothing is written to the account, the disk or the server. A scenario that
arms ids disarms them on its finish path (`Runner::onFinish`).

## Release and inactive identity

The production call is the single `Test::PlayWidgetBot()` in
`Tgw::Runtime::botRequested()` (`tgw_runtime.cpp`), with the item's
`globalId()`, the request's data and responder, and its wallet flag where the
runtime passes one (otherwise the default `false`), after its own Live,
current-core and attached refusal and right before
`Tgw::Bridge::request()`; when it returns true the runtime returns. The
Release branch of `test_widget_bot.cpp` defines only `PlayWidgetBot()`, with
the same parameters, returning false; every other declaration of the header
is left undefined there. In Debug, an inactive harness and an unarmed id
return false before any record is touched, and the bridge gets the request
as before.

## Verification

`Test::RunWidgetBotSelfCheck()` drives `PlayWidgetBot()` under message ids
of peer 0, which no real message of any session has, with a recording
responder, and disarms its ids before it returns:

- an unarmed id, and another session's message with an armed `FullMsgId`,
  return false with no response and nothing recorded;
- listed requests are answered once each before `PlayWidgetBot()` returns,
  with no failure, their templates filled and `{n}` counting each exact
  data separately;
- a listed entry without result and unlisted data are answered without
  result;
- the record holds all five requests in order with their `listed`, `n` and
  `result`, each with `wallet` unset, stays readable after the disarm, and a
  request after the disarm is not played; arming again gives a fresh, empty
  record;
- a request played with `wallet` true is answered from the table as any
  other and recorded with `wallet` set, and the same data played right after
  without it is recorded unset, so the flag is the request's, not the data's
  or the record's.

The conformance runner's self-check stage takes these rows as Checks, beside
its own bot-table controls ([test_widget_conformance](test_widget_conformance.md#verification)).
They run in the real binary in the conformance campaign, which records them;
the Release identity is a static claim, not a launched one. The wallet flag
and its row compiled in the Windows Win32 Debug build of
`2026/10/31/let-widgets-pay-and-sign-data-through-gram-wallet-for-their-bot`
and have not run in the app yet.

## Related guidance

[Widget conformance runner](test_widget_conformance.md);
[validated-ops tap](test_widget_ops.md);
[shared widget fixture](test_widget_fixture.md);
[scenario teardown](../teardown.md).
