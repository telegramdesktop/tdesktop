# test_widget_fixture

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Internal dependency.
Source contract: [test_widget_fixture.h](../../test_widget_fixture.h).

## Use when

Maintaining the live-chat fixture that the widget stage packs share: a Test
chat shown in its own `HistoryInner` of the legacy `HistoryWidget`, local
outgoing widget messages with tapped validated ops, card readings, the
runtime log, loopback network controls, the primary window's one-column
placement and one idempotent release.

Callers: `Test::AppendWidgetAttackAudit` in `test_widget_attacks.cpp`
([test_widget_attacks](test_widget_attacks.md)),
`Test::AppendWidgetConformance` in `test_widget_conformance.cpp`, with its
action stages in `test_widget_conformance_actions.cpp`
([test_widget_conformance](test_widget_conformance.md)),
`Test::AppendWidgetStress` in `test_widget_stress_live.cpp`
([test_widget_stress](test_widget_stress.md)), and
`Test::AppendChatListPlaceSelfTest` in `test_chat_show_place.cpp`
([test_chat_show_place](test_chat_show_place.md)), which injects two local
widget cards and two text fillers and places them. The scoped evidence
overlay of
`2026/10/26/move-widgets-to-widget-sandbox-v0-2-2-and-give-them-their-state-and-message-context`
also drives it directly, to inject local widget messages with a message
kind's media fields and read their hosts. Other overlays use the two packs,
not this module. An absence of direct overlay includes does not mean it is
unused. The [one-column window](#one-column-window) has two of these
callers: the conformance runner appends the fixture's own sizing stages,
and the stress-kit runner builds its placement stages on the same readings,
move and settle.

It is not a general fixture framework: it exposes only the operations its
callers make. Selection, verdicts, stage assembly and rows stay in each
pack, and so do pack-specific readings such as the attack audit's death-line
parse of the log and pack-specific window policies such as the stress kit's
screen choice and stay-on-top hint.

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
  `widget conformance: ` and `CONFORMANCE`, the chat-list place self-test
  `chat-list place self-test: ` and `CHAT_LIST_PLACE_SELFTEST`.
- **Gates.** `AppendWidgetFixtureOpen` appends "capture the baseline and show
  the window normally", "keep the window exposed" and "open the chat in the
  legacy HistoryWidget": the window's session is on the Test environment,
  no layer is shown, the window is not hidden, no proxy the
  account can use points at a loopback control port, the window is shown
  normally and exposed ([test_window_exposure](test_window_exposure.md)),
  nothing activates it, and the chat opens in the background in its own
  `HistoryInner`, loaded at its bottom with nothing pending. A failed gate is
  one `fixture gate:` FAIL; `WidgetFixtureGate()` then names it, the later
  fixture stages skip, and each caller skips its message stages with it.
  `AppendWidgetFixtureOneColumn()` adds two more gates, only for a caller
  that appends it ([one-column window](#one-column-window)); the open
  stages never size the window.
- **Messages.** A record is registered with `AddWidgetFixtureMessage()`
  before anything is injected for it; an unregistered record is refused.
  `InjectWidgetMessage()` assigns the record's `FullMsgId`, its
  session-qualified `GlobalMsgId` and the armed tap
  ([test_widget_ops](test_widget_ops.md)) before
  `History::addNewLocalMessage()` can notify observers, so
  `WidgetFixtureOwns()` already answers for the message inside the
  history's new-item notification. The widget's width and height anchors
  are parameters (the attack audit passes 0 and 0, auto; the conformance
  runner 390 and 0; the chat-list place self-test 0 and 64). The media's
  other layer-233 fields are an optional `WidgetMessageFields`: `Tgw::WidgetFlags` (`isCopy`, `canQuery`,
  `backgroundQuery`), `Tgw::WidgetState` (raw `chatData` and `publicData`
  bytes) and `widgetId`. The default is no flag, state, id or via bot, which
  is what the two packs pass. They are sent as given, never validated or
  derived: each true flag and each present field sets its TL flag, so a present
  empty `chatData` or `publicData` and a present zero `widgetId` reach the
  media as present, distinct from absent. `WidgetMessageFields::viaBot`
  (default 0, none) is not a media field: a nonzero `UserId` becomes the
  local message's `HistoryItemCommonFields::viaBotId`, so the message comes
  via that bot (`HistoryMessageVia`, `HistoryItem::viaBot()`) while it stays
  a local outgoing client-id message from the account itself. The caller
  resolves and loads the bot first; the fixture never resolves it and checks
  only the id. The record keeps them in
  `WidgetFixtureMessage::fields`. The record's `caption`, set before the
  injection, is the widget message's text, which the history shows as the
  caption below the widget's media; empty, the default, is no text. It
  changes only the message text, never the media fields or the flags, and
  the bubble grows below the widget's card. The conformance runner sets
  each run's manifest id there but for an entry with `safeArea`, whose
  message stays without one
  ([message contexts](test_widget_conformance.md#message-contexts)); the
  attack audit and the chat-list place self-test set none. A caller that
  sets the record's `watchHost` (a `WidgetHostOverride`; an empty one only
  watches) before the injection also gets `WidgetFixtureMessage::host`, the
  message's host watch ([test_widget_host](test_widget_host.md)), armed
  with the tap for the same key and disarmed with it. The conformance
  runner sets an empty one for every run, and the stress-kit runner
  ([test_widget_stress](test_widget_stress.md)) its pass's override; the
  attack audit and the chat-list place self-test set none and arm no watch.
  The message is a local outgoing client-id
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
- **Desktop stop.** `WidgetDesktopStop(fixture, id, core, fatalFrozen)`
  only reads a stop the desktop decided itself, such as a paint bound's
  refusal of the widget's frames in `RendererCore::frame()`. Such a stop
  reaches no host death: the host watch's `dead` stays null, or reads
  `terminated` once the retired host is destroyed
  ([test_widget_host](test_widget_host.md)). The identity is the caller's:
  `core` is `WidgetHostSample::core` of the run's one watched attachment,
  compared by address with the list's own runtime's `Runtime::core()`,
  which is the retired core itself (the still) after the stop; never
  `interactive()`, null once stopped. `fatalFrozen` is the run's tap count
  ([test_widget_ops](test_widget_ops.md)): a host's counted-cap stop sends
  a fatal `frozen` and carries its reason in the host's own death. The
  reading changes nothing, keeps nothing and never takes another view's
  runtime. Its consumers are the stress-kit runner, which latches its first
  answer with that reading's time, and the conformance runner, which reads
  it once in its log stage.
- **Bounds.** Every wait ends inside its stage's until with a named reading:
  4 s window, 20 s chat, 3 s per control phase, 6 s per one-column settle
  and 6 s for the settle after the teardown inside the fixture's stages;
  `kWidgetLayoutBound` (8 s), `kWidgetLogBound` (3 s), `kWidgetBarrierBound`
  (10 s) and `kWidgetPlaceBound` (6 s) for the stages a caller builds on
  `WidgetMessageLaidOut()`, `RuntimeLogDecided()`, `FixtureBarrierDecided()`
  and `FixtureWindowSettled()`, each with `kDefaultStageTimeout` more.
- **Network controls.** Per phase and port, one `QTcpSocket` connection and
  one `QUdpSocket` datagram to 127.0.0.1 with `QNetworkProxy::NoProxy`, each
  carrying `{"phase","pid","port","protocol","token"}` (compact JSON, then
  `\n`). An empty token, no port or a port out of range makes the control
  stages N/A. A written control is not a received one: only an external
  listener's record of it is the positive control.

## One-column window

The primary window's placement for the app's one-column layout, with only
the fixture's chat shown. The reading, target, move and settle are shared;
the screen, the window-state handling, the stacking hint and the gates
stay with each caller (their policies close this section).

- **Reading.** `ReadFixtureColumns(fixture, window)` only reads the passed
  primary window and the fixture's history: the column layout its session
  controller has (`one-column`, `normal` or `three-column`, from
  `adaptive()`), `threshold` = `st::columnMinimalWidthLeft +
  st::columnMinimalWidthMain` (640 logical px at 100 %), `body` =
  `computeColumnLayout().bodyWidth` (`bodyWidget()->width() -
  filtersWidth()`), `bodyWidget`, `filters` (`filtersWidth()`), `content`
  (`MainWidget`'s width), `chat` and `chatShown` (the visible legacy
  `HistoryWidget` of the fixture's history), `dialogsShown` (the visible
  `Dialogs::Widget`s in `MainWidget`) and `minimumWidth`.
  `OnlyFixtureChatShown()` is one column, the body below the threshold, no
  chat list shown and the chat shown as wide as `MainWidget`: only the
  chat, no chat list and no third column.
- **Target.** `SessionController::computeColumnLayout()` lays a body
  narrower than the threshold out in one column, so the target asks for a
  `bodyWidget()` of `threshold - 1` plus the filters sidebar's width when
  it is shown (711 logical px at 100 % with the 72 px sidebar, 639 without
  it), plus the window's own width beside its `bodyWidget()`, never below
  the window's minimum width. Its frame starts `kWidgetWindowMargin` (64
  logical px) inside the given screen's available area and is at most 1000
  logical px tall, never taller than that inner area; the client geometry
  is that frame without the window's current frame margins. The margin and
  the height are harness constants, not product styles; the width comes
  from the column styles at runtime.
- **Move.** `MoveFixtureWindow(fixture, window, screen)` reads the columns,
  computes the target on `screen` from the window's geometry now and calls
  only `QWidget::setGeometry(target.client)`, when the target has no error
  and differs from that geometry. It returns a `FixtureWindowMove`: the
  columns read before the move, the target and whether it moved, so a
  caller records values from before the move. It changes no window state,
  flag, stacking order or display mode and requests no activation.
- **Settle.** `FixtureWindowSettled(fixture, wait, window, screen)` is an
  until on a `WidgetFixtureWait` started before the move. Each call samples
  `FixtureWindowText()` (`screen=[<name>] frame=<rect> dpr=<ratio>
  state=<flags> minimized=<0|1>`) and `FixtureColumnsText()`, restarts the
  stable count whenever that sample changes, and ends once
  `FixtureWindowPlaced()` holds (on `screen`, not minimized, maximized or
  full screen, the frame wholly inside the screen's available area) with
  the sample unchanged for 1 s, or at `kWidgetPlaceBound` (6 s) from the
  wait's start. `wait.sample` keeps the last sample, which each caller's
  stage gives as its timeout details. The bound only ends the wait: a
  settle that ran out is readiness to judge, never a placement pass.
- **Two moves.** Both callers move twice: the first move and its settle,
  then the same move again from the settled window with its own settle.
  The settled window's frame margins and scale can differ on another
  screen or out of another state, and the filters sidebar, shown only while
  `SessionController::enoughSpaceForFilters()` holds (the window at least
  its minimum width plus `st::windowFiltersWidth` wide), can hide in the
  narrower window, so the second move can ask for another width than the
  first one read.
- **Stages.** `AppendWidgetFixtureOneColumn(runner, fixture)`, appended
  after `AppendWidgetFixtureOpen()`, adds "move the window for the
  one-column layout" and "size the window for the one-column layout", both
  skipped by the fixture gate and bounded by `kWidgetPlaceBound` plus
  `kDefaultStageTimeout`, the last sample their timeout details. The first
  resolves the top-level window of the fixture's list and the screen its
  `QWindow` is on now, reads the window, its columns and
  `isActiveWindow()`, leaves a maximized or full-screen state (below) and
  makes the first move; its until ends on a refusal, the window or the
  screen gone, or the settle on the fixture's own `wait`. Its gate, `the
  window is moved for the one-column layout on its own screen`, fails on
  `no window shows the chat's list`, `the window is on no screen`, `the
  one-column target is refused: <error>` (the window keeps its geometry) or
  `the window or the screen it started on is gone`. The second restarts
  the wait, makes the second move and settles; its gate, `only the chat is
  shown, in the one-column layout`, fails on the same refusals or when
  `OnlyFixtureChatShown()` of the columns read then is false. Both gates'
  details carry `before`, the `target` and `corrected` moves (frame,
  client, bodyWidget, moved, error and the columns each read), `after`, the
  window's captured, expected and current state with its `normalized`
  mark, `shownNormal` and `active` before and after. A failed gate is
  `WidgetFixtureGate()`, so the caller's later stages skip by its name.
- **Window state.** A maximized or full-screen window cannot take the
  target geometry, so the first stage clears those bits from its state
  without `Qt::WindowActive` (the fixture's `WithoutActive()`:
  `QWidget::setWindowState()` answers an added `Qt::WindowActive` with
  `activateWindow()`), marks `normalized` and Notes it. The window stays
  normal: neither the teardown nor the caller's `Runner::onFinish` backstop
  maximizes it again, since Qt on Windows does that with
  `ShowWindow(SW_MAXIMIZE)`, which activates the window whatever
  `Qt::WindowActive` says, and the activated window could mark the
  restored baseline chat read. "Settle after the teardown" then requires
  the normal state; the captured one stays in its details. `shownNormal`,
  the opening stage's separate mark for a minimized window's
  `showNormal()`, is still undone by the release. A normal window gets only
  `setGeometry()`. Nothing in the stages sets `Qt::WindowActive`, raises
  the window, calls `activateWindow()` or `requestActivate()` or changes
  its flags: they request no activation and leave the window's own focus
  child as it was.
- **Lasting effect.** No geometry and no maximized or full-screen state is
  restored. The window keeps the one-column geometry and its new position
  on its screen, which Telegram saves, and a normalized one stays normal.
- **Callers' policies.** The conformance runner appends the stages above:
  the window's own screen, never another, so the device pixel ratio its 2x
  captures need stays that screen's, and only the two fixture gates; no
  stay-on-top hint, covering-window check, focus clearing or cursor move
  ([test_widget_conformance](test_widget_conformance.md#preconditions-and-limits)).
  The stress-kit runner calls `MoveFixtureWindow()`,
  `FixtureWindowSettled()`, `FixtureWindowPlaced()`, `ReadFixtureColumns()`,
  `OnlyFixtureChatShown()` and the two texts from its own stages
  ([test_widget_stress](test_widget_stress.md#preconditions-and-limits)): it
  picks the screen with the highest refresh rate, sets the stay-on-top hint
  before the first move, leaves a maximized or full-screen state itself,
  without the `normalized` mark, and keeps the window normal, records each
  move's readings from before it in its run file's `placement`, gates the
  frame on that screen, the layout and the hint, then clears the focus and
  moves the cursor off the window. The attack audit and the chat-list place
  self-test size nothing.

## Readings and refusals

| Operation | Reading or refusal |
| --- | --- |
| `InjectWidgetMessage` | Empty, `not injected: ...` (not registered, history gone or not at its bottom, list not shown, payload does not unpack) with nothing added, or `the injected message got id ..., not the armed ...`. |
| `CheckWidgetMessage` | Fills `shape`; empty, or `no HtmlWidget view: ...` (not a local outgoing client-id message, not a `Tgw::WidgetMedia` of the bytes, `its media's flags, state or widget id are not the injected ones (the media has ...)` comparing `Tgw::WidgetMedia::fields()` with the record, absent and present-empty bytes and an absent and a zero id told apart, `it comes via bot <id or none>, not the injected <id or none>` comparing `HistoryItem::viaBot()` with `fields.viaBot`, no laid-out card of the list's own). `shape` includes the record's `flags=<copy\|query\|background or none> chat=<bytes or none> public=<bytes or none> widgetId=<id or none>`: sizes, never the state's contents (`chat=0` is present and empty). |
| `RuntimeLogDecided` / `FinishRuntimeLog` | `log.source`: `requestLog`, `logText` (the runtime kept no callback), `logText-at-bound`, `unavailable`; `log.chars`, `log.pending`. |
| `log.summary` | The raw `Tgw::Runtime::_summary` (the `Summarize()` text) with `log.summaryState`. |
| `RevalidateWidgetCard` | Empty, or what changed: show refused, list gone, no hover, view gone or another, card not ready, core, card or view geometry. |
| `WidgetDesktopStop` | `RendererCore::stopReason()` verbatim, or empty: `core` 0, no own runtime or no current core, another core, a runtime not `Stopped` (a stop off screen that `release()` already rearmed reads `Released`), a stop that is not fatal, or a fatal `frozen` counted on the tap. Empty is no desktop stop seen, never proof that the widget is alive. |
| `FixtureBarrierDecided` | True once no tap, the tap finished (`WidgetTapFinished()`: every factory released, every wrapper destroyed), or the bound passed; only a reading after a finished tap is final. True at the bound too, so a caller that needs the hosts gone (one widget alive at a time) checks `WidgetTapFinished(ReadWidgetOps(*message.tap))` after it and stops before another injection when it is false. |
| `ReadFixtureColumns` | The columns, or `error` `the primary window has no session controller, body or content` with only `threshold` set. `FixtureColumnsText()` is `layout: <error>`, or `columns=<name> body= threshold= bodyWidget= filters= content= chat= chatShown= dialogsShown= minimumWidth=` with each value. |
| `MoveFixtureWindow` | `target.error`: the columns' error, or `the one-column frame <frame> does not fit in the screen's available area <inner> inside its margin`, and nothing moved; `moved` is false as well for a window already at the target. |
| `FixtureWindowSettled` | True once placed and unchanged for 1 s, or at `kWidgetPlaceBound`; never a verdict: only the caller's gate after it judges the placement. |

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
teardown". The release is idempotent: it disarms every registered tap and
host watch by its kept key, as `RemoveFixtureMessage()` does for one record,
destroys every registered message by id (its text counts them as
`files`), closes the control sockets, leaves the list, and restores the
`use-new-chat-view` value, the baseline chat, the stacking hint and a
minimized window's state, nothing else. A maximized or full-screen state
the one-column stages left, and the geometry they gave the window, are
never restored; "settle after the teardown" requires the state the release
leaves, the normal one after such a placement
([one-column window](#one-column-window)). A layer shown over the chat is
hidden at once by `HideFixtureLayers()` and recorded, never confirmed. The
scroll position is not restored. The log callback holds its record weakly
and is never called after the record is gone.

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
`CheckWidgetMessage()` passing for each. None of this has run on Windows.
The optional host watch (`watchHost`, `host`, their arming next to the tap
and their disarming) compiled, with the rest of the module, in a Windows
Win32 Debug single-file compile for
`2026/10/30/run-the-widget-stress-kit-at-v0-4-0-on-a-release-build-without-lto`;
the stress-kit runner and the conformance runner set it. The `caption`
field and the conformance runner's watch compiled in the macOS arm64 Debug
and Release (no LTO, with the harness) builds of
`2026/10/29/report-the-desktop-conformance-and-run-the-stress-kit-at-widget-sandbox-v0-3-0`
and ran in the app in that task's Release smoke (its `work/smoke.md`): the
hosts of the two captioned runs, `auto/04-values-units` and
`auto/31-stop-text`, got all 0, the two uncaptioned `safeArea` entries,
`auto/36-safe-area` and `visual/17-time-corner`, got `bottom` 26 with
`cornerRight` 118, and each watch was armed with the tap, read and
disarmed.
The via bot
(`WidgetMessageFields::viaBot` and its `CheckWidgetMessage()` comparison)
compiled in the Windows Win32 Debug build of
`2026/10/31/let-widgets-pay-and-sign-data-through-gram-wallet-for-their-bot`,
which adds it for local via-bot widget messages; no existing caller sets
it, and it has not run in the app.
`WidgetDesktopStop()` compiled, with its two consumers, in the Windows
Win32 Debug build of
`2026/10/31/stop-a-widget-on-its-last-picture-when-the-paint-bounds-refuse-its-frames`;
it has not run in the app yet: that task's evidence campaign runs it
through both runners.
The [one-column window](#one-column-window) (its reading, target, move and
settle, moved from the stress-kit runner, and
`AppendWidgetFixtureOneColumn()`) has not run in the app yet. Before the
move the same reading, target and settle ran inside the stress-kit
runner's Windows passes ([test_widget_stress](test_widget_stress.md#verification)).
The evidence campaign of
`2026/10/31/put-the-conformance-runners-window-in-the-one-column-layout`
runs a conformance smoke from a normal window narrowed to the first
two-column width, one from an inactive maximized window and a stress-kit
smoke, and records what ran in that task's evidence.

## Related guidance

[Widget attack audit](test_widget_attacks.md);
[widget conformance runner](test_widget_conformance.md);
[widget stress kit](test_widget_stress.md);
[validated-ops tap](test_widget_ops.md);
[widget host watch](test_widget_host.md);
[chat-list show before input](test_chat_show.md);
[window exposure](test_window_exposure.md);
[scenario teardown](../teardown.md);
[chat-list resolution](../chat-lists.md).
