# test_fixture_fence

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API (with its own self-test).
Source contract: [test_fixture_fence.h](../../test_fixture_fence.h).

## Use when

An `inject` overlay places local objects in a real chat of the shared Test
account - local messages, local topic roots, rows of a local sublist, locally
built users, channels or chats - and product code may then name their
invented ids in a request: the reactions poll of a painted message, read
marking, views. Such a request would carry a fixture id to the server. The
fence keeps it off the network: the overlay registers each fixture message id
and synthetic peer id before publishing the fixture and pastes one hook into
`MTP::details::Session::sendPrepared`; under `-testagent` every request that
names a registered id is refused before the send map and recorded with its
method, request id and the ids it named.

It refuses; it never answers. To end a refused request, or to answer any
other request with a controlled reply, use
[test_rpc_fixture](test_rpc_fixture.md). Wallet provider calls have their own
intercept, [test_wallet_intercept](test_wallet_intercept.md), and
notify-setting writes their own observer,
[test_notify_override](test_notify_override.md); all three hooks coexist in
one paste (below). A server fixture an overlay really mutates belongs to
[test_fixture_transaction](test_fixture_transaction.md). An overlay that
registers nothing is unaffected: the pasted hook only counts its requests.

## Preconditions and limits

- Debug build, launched with `-testagent` (`Test::Active()`), the hook pasted,
  main thread only. `FenceFixtureRequest` asserts the main thread; nothing
  locks.
- Register BEFORE the fixture is published (before `History::addNewMessage`,
  before the local peer is created), so the first product request that can
  name it is already fenced. Registrations are idempotent and last until the
  process exits.
- Two kinds. `registerMessage(FullMsgId)`: the message id must be a positive
  TL `int` (at most `INT_MAX`); `Expects` enforces it. `registerPeer(PeerId)`:
  the bare id (`PeerId::value & PeerId::kChatTypeMask`, what the wire
  carries) must be nonzero. Other invented 64-bit ids (fixture documents,
  synthetic calls) and ids that only appear inside strings are not a
  registered kind.
- Matching is typed scalar matching, not field classification. Every request
  body is decoded by the generated TL dumper (the rendering the MTP log prints
  for every `Send:`); a registered message id matches any `<n> [INT]` value,
  a registered bare peer id any `<n> [LONG]` value, outside quoted strings.
  Strings and bytes, int128/int256/double, vector counts and constructor
  names never match: a real `user_id` LONG equal to a fixture message id
  passes, and so do two INT values whose adjacent words spell a fixture peer
  id. But flags, dates, limits, hashes and every other same-width scalar
  render with the same tags, so one exactly equal to a registered value is
  refused too. That refusal is conservative (nothing leaks), but it changes
  product behavior in the run, so choose reserved values: message ids above
  every id the account's chats can serve and below `INT_MAX` (earlier
  campaigns used `2000000100..`, `2050000000..`, `2070000000..`), bare peer
  ids from a reserved range such as `0xFD << 32` plus a counter
  (`Data::FakePeerIdForJustName()` already uses `0xFE << 32`), and values no
  ordinary scalar of the run can take.
- Not peer-scoped. A registered message id matches in every request,
  whatever peer it names (the `FullMsgId` peer only labels the registration
  row), and a bare peer id matches whatever peer kind a request names.
- Cost. With nothing registered the hook only counts. With registrations it
  scans the body words in place (upload bodies included); only a word equal
  to a registered message id or an adjacent low/high word pair equal to a
  registered bare peer id makes it allocate the dumper's 1 MiB buffer and
  decode. The dump alone decides a refusal.
- A body the dumper cannot decode completely after such a hit raises
  `Unexpected` before anything is sent: requests reaching `sendPrepared` are
  complete generated serializations, so this is a harness or schema defect to
  investigate, never a request to let through or a partial match.
