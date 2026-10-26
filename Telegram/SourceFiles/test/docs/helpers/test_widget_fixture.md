# test_widget_fixture

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Internal dependency.
Source contract: [test_widget_fixture.h](../../test_widget_fixture.h).

## Use when

Maintaining the live-chat fixture that the widget stage packs share: a Test
chat shown in its own `HistoryInner` of the legacy `HistoryWidget`, local
outgoing widget messages with tapped validated ops, card readings, the
runtime log, loopback network controls and one idempotent release.

Callers: `Test::AppendWidgetAttackAudit` in `test_widget_attacks.cpp`
([test_widget_attacks](test_widget_attacks.md)), and
`Test::AppendWidgetConformance` in `test_widget_conformance.cpp`, with its
action stages in `test_widget_conformance_actions.cpp`
([test_widget_conformance](test_widget_conformance.md)). The scoped evidence
overlay of
`2026/10/26/move-widgets-to-widget-sandbox-v0-2-2-and-give-them-their-state-and-message-context`
also drives it directly, to inject local widget messages with a message
kind's media fields and read their hosts. Other overlays use the two packs,
not this module. An absence of direct overlay includes does not mean it is
unused.

It is not a general fixture framework: it exposes only the operations its
callers make. Selection, verdicts, stage assembly and rows stay in each
pack, and so do pack-specific readings such as the attack audit's death-line
parse of the log.

## Preconditions and limits

- **Session and chat.** `WidgetFixtureArgs::chat` is a chat of the Test
  session the primary window shows, loaded, with its folder known and no
  unread messages. The caller appends the fixture after its overlay's
  session and chats-loaded waits.
- **Text.** Every stage name, Check and Note of the fixture starts with
  `prefix`; its two rows are `<marker>_ENVIRONMENT` (pid, wall time, token,
  ports, the proxy without credentials and its conflicts) and
  `<marker>_NET_CONTROL` (one per probe). The attack audit passes
  `widget attack audit: ` and `ATTACK`, the conformance runner
  `widget conformance: ` and `CONFORMANCE`.
- **Gates.** `AppendWidgetFixtureOpen` appends "capture the baseline and show
  the window normally", "keep the window exposed" and "open the chat in the
  legacy HistoryWidget": the window's session is on the Test environment,
  `Tgw::Enabled()`, no layer is shown, the window is not hidden, no proxy the
  account can use points at a loopback control port, the window is shown
  normally and exposed ([test_window_exposure](test_window_exposure.md)),
  nothing activates it, and the chat opens in the background in its own
  `HistoryInner`, loaded at its bottom with nothing pending. A failed gate is
  one `fixture gate:` FAIL; `WidgetFixtureGate()` then names it, the later
  fixture stages skip, and each caller skips its message stages with it.
- **Messages.** A record is registered with `AddWidgetFixtureMessage()`
  before anything is injected for it; an unregistered record is refused.
  `InjectWidgetMessage()` assigns the record's `FullMsgId`, its
  session-qualified `GlobalMsgId` and the armed tap
  ([test_widget_ops](test_widget_ops.md)) before
  `History::addNewLocalMessage()` can notify observers, so
  `WidgetFixtureOwns()` already answers for the message inside the
  history's new-item notification. The widget's width and height anchors
  are parameters (the attack audit passes 0 and 0, auto; the conformance
  runner 390 and 0). The media's other layer-233 fields are an optional
  `WidgetMessageFields`: `Tgw::WidgetFlags` (`isCopy`, `canQuery`,
  `backgroundQuery`), `Tgw::WidgetState` (raw `chatData` and `publicData`
  bytes) and `widgetId`. The default is no flag, state or id, which is what
  the two packs pass. They are sent as given, never validated or derived:
  each true flag and each present field sets its TL flag, so a present
  empty `chatData` or `publicData` and a present zero `widgetId` reach the
  media as present, distinct from absent. The record keeps them in
  `WidgetFixtureMessage::fields`. The message is a local outgoing client-id
  message, never sent, and a history entry, so it cannot stand for a Recent
  Actions, scheduled or quick-reply entry; its Web Storage never reaches the
  disk. `InjectTextMessage()` adds a local outgoing client-id text message
  of the same shape, without a tap; it is laid out with the history, so the
  next widget message's `WidgetMessageLaidOut()` covers it. The conformance
  runner injects its fillers above a widget that scrolls off screen this
  way.
- **Card.** `ReadWidgetCard()` only reads: the `HtmlWidget` card rectangle,
  the runtime state, whether the interactive core is the current core,
  attached and accepting input, and whether its cached frame was cut for the
  current card size (`WidgetCardReady()`). `RevalidateWidgetCard()` reads
  the accepted show, list, view, core and geometry again right before a
  press; its first refusal names the show by the caller's `purpose`
  ("the click show refused: ...").
- **Bounds.** Every wait ends inside its stage's until with a named reading:
  4 s window, 20 s chat, 3 s per control phase and 6 s settle inside the
  fixture's stages; `kWidgetLayoutBound` (8 s), `kWidgetLogBound` (3 s) and
  `kWidgetBarrierBound` (10 s) for the stages a caller builds on
  `WidgetMessageLaidOut()`, `RuntimeLogDecided()` and
  `FixtureBarrierDecided()`, each with `kDefaultStageTimeout` more.