- It inspects only what reaches the hook. A request an earlier hook in the
  paste consumed (the notify observer's suppression, the wallet intercept)
  is never inspected or counted here. `SessionPrivate`'s own resends
  re-queue only requests already sent, which a refused one never is; every
  `MTP::Instance::Private` resend reaches `sendPrepared` again and is
  inspected and recorded again. The body is the bare method: the
  `invokeWithLayer`, `initConnection` and container wrappers are added later,
  at transmission, so the recorded method is the request itself (or a
  wrapper the product built, such as `invokeWithTakeout`).
- It does not prove that no fixture id ever left the process: a request
  formed before registration, or by a process without the paste, is not
  fenced. The transmission audit is the post-exit MTP log (`-testagent`
  implies debug logging; `<working dir>DebugLogs/mtp_HH_MM.txt`, selected by
  quarter-hour and first-line day identity, never by file time - see
  `Test::SelectThisLaunchLogs()` in
  [test_secrecy_scan](test_secrecy_scan.md)).

## Minimal use

The overlay's hunk in `mtproto/session.cpp`, with all three request hooks
(omit the ones the overlay does not use, with their includes). The notify
observer stays first so it records every formed request; the fence stays
last, directly above the existing `DEBUG_LOG` and send-map insertion, so it
inspects exactly the requests that would otherwise enter the send map. Never
retained in `session.cpp`.

```cpp
#ifdef _DEBUG
#include "test/test_fixture_fence.h" // OVERLAY.
#include "test/test_notify_override.h" // OVERLAY, if used.
#include "test/test_wallet_intercept.h" // OVERLAY, if used.
#endif // _DEBUG

// At the start of Session::sendPrepared:
#ifdef _DEBUG
	if (Test::InterceptNotifyRequest(request)) { // OVERLAY, if used.
		return;
	}
	if (Test::InterceptWalletRequest(_instance, request)) { // OVERLAY, if used.
		return;
	}
	// The optional wallet guard, when used, stays here.
	if (Test::FenceFixtureRequest(_instance, request)) { // OVERLAY.
		return;
	}
#endif // _DEBUG
```

A scenario sketch against the real API; no Telegram translation unit
compiles it. `ScenarioChannel`, `PlaceFixtureMessage` and
`DestroyFixtureMessage` stand for the overlay's own code: the real channel's
history, the local message built and shown in it, and its removal. The
modern `HistoryView::ListWidget` reads `History::messages()`, and
`HistoryItem::addToMessagesIndex()` adds to that index only when
`History::maybeMessages()` already exists (before that it does nothing), so
the sequence that worked is: register the id, show the real chat and wait
until its list is loaded at the bottom with `maybeMessages()` non-null, then
create the message with `History::addNewMessage(...,
NewMessageType::Existing)`, call `addNewerSlice` only when the legacy blocks
are loaded at their bottom, call `addToMessagesIndex()`, and check that the
shown list owns a view of it.

```cpp
constexpr auto kFixtureMsgId = MsgId(2071000001); // reserved, unused

void SetupScenario(not_null<Runner*> runner) {
	struct State {
		FullMsgId fixture;
		int refusalMark = 0;
		std::vector<Test::FixtureFenceRefusal> refused;
	};
	const auto state = std::make_shared<State>();
	// ... session gate, chats, ScenarioChannel() bound ...
	runner->add({
		.name = u"fixture: register, then place"_q,
		.run = [=] {
			const auto history = ScenarioChannel();
			state->fixture = FullMsgId(history->peer->id, kFixtureMsgId);
			auto &fence = Test::ProcessFixtureFence();
			fence.registerMessage(state->fixture); // before publishing
			state->refusalMark = fence.mark(); // right before the action
			PlaceFixtureMessage(history, kFixtureMsgId);
		},
	});
	runner->add({
		.name = u"fixture: the product's poll is refused"_q,
		.until = [=] {
			return Test::ProcessFixtureFence().refusalCount(
				state->refusalMark) > 0;
		},
		.then = [=] {
			const auto &fence = Test::ProcessFixtureFence();
			auto named = true;
			auto rows = QStringList();
			for (auto i = state->refusalMark; i != fence.mark(); ++i) {
				const auto refusal = fence.refusal(i); // a copy
				named = named && ranges::contains(
					refusal.matches,
					Test::FixtureIdMatch{
						.kind = Test::FixtureIdKind::Message,
						.value = uint64(kFixtureMsgId.bare),
					});
				rows.push_back(u"%1 request=%2"_q.arg(
					refusal.method,
					QString::number(refusal.requestId)));
				state->refused.push_back(refusal);
			}
			Test::Check(
				named,
				u"fixture: every refused request names the fixture id"_q,
				rows.join(u"; "_q));
		},
		.timeout = crl::time(60000),
	});
	runner->add({
		.name = u"fixture: destroy, then end the refused requests"_q,
		.run = [=] {
			DestroyFixtureMessage(state->fixture);
		},
		.then = [=] {
			// A later turn than the hook; one answer per (instance, id).
			auto ended = base::flat_set<std::pair<void*, mtpRequestId>>();
			for (const auto &refusal : state->refused) {
				const auto instance = refusal.instance.data();
				if (!instance
					|| !ended.emplace(instance, refusal.requestId).second) {
					continue;
				}
				const auto delivery = Test::DeliverControlledRpcError(
					instance,
					refusal.requestId,
					400,
					u"FIXTURE_ID_FENCED"_q);
				Test::Check(
					(delivery.route
						== Test::ControlledRpcDelivery::Route::Error)
						&& delivery.registeredBefore
						&& !delivery.registeredAfter,
					u"fixture: refused request ended through its fail"_q,
					u"request=%1"_q.arg(refusal.requestId));
			}
		},
	});
}
```

Wait on "any refusal since the mark" and judge the contents in `then`:
waiting for an exact expected refusal would turn a wrong refusal into a
timeout. Expect "every refusal names the fixture id", not "exactly one
refusal": a visible local message can also provoke `channels.readHistory`
(hold read marking off with `Test::KeepMainWindowNotMarkingRead`,
[test_marking_read](test_marking_read.md)), views and other requests, and a
resend is recorded again under the same request id.

A run that expects NO request to name its fixtures appends the whole-run check
after its deciding stages:

```cpp
Test::AppendFixtureFenceCheck(runner, u"fixture ids"_q);
```

## Readings and refusals

`inspect()` and `FenceFixtureRequest()` return true for a refused request.
`mark()` is the end of the refusal records; `refusalCount(from)` counts the
refusals since a mark; `refusal(index)` is a reference valid until the next
`inspect()`, so copy it before calling anything that can send a request.
`inspectedCount()` counts every request handed to the fence. Each record holds
`instance` (a `QPointer`, null once the `MTP::Instance` is destroyed),
`requestId` (the id `send()` returned), `method` (the dumper's outer
constructor name) and `matches` (every registered value the request named,
once each).

Rows go to `test_log.txt` as `NOTE:` rows, never verdicts. Ids are ordinary
decimals, so a reader correlates them with the MTP log's `[r<id>]` and
`<n> [INT]`; the fence never logs a decoded request or an access hash:

```
NOTE: fixture-fence: registered message:<n> peer=<PeerId::value>
NOTE: fixture-fence: registered peer:<bare id> peer=<PeerId::value>
NOTE: fixture-fence: refused method=<generated name> request=<request id> ids=<kind>:<value>[,<kind>:<value>...]
```

for example
`NOTE: fixture-fence: refused method=messages_getMessagesReactions request=<id> ids=message:2070000004`.
A registration row is written only for a new value.

`verdict()` is pure and covers the whole process:

| State | When | Check |
| --- | --- | --- |
| `clean` | at least one request inspected, none refused | PASS |
| `refusals` | any request refused | FAIL |
| `nothing-inspected` | no request ever reached the fence: the hook is missing (a ready session has already sent its startup requests) | FAIL, never a zero PASS and never N/A |

`AppendFixtureFenceCheck(runner, what)` appends one stage whose `then` writes

```
TEST_RESULT: PASS: <what>: the fixture fence inspected this run's requests and refused none - state=clean inspected=<n> registered=<n> refusals=0 rows=[none]
TEST_RESULT: FAIL: <what>: the fixture fence inspected this run's requests and refused none - state=refusals inspected=<n> registered=<n> refusals=<n> rows=[refused method=... ; ...]
TEST_RESULT: FAIL: <what>: the fixture fence inspected this run's requests and refused none - state=nothing-inspected inspected=0 registered=<n> refusals=0 rows=[none]
```

The counts follow `HelperNumberFormat()` (`<n>p` after
`RequestTelemetryNumbers()`); the ids in the rows stay ordinary decimals. A
refusal is never reclassified as PASS: a run whose evidence needs a refusal
reads the records, and its whole-run check is an expected FAIL it names.

### What the sender of a refused request observes

`send()` returned the request id and the request stays registered in its
`MTP::Instance`: neither `done` nor `fail` runs, `hasCallback(id)` stays true,
and `MTP::Instance::state(id)` reports `RequestSent` while connected (the
request is not in the send map), exactly like a request in flight or a
stalled one. Only the refusal record, by its request id, tells a fenced
request from a product stall.

`Data::Reactions::pollCollected()` sends one `messages.getMessagesReactions`
per peer naming every collected item of that peer, so a fixture id and real
ids of the same channel share one request, and the refusal withholds the real
ids too. Any of the collection's requests finishing (done or fail) runs its
`finalize`, which clears the shared polling items and resets
`_pollRequestId`. So with a single peer an unanswered refusal stops that
session's reactions polling until it is ended; with several peers another
peer's answer can release the shared state while the refused request stays
registered.

### Ending a refused request

- `Test::DeliverControlledRpcError(instance, requestId, 400,
  u"FIXTURE_ID_FENCED"_q)` on a later main-thread turn: the sender's `fail`
  runs (MTP logs "RPC Error") and the request is unregistered. A fail
  handler may retry, show a toast or change state, so choose the ending per
  request; for the reactions poll it runs `finalize` and polling resumes.
- `MTP::Instance::cancel(requestId)`: erases the callbacks without calling
  `fail`, so the sender never learns (the reactions poll keeps
  `_pollRequestId` set). Use it only when the sender's state no longer
  matters, at teardown.
- Process exit.

Never answer or cancel inside the hook: it runs inside `sendPrepared`, in the
middle of MTP's request bookkeeping.

## Lifetime and cleanup

`ProcessFixtureFence()` is leaked like `ControlledWalletIntercept()`:
registrations and records last until the process exits, there is no
unregister, and the module registers no `Runner::onFinish` work. Fixture ids
are invented, so keeping them registered after the fixture is gone refuses
nothing real. The overlay owns its fixtures (destroy them) and the refused
requests (end them, or leave them to process exit). The fence sends nothing,
writes no file and changes no account state.

## Verification

`Test::AppendFixtureFenceSelfTest(runner)`, called from a disposable
`test_scenario.cpp`, measures private fences with private probe names over
real `SerializedRequest::Serialize` bytes and synthetic request ids that are
never handed to an `MTP::Instance`; it never touches `ProcessFixtureFence()`.
Values: message `2070000004`, peer `(0xFD << 32) | 1`; the unrelated channel
and hash constants have no word or adjacent word pair equal to either.

- S1, empty fence: a poll naming the message id and a getChannels naming the
  peer id both pass, both are counted, no row is written.
- S2, both registered: a poll `{15, M, 16}` is refused with method
  `messages_getMessagesReactions`, its request id, `message:M` alone and
  exactly one row; the neighbour poll `{15, M + 1}` passes; a
  `messages.search` whose `inputPeerChannel` LONG equals `M` and whose query
  is `"M [INT]"` passes although a raw body word holds `M`.
- S3, same fence: a getChannels naming `inputChannel` `P` is refused with
  `channels_getChannels`, its request id, `peer:P` alone and one row; the
  neighbour `P + 1` passes; a poll `{1, 253}` passes although its adjacent
  raw words equal `P`.
- S4, three more fences: one inspected request and no refusal reads `clean`,
  one refusal reads `refusals`, a registered fence that inspected nothing
  reads `nothing-inspected`; each prints its observation.

Every Check prints the decision, the matches and the rows since its mark on
both verdicts; it needs no network, session or account data, emits no
deliberate FAIL and leaves nothing to tear down. It catches a blanket refusal
(S1 and the controls), raw-word matching (the S2 search and the S3 halves), a
missing decision or row (the S2 and S3 positives) and a verdict that reads
clean without inspection (S4).

It does not prove the paste or the live path. The consumer
campaign: a launch without the paste runs the self-test and appends the
public check after registering one unused value, which must FAIL exactly
`state=nothing-inspected inspected=0`; a launch with the combined paste
sends an unregistered read-only `messages.getMessagesReactions` control
(answered, not refused, check `clean`), shows a registered local message in a
real Test channel, lets the product poll it, reads one refusal naming only
that id, ends it with `DeliverControlledRpcError`, and audits the post-exit
MTP log for the absence of the id in every outgoing `Send:`.

Executed in Run 1 of `2026/10/29/keep-local-fixture-ids-off-the-network-in-test-overlays`
(`work/test.md`): the launch without the paste read the one expected
`state=nothing-inspected inspected=0` FAIL; the launch with the paste
answered the control through `done`, read `clean` before the fixture, and
refused the product's `messages.getMessagesReactions` naming only
`message:2071000001` with its handler still registered, ended it with
`DeliverControlledRpcError` (route `Error`), and its post-exit MTP log held
no request naming the id while the control's `Send:` and answer were there.
Two facts it needed: `Data::Reactions::poll()` collects an item marked
unknown (`updateReactionsUnknown()`) only from about 21 s of process age
(`kPollEach`), and `Test::KeepMainWindowNotMarkingRead` minimizes the window,
so the scenario rendered the chat's `ChatWidget` over the fixture row
in-process to run the product paint that polls it.

Static non-Debug shape: the header declares everything in every
configuration and defines no inline code that calls a Debug-only definition;
the `.cpp` defines everything under `_DEBUG`, and the only callers outside
`test/` are the overlay hunks above, themselves under `_DEBUG`.

## Related guidance

[test_rpc_fixture](test_rpc_fixture.md) (ending a refused request);
[test_wallet_intercept](test_wallet_intercept.md) and
[test_notify_override](test_notify_override.md) (the other `sendPrepared`
hooks); [test_probe](test_probe.md) (marks and rows);
[test_marking_read](test_marking_read.md); [test_capture](test_capture.md)
(rendering the list that polls the fixture);
[test_secrecy_scan](test_secrecy_scan.md) (launch log selection);
[chat lists](../chat-lists.md); [scenario design](../scenarios.md);
[teardown](../teardown.md); [failure diagnosis](../diagnostics.md).