- **Network controls.** Per phase and port, one `QTcpSocket` connection and
  one `QUdpSocket` datagram to 127.0.0.1 with `QNetworkProxy::NoProxy`, each
  carrying `{"phase","pid","port","protocol","token"}` (compact JSON, then
  `\n`). An empty token, no port or a port out of range makes the control
  stages N/A. A written control is not a received one: only an external
  listener's record of it is the positive control.

## Readings and refusals

| Operation | Reading or refusal |
| --- | --- |
| `InjectWidgetMessage` | Empty, `not injected: ...` (not registered, history gone or not at its bottom, list not shown, payload does not unpack) with nothing added, or `the injected message got id ..., not the armed ...`. |
| `CheckWidgetMessage` | Fills `shape`; empty, or `no HtmlWidget view: ...` (not a local outgoing client-id message, not a `Tgw::WidgetMedia` of the bytes, `its media's flags, state or widget id are not the injected ones (the media has ...)` comparing `Tgw::WidgetMedia::fields()` with the record, absent and present-empty bytes and an absent and a zero id told apart, no laid-out card of the list's own). `shape` includes the record's `flags=<copy\|query\|background or none> chat=<bytes or none> public=<bytes or none> widgetId=<id or none>`: sizes, never the state's contents (`chat=0` is present and empty). |
| `RuntimeLogDecided` / `FinishRuntimeLog` | `log.source`: `requestLog`, `logText` (the runtime kept no callback), `logText-at-bound`, `unavailable`; `log.chars`, `log.pending`. |
| `log.summary` | The raw `Tgw::Runtime::_summary` (the `Summarize()` text) with `log.summaryState`. |
| `RevalidateWidgetCard` | Empty, or what changed: show refused, list gone, no hover, view gone or another, card not ready, core, card or view geometry. |
| `FixtureBarrierDecided` | True once no tap, the tap finished (`WidgetTapFinished()`: every factory released, every wrapper destroyed), or the bound passed; only a reading after a finished tap is final. |

`log.summaryState` is explicit about provenance; the fixture never parses the
summary:

- `Fresh`: read in the `requestLog()` callback, right after
  `Runtime::applySample()` stored that sample of the requested host;
- `Final`: `requestLog()` kept no callback because the last host's final
  sample was already applied (`Runtime::_summaryLast`);
- `Stale`: read without a fresh answer, at the bound or from a held sample
  that was not final;
- `Absent`: no summary, or no runtime.

Only `Fresh` and `Final` describe the host the request was made for; a
consumer that reads anything from the summary refuses the other two by name,
as the conformance runner's
[bridge reader](test_widget_conformance.md#bridge-calls-and-uncaught-errors)
does.
The summary and the log are bounded by the product (`kMaxLogLine`,
`kLogBytes`, the tail limits of `Summarize()`), so neither proves an absence.

## Lifetime and cleanup

The caller registers its `Runner::onFinish` before any stage that changes
state and calls `ReleaseWidgetFixture()` there; `AppendWidgetFixtureClose()`
appends "teardown" (the same release, then its Checks) and "settle after the
teardown". The release is idempotent: it disarms every registered tap by its
kept key, destroys every registered message by id (its text counts them as
`files`), closes the control sockets, leaves the list, and restores the
`use-new-chat-view` value, the baseline chat, the stacking hint and the
window state it changed, nothing else. A layer shown over the chat is hidden
at once by `HideFixtureLayers()` and recorded, never confirmed. The scroll
position is not restored. The log callback holds its record weakly and is
never called after the record is gone.

## Verification

Executed: the macOS arm64 Debug build of the moved module and its attack
caller, and a comparison of every moved body with the attack pack before
the move, after its renames (the conformance task's evidence records the
command and its output). The attack audit's own self-check
stage, its three verdict controls and three accepted regression files
(`activation-animation-events.html`, `api-fetch.html`,
`fallback-at-boot.html`) are planned for the conformance task's packed
campaign, which records its results in that task's `work/test.md`; until it
runs, the fixture after the move has no executed app evidence. The summary
reading and the text injection are the conformance runner's
([test_widget_conformance](test_widget_conformance.md)), and they too run
in the app first in that campaign. The media-field parameter
(`WidgetMessageFields`) ran in the macOS arm64 Debug client in the evidence
campaign of the task named above (its `work/test.md`, Runs 1-3): the
attack audit's 105 default calls each read back `flags=none chat=none
public=none widgetId=none`, and 16 injections with given fields in a
private chat, a basic group, a supergroup and a channel kept every flag,
state byte and presence and id as given, a present empty `chatData`, an
absent field and a present zero `widgetId` included, with
`CheckWidgetMessage()` passing for each. Windows has not
built it; the code is portable.

## Related guidance

[Widget attack audit](test_widget_attacks.md);
[widget conformance runner](test_widget_conformance.md);
[validated-ops tap](test_widget_ops.md);
[chat-list show before input](test_chat_show.md);
[window exposure](test_window_exposure.md);
[scenario teardown](../teardown.md);
[chat-list resolution](../chat-lists.md).
