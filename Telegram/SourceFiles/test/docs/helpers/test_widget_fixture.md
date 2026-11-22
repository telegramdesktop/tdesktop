# test_widget_fixture

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Internal dependency.
Source contract: [test_widget_fixture.h](../../test_widget_fixture.h).

## Use when

Maintaining the live-chat fixture that the widget stage packs share: a Test
chat shown in its own `HistoryInner` of the legacy `HistoryWidget`, local
outgoing widget messages with tapped validated ops, card readings, the
runtime log, loopback network controls, the primary window's one-column
placement and one idempotent release. [Remote engines](#remote-engines)
says where its widgets run: in a sandboxed
widget helper, packaged builds included, which on macOS, on Linux x86_64
and aarch64 and on Windows 7 SP1 or later (the Windows Store build from
Windows 10 1809) is the separate Qt-free `TelegramWidgetHelper` (in the
Mac App Store build, from macOS 12, an App-Sandboxed XPC service), on Windows a
restricted `modules\<arch>\TelegramWidgetHelper.exe` in the sandbox tier
of its Windows version. Where no such
helper is available no widget engine runs: every widget stops with `no
sandboxed widget helper is available here: <reason>`.

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
  attached and accepting input, and whether the picture last delivered to
  it (`RendererCore::_frame` with its `_frameArea`) has the current card
  size (`frameAtCard`, which `WidgetCardReady()` needs). That is a held
  picture, not a drawn one. A core's height and picture reach it later
  than the request that made them, in the posted answer
  `RendererCore::apply()` takes, and `RendererCore::frame()` draws the
  held picture only while its size request serial (`_frameSerial`) is the
  current one (`_sizeSerial`) and its pixel ratio the screen's. So
  `frameAtCard` can be true while `frame()` returns nothing and the card
  shows no picture: after a width change back to an earlier width (A, B,
  A) or a scale-only change, whose older picture has an equal area, or a
  pixel ratio change, until the current request's answer arrives. The
  fixture checks neither condition; an overlay that needs the drawn
  picture reads the two serials through the same private-member accessor
  or records the draw.
  `RevalidateWidgetCard()` reads the accepted show, list, view, core and
  geometry again right before a press; its first refusal names the show
  by the caller's `purpose` ("the click show refused: ...").
- **Desktop stop.** `WidgetDesktopStop(fixture, id, core, fatalFrozen)`
  only reads a stop the desktop decided itself, such as a paint bound's
  refusal of the widget's frames: the Qt backend refuses a helper picture
  the core's `RendererWork` paints on the widget render thread, and that
  worker stops the core and closes its route there
  (`RendererWork::refuse()`). The stop reaches the chat later, in the
  answer the worker posts and `RendererCore::apply()` takes: only then do
  the core's `stopped()`, `stopReason()` and `stopFatal()` read it and the
  runtime retire, so the reading stays empty until that answer arrives. A
  hosted host's death goes the other way: its `onDeath` callback calls
  `HostLink::postDeath()`, which, for a death the stream did not spare,
  records the run's decision and calls `HostLink::postFrozen()`: that posts
  `RendererCore::hostFrozen()` to the main thread, which latches the
  freeze as soon as it runs, and `RendererWork::freeze()` to the widget
  render thread; that stop, too, is published only by the posted answer.
  A spared death posts only `RendererCore::hostFrozen()`, and no stop
  ([Remote engines](#remote-engines), *Deaths*).
  A desktop stop is no host death: the host watch's `dead` stays null,
  reads `terminated` once the retired host is destroyed, or holds another
  death of that host, such as `the widget engine process ended` when the
  desktop refused the traffic of a route its helper's end had spared
  ([test_widget_host](test_widget_host.md)).
  The identity is the caller's: `core` is `WidgetHostSample::core` of the
  run's one watched attachment, compared by address with the list's own
  runtime's `Runtime::core()`, which is the retired core itself (the
  still) after the stop; never `interactive()`, null once stopped. That
  address holds unless the watched core stops before its first delivered
  picture while an older still is shown: `Runtime::retire()` then
  discards the stopped core, `Runtime::core()` stays the older still,
  another address, and the reading is empty, as for any other core.
  `fatalFrozen` is the run's tap count of its hosted works' final ordinary
  or stalled charges
  ([test_widget_ops](test_widget_ops.md#hosted-deaths-and-final-charges)),
  never of a death a host reported by itself, and the reading is
  empty once it is above 0: a host's counted-cap stop carries its reason
  in the host's own death, so a stop that death made is never read as the
  desktop's. A refusal the desktop made of a hosted widget's traffic is a
  final charge of its work too, counted apart (`desktopCharges`), which
  this reading does not consult; the conformance runner takes a hosted
  widget's cause from its sampled work's own record, never from this
  reading. The reading changes nothing, keeps nothing and never takes another view's
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

## Remote engines

On macOS (in the Mac App Store build from macOS 12), on Linux x86_64 and
aarch64 and on Windows 7 SP1 or later (each Windows version in its own
sandbox tier, **The Windows sandbox tiers** below; the Windows Store build
from Windows 10 1809, in the full tier alone, **The Windows Store build**
below), packaged builds included on all three, every widget of the client
runs in one shared widget helper process that has locked itself down,
instead of the client's own threads, in Debug and Release builds alike
and with no switch. That helper is the separate Qt-free executable
`TelegramWidgetHelper`, on Windows the restricted
`TelegramWidgetHelper.exe`, which the Windows Store build carries in its
package (**The Windows Store build** below); the Mac App Store build
packages it as an App-Sandboxed XPC service (**The Mac App Store
service** below). On macOS, both packages alike, on Linux, in the
`TelegramHelpers` and the libexec layouts alike, and on Windows, the
Windows Store build too, that helper runs the whole widget: its engine,
its trusted validator and its renderer, while the client keeps the
checked Qt work and the pictures (**Hosted widgets on macOS, Linux and
Windows** below). Telegram links no widget engine, validator, renderer or
litehtml on any platform: Nova, the validator, the renderer and its
litehtml are twidget's Rust serving, the helper's one Rust library
(**The shared helper** and **The Windows helper** below). Everywhere else, and in every
build configured with `DESKTOP_APP_DISABLE_PRIVATE_QT_API`, no widget
engine runs: every widget stops with `no sandboxed widget helper is
available here: <reason>` (**Platform default** and **Store, packaged and
other platforms** below). The
packs, the fixture and the seams they use need no change for it.

- **Platform default.** It is `Platform::WidgetHelper::Enabled()`: true on
  macOS, builds with packaged libraries (`DESKTOP_APP_USE_PACKAGED`)
  included and the Mac App Store build (`OS_MAC_STORE`) from macOS 12, on
  Windows 7 SP1 or later, Windows 7 with update KB2533623, in the Windows
  Store build (`OS_WIN_STORE`) from Windows 10 1809, and on Linux x86_64
  and aarch64, so a launch with no switch
  runs every widget in the sandboxed helper. Everywhere else it is
  false, and a launch with no switch stops every widget with `no
  sandboxed widget helper is available here: <reason>`, the reason of
  `Platform::WidgetHelper::UnsupportedReason()`, which is empty exactly
  when `Enabled()` is true and with which each platform's `Launch()`
  refuses before any spawn or connection: `the widget helper needs macOS
  12 or later in the Store build` in the Mac App Store build before macOS
  12 (`@available(macOS 12.0, *)` in `widget_helper_mac.mm`), `the widget
  helper needs Windows 7 SP1 or later` on Windows before 7 SP1 or when the
  Windows version cannot be read, `the widget helper needs update
  KB2533623 on Windows 7` on a Windows 7 whose kernel32 lacks
  `SetDefaultDllDirectories`, `the widget helper needs Windows 10 1809 or
  later` in the Windows Store build before Windows 10 1809 or when the
  Windows version cannot be read, `the
  widget helper is not available on this architecture` on other Linux
  architectures and `the widget helper is not available on this
  platform` on the non-Linux all-other platforms. A build configured with
  `DESKTOP_APP_DISABLE_PRIVATE_QT_API` stops every widget the same way on
  every platform, with the reason `this build cannot bound the clips of
  widget pictures without Qt's private API` (`tgw_remote.cpp`): its painter
  charges nothing for the copies that nested clips make
  (`tgw/tgw_painter_clips.cpp`), so its client never starts the helper,
  which such a build still builds, and reads no Debug switch.
  On Windows it is true exactly when `UnsupportedReason()`, defined in the
  client's `platform/win/widget_helper_win.cpp`, returns no reason,
  whatever the architecture. It returns
  `SandboxUnsupportedReason()` of the shared `widget_sandbox_win.cpp`,
  which checks only that the version reading maps to a sandbox tier and,
  on the Windows 7 tier, that kernel32 exports `SetDefaultDllDirectories`
  (the update the helper's `Adopt()` refuses without), so such a system
  stops at that place instead of failing every start (**The Windows
  sandbox tiers** below). In the Store build only Windows 10 1809 and
  later map to a tier, the full one, so it keeps the floor and the reason
  it had before the tiers (**The Windows Store build** below).
  On Linux it is true on x86_64 and aarch64 in every build, Debug and
  Release, with or without the harness, and has no packaged exclusion:
  distribution builds against packaged libraries, Flatpak and Snap default
  to the sandboxed helper as well (`kLockdownAvailable` in
  `widget_lockdown_linux.h`). Other Linux architectures have no seccomp
  filter and stop their widgets. The helper links no Qt, GLib or other
  packaged library, so the libraries a build against packaged libraries
  loads never reach it; only the helper's own link set
  (**The shared helper** below) loads before its `main`, and a library
  there that started a thread would make every helper refuse with `the
  widget helper has <N> threads before its lockdown` (**The Linux
  lockdown** below). The client decides once, on the main thread, when it
  constructs its first widget host (`Tgw::PrepareRemoteEngines()` in
  `Runtime::construct()`), keeps that choice for the whole process and
  names it in the main log: `Tgw: Widget engines, validators and renderers
  run in the widget helper.` or `Tgw: Widget engines, validators and
  renderers do not run, no sandboxed widget helper is available here:
  <reason>.`. Every widget takes a hosted route, the helper's or, where no
  helper is available, one that refuses it with that reason. Once it chose
  the helper no widget of that process runs anywhere else: a helper that
  cannot start or refuses its lockdown ends its widgets with a diagnostic,
  and the failure limit stops further starts (**Runtime log and deaths**
  and **Failure limit** below). Release builds,
  `TDESKTOP_RELEASE_TEST_HARNESS` ones included, read no switch and never
  probe.
  Telegram has no `-widgethelper` entry on any platform; its
  helper, where it has one, is the separate `TelegramWidgetHelper` (**The
  shared helper** and **The Windows helper** below), whose Release build
  contains no probe and accepts only its channel token. On Linux
  `Telegram -widgethelper 3` starts an ordinary Telegram, which takes both
  arguments as start URLs and answers no channel, and on Windows
  `Telegram.exe -widgethelper <handle>` started an ordinary client that
  answered no valid opening on the inherited pipe
  ([Verification](#verification)). A Windows helper that runs without the
  client's confinement refuses in its lowering (`LockDownHelper()`, **The
  Windows lowering** below) before Ready. The Windows helper has run on
  Windows 11 build 26300 (10.0.26300): in Win32 (x86 on WOW64) and x64
  Debug builds, and from Release packages (test harness on, LTO off), x86
  installed, portable and updated and x64 installed, and in an x64 Store
  Debug build installed as a test package
  ([Verification](#verification)), all with the QuickJS engine, before
  the Nova change; with Nova, the x86 and x64 helpers have since run
  there under the full policy in Debug and Release-configured (LTO off)
  builds
  (`2026/11/21/build-the-x64-windows-widget-helper-image-for-windows-7`
  and
  `2026/11/21/build-the-x86-windows-widget-helper-image-for-windows-7`,
  their `work/test.md`). Before that build it has run on Windows 10 22H2
  (10.0.19045.6466) in a Hyper-V guest: the x86 (on WOW64) and x64 Debug
  clients and their Nova helpers, outside the Store, under the full tier
  that each process chose from its own unforced version reading, once
  the non-Store package SID was fixed data (**The Windows lowbox** below)
  (`2026/11/15/run-the-lpac-windows-widget-helper-on-a-windows-build-before-26300`,
  its `work/test.md`). ARM64, an x86 or Release Store build, a Release or
  Store build before 26300, the other builds from 10.0.17763 to
  10.0.26299 and a Windows before 26300 on bare metal have not. A tier
  other than the full one runs only as a simulation on build 26300,
  forced by a disposable overlay, and a real Windows below 10.0.17763 is
  field coverage (**The Windows sandbox tiers** below).
- **Debug switch.** Debug builds that keep the private Qt API also read
  `TDESKTOP_WIDGET_ENGINES` at that point:
  - unset or any other value: the platform default, so a Debug build runs
    its widgets in the helper wherever that default is true and stops
    every widget elsewhere;
  - `helper`: the sandboxed helper explicitly, the same as the default on
    macOS (the Mac App Store build from macOS 12), on Linux and on Windows
    7 SP1 or later, so command lines that name it keep working; it selects
    the helper, never its Windows sandbox tier; it hosts
    the whole widget as the default does (**Hosted widgets on macOS, Linux
    and Windows** below), and on Windows it is the restricted helper (**The
    Windows helper** below);
  - `probe` and `probe-unsandboxed`: on macOS every helper, whose Debug
    build carries the probe, hosts its widgets as `helper` does, probes its
    lockdown, or runs the same probe without it as the control, and
    refuses the stream instead of serving a widget, unless the client's
    probe fixture asks for the serving variant, which a disposable overlay
    gives (`rendering:<ms>`,
    [Serving while rendering](test_widget_helper_probe.md#serving-while-rendering));
    its listing goes to
    `app_stderr.txt` and its summary to the main log's `stream ended` line
    ([test_widget_helper_probe](test_widget_helper_probe.md); the Mac App
    Store service gets the mode in its channel message,
    [Mac App Store](test_widget_helper_probe.md#mac-app-store)). Each
    refusal counts as a failure (see the failure limit below). On Windows
    `probe` runs the restricted helper with its native probe and then
    hosts its widgets as `helper` does (**Windows probe** below), and
    `probe-unsandboxed` makes every helper refuse the stream with `the
    widget helper probe is not available on this platform`, each refusal
    a failure as well. On Linux both start the sandboxed helper, whose
    Debug build carries the probe too; it writes its probe lines to
    `app_stderr.txt` and then serves widgets, hosting them as `helper` does
    (**Linux probe** below):
    `probe` under the real lockdown, which still refuses the stream when
    it fails, and `probe-unsandboxed` with no lockdown at all, so that
    helper runs its widgets unconfined.

  The helper never reads the variable: the client passes a probe value to
  it as the mode argument after its channel (**The shared helper**, **The
  Mac App Store service** and **The Windows helper** below). The launch
  passes the variable to the
  client, on macOS:

  ```sh
  python3 .agents/skills/process-inbox/scripts/workspace.py test-run \
    --exe out/Debug/Telegram.app/Contents/MacOS/Telegram --run-dir "$run" \
    --env TDESKTOP_WIDGET_ENGINES=helper
  ```

  and on Windows, from PowerShell:

  ```powershell
  python .agents/skills/process-inbox/scripts/workspace.py test-run `
    --exe out/Debug/Telegram.exe --run-dir $run `
    --env TDESKTOP_WIDGET_ENGINES=helper
  ```

  On Linux the first command takes `--exe out/Debug/Telegram`, whose
  helper the same build puts at
  `out/Debug/TelegramHelpers/TelegramWidgetHelper`. The Windows command's
  Win32 build puts its helper at
  `out/Debug/modules/x86/TelegramWidgetHelper.exe`, and an x64 build into
  `modules/x64/` next to its own `Telegram.exe`. A Store build is run from
  an installed test package through its alias instead (**Windows Store
  test runs** below). The main log names a
  recognized value: `Tgw: Widget engines, validators and renderers run in
  the widget helper (TDESKTOP_WIDGET_ENGINES=helper).`, the same with
  `probe` or `probe-unsandboxed`; any other value is logged as the default
  is, with no value named.
- **Hosted widgets on macOS, Linux and Windows.** The helper always
  renders: every macOS, Linux and Windows build, the Mac App Store build,
  both Linux layouts and the Windows Store build included, builds it with
  the thin `_other/widget_helper_rust.cpp` entry over twidget's Rust
  serving (`twidget_helper_ffi`, `lib_widget_helper_rust.cmake`), whose
  hosted glue keeps the renderer message rules `RendererWork` keeps
  (`tgw/tgw_renderer_rules.h`, mirrored in twidget's `rust/crates/helper`),
  while Telegram links only TwidgetCore's platform end with its wire,
  remote messages, PNG and filter code (`lib_widget_renderer_platform`). The default, `helper`
  and the probe values host each widget in the helper, in Release with no
  switch at all:
  - *Where the work runs.* The helper serves the hosted opening
    (`serve_hosting`): at each widget's hosted start, after its lockdown,
    it builds a served host with the engine, the trusted validator and a
    renderer end over twidget's Rust litehtml, built on the glue thread
    from the client's `Open` control with the device pixel ratio. Layout, style, display lists, layout queries and held context
    menus are the helper's, and so is the check of each canvas font
    spelling, under Telegram's rule (*Canvas fonts* below). The client builds a hosted `WidgetHost` (no
    Nova runtime, no validator, no litehtml document, cursor styles
    included), whose client decides every privileged call again on the
    presses its stream writer admitted (*Presses and grants* below), and
    one `RendererWork` per widget on
    `widget.render` driving the shipped platform end (`RendererPlatform`):
    its `QtBackend` answers the renderer's font, text-width, image and
    canvas requests and paints each display list with its checks (the
    paint bounds, the raster deadline, the image formats),
    and the work keeps the clock, the input serials, the sizes and the
    pictures. The embedding's own controls cross as opaque platform
    messages (`tgw/tgw_hosted_wire.h`: `Open`, the deliveries of held
    context menus, `CanvasRefill` to the helper, `Content`, `Loaded`,
    `Stopped`, `Cursor`, `CanvasRefused`, `Clicked` from it, the Debug
    mirror). The
    main thread runs no widget layout, validation, text measuring or
    raster. A widget's start note says its engine, validator and renderer
    run in the widget helper.
  - *Hold and timeouts.* The helper's requests and messages for one widget
    wait in one queue on `widget.render`, in arrival order, each consumed
    inside one charged pass of that widget's meter, and the whole queue
    waits while the widget's hold lasts (input exempts 250 ms, and a
    queued request with the entries before it goes ahead of the widget's
    own new raster and tick work). A
    request's answer is due 3 s after its arrival, however long input
    keeps renewing the exemption. Only a hold the request actually meets
    before its admission, while it or a request ahead of it waits, moves
    that deadline: a release the meter published still ahead with no
    exemption in force, at its arrival, when the meter publishes a later
    release while it waits, or from the end of an exemption that the
    release outlasts and no renewal reached in time (the waiter wakes at
    that end to read the record again, each renewal moving the wake).
    Then it is due 3 s after that release, a later release moving it
    again. Its admission makes it due at most 3 s from then, never later
    than it was, and nothing moves it after that (widget-sandbox's
    `SandboxHostedWait`). An
    answer that never comes still ends the widget at that deadline with
    `platform-timeout`, without another event. The served host's own 3 s
    for a layout query counts only time its glue did not spend waiting for
    the client's answers. The chat's 150 ms menu and claim deadlines are
    unchanged.
  - *A renderer that stops answering.* The widget helper's stream times its
    renderers' answers by Telegram's reading of the hosted messages,
    `Tgw::HostedAnswers()` (`tgw_remote.cpp`), which `Manager::launch()`
    gives every hosting start (widget-sandbox's `SandboxHostedAnswers`).
    The Linux source selects the same manager and classifier; the deadline
    was measured only in the macOS arm64 direct-download Debug client
    ([Verification](#verification)).
    Telegram expects exactly one answer, per widget in the order it sent
    them, to each frame request (the frame of its sequence, painted or
    unchanged, a painted one whole or as a frame delta), each touch or
    hover (a Pointer) and each claim query (the claim answer of its
    sequence); every other message it sends, a width, height, theme or
    selection change among them, expects none. A frame
    request is timed only when sent while the widget is on screen; a touch,
    a hover and a claim query are timed whatever the widget's visibility.
    The runtime gives the host a visibility change before the frame request
    it causes (`Runtime::syncHostVisibility()`), so a frame request sent off
    screen is never timed, and one sent on screen stays timed when the card
    leaves the screen. A timed message's answer is due after 3 s of the
    renderer's own time from the stream writer's admission of that message
    (`SandboxEngine::kPlatformAnswerSeconds`): the time from the arrival of
    one of the renderer's own requests (*Hold and timeouts* above) until
    Telegram's answer to it is written is not that renderer's, and an answer
    counts at its arrival from the helper, however long it then waits on
    `widget.render` behind Telegram's own work. An answer settles nothing
    when it does not answer its widget's oldest owed message (another family
    or another sequence), when it is past its size cap (for a frame or a
    frame delta the 8 MiB `HostedWire::kMaxFrameBytes`) or when the stream's
    own reading refuses it (`ReadExchange()`): a claim answer TwidgetCore's
    `decode()` refuses, a pointer cut short, malformed or with bytes after
    its fields, a frame whose fixed part (sequence, state, flags) is cut
    short or malformed, whose list or damage has another form or version, a
    flag that is no flag or a count past its cap or past what the bytes
    after it can hold (for an empty list, any byte after it), whose damage
    is not whole or that has bytes after its values (`FrameComplete()`,
    `ListAdmitted()`, `DamageComplete()`), and a frame delta TwidgetCore's
    `delta_scan()` refuses (its fixed part, a run of an unknown tag, empty,
    past the bytes or with a copy past the 256 KiB delta base, runs that do
    not make its length exactly, a damage value not whole or bytes after
    it, the frame it stands for past the caps), whose list length no list
    `ListAdmitted()` takes can have (exactly 6 bytes empty, at least 11 with
    a command) or whose damage `DamageComplete()` refuses (`DeltaKey()`).
    Those frame checks read a list's header and command count, never its
    commands: a painted frame that passes them earns its request's credit
    even when TwidgetCore's `wire::decode()` would refuse its commands, and
    TwidgetCore's platform end on `widget.render`, which decodes the frame
    whole, refuses it only afterwards, a stop of the widget's own (below). A
    frame delta's checks walk its runs, at most one more than the command
    cap, without making them, and never rebuild its list, which needs the
    kept base only that platform end holds: a delta that passes them earns
    its credit even when its base is not the last painted frame or its
    copies pass the kept list, and the platform end, which rebuilds and
    decodes it, refuses it only afterwards, as it does a frame's commands.
    The cases the self-check reads are listed under
    [test_widget_ops](test_widget_ops.md#verification). A deadline runs only
    while a timed message is unanswered: a card with none outstanding sets
    none, whether idle, off screen or static, and one that answers within
    3 s never reaches it. In the campaign an idle chat whose running card
    answered its timed frame requests in time, cards placed off screen with
    nothing timed admitted there and a static card with no frame request
    admitted each kept the helper for 10.5 s
    ([Verification](#verification)). A card that leaves the screen still
    owes, on its deadline, the frame of a request it sent on screen. A frame
    request sent off screen is never timed but still owed in order: an
    answer to a later message that comes before its frame settles nothing,
    and at an end that is neither the unanswered one nor the owner's stop
    its route counts among those with a message unanswered (`Ordinary`, not
    `Spared`), so its run is charged (**Runtime log and deaths** below).
    A look of the
    stream's monitor more than 1 s after the wake it planned (a Mac that
    slept, or a thread that did not run) concludes nothing and looks once
    more 1 s later, so a run that measures the 3 s must keep the Mac awake.
    When a timed message's answer is overdue the stream ends with `a hosted
    renderer left a platform message unanswered for more than 3000 ms`, and
    the manager retires and reaps that helper as after any end it did not
    ask for (`Manager::ended()`, `retireLocked(false)`, `reap()`):
    `complete(kExitWait, kKillWait)` waits up to 1 s for its exit and only
    then kills it and waits up to 1 s more (in the Mac App Store build it
    cancels the service's connection instead, **The Mac App Store service**
    below; read, not run there). That end counts as one helper failure
    (**Failure limit** below). At that end the stream decides each live
    route that takes part (`Connection::blameLocked()` in widget-sandbox's
    `remote_stream.cpp`): `Stalled` for each one whose oldest timed message
    was overdue at that instant, one or several, `Spared` for the others.
    The run of a route it decided `Stalled` is charged, with the reason
    `The widget stopped responding` (`StalledReason()` in
    `tgw_renderer_work.cpp`, which `HostLink::postDeath()` gives when its
    own channel's `death()` is `Stalled`, and `RendererWork::refuseRoute()`
    when Telegram refuses that route after the stream decided);
    **Runtime log and deaths** below says which widget, if any, shows that
    stop. A route it decided `Spared` gets no charge from
    that end; whether its widget then starts again is **Runtime log and
    deaths** below. A widget whose route Telegram refused for good first (a frame
    of another sequence, a bound: `RendererPlatform::refused()` and
    `fail()`, `RendererWork::refuseRoute()` and `backendRefused()`, through
    `HostLink::localStop()`), or whose hosted client a counted stop
    (`SandboxEngine::stop()`) ended first, stops with its own reason, and
    the same step (`SandboxHostedChannel::localStop()` and the client's
    claim of its own death) takes everything its renderer owed out of the
    stream's account, so none of it times out the helper its siblings still
    use. Such a refusal is that widget's own charge, once, also when an end
    that spared its route came first: until the run settles at that end's
    marker (*Deaths* below), a surviving core that refuses an answer the
    stream had already credited still charges its run (`Refused`, with the
    stream's `Spared` kept apart).
  - *Frames.* One frame request is outstanding at a time, a tick apart
    from a paint. A frame that answers no outstanding request, or a tick
    with a paint or the reverse, a malformed state or a list the checks
    refuse closes that widget's route and stops it as a stop the desktop
    decided itself (`paint-bounds`). A frame is checked against its
    request before its state counts: one for an older size or pixel-drop
    generation commits nothing, its state included, and the work asks
    again on a later turn while demand remains; a current paint of
    another device size than the client computes, or one whose layout the
    card cannot take (its card area under the cut off the device size, or
    an auto-height cut at another logical height than the document's,
    `FitsLayout()` in `tgw_renderer_work.cpp`), commits its state but is
    not painted and publishes nothing. It asks again only after a new
    size, a cut change or a draw demand, or after a document height or
    state change that makes the client's size, the helper's viewport and
    the document height agree; the same reports again, or a change that
    leaves them apart, ask nothing. Such a refusal also forgets the
    geometry the work last painted, as a refresh of the pixels does
    (`RendererWork::refreshFrame()`): the refused paint may carry newer
    content than the chat's picture, so a return to that picture's
    geometry no longer counts as painted. The chat keeps its picture,
    height and anchor until a new one comes, and the work keeps its record
    of what the chat was given (below). A
    build with `DESKTOP_APP_DISABLE_PRIVATE_QT_API` never hosts: without the
    private API the painter charges nothing for the copies that nested
    clips make, so such a build stops every widget (**Platform default**
    above). Every accepted list is painted whole,
    inside the painter's 1 s raster deadline; the helper's damage is
    ignored. The painted picture goes to the chat at once (*Kept
    pictures* below), and a draw demand or a cut change that came while
    it was painted is still served: the work asks for a new frame
    whenever its last picture does not match the card's size, cut and
    pixel-drop generation. Whether the document still owes the chat a
    picture of its latest geometry, whatever the clock, is one record of
    the work (`RendererReflow`): the geometry of the picture last offered
    to the chat (size serial, pixel-drop generation, cut, device size,
    card area and the document's CSS height), the owed picture and one
    correction. A draw demand, a cut change, a new size that withdraws an
    untaken picture (also one the chat's wake left in the mailbox because
    its serial or generation is obsolete), a new size that a live card on
    screen gets while a request for an older size is out (owed when the
    size arrives, so a stop that runs before the work turn keeps it), a
    paint for a new size waiting behind an older request, and a current
    answer the stop consumed for a geometry the chat was not given each
    owe a picture; it lasts while a paint request is out, while a paint
    refused for its device size or layout (above) waits for the change that
    lets it ask once more, and through the stop until the retirement, and
    ends when its picture goes into the mailbox, or on a turn with nothing
    to paint. Until the chat draws that
    picture or a new demand, a size, cut or visibility change comes, a
    widget whose state turns dirty again asks for no further paint; a
    demand waiting behind a tick request survives the tick's answer. A
    kept card's new size with nothing to withdraw and no live request out
    asks nothing until the chat draws the card. A picture that served a
    demand or a new size or cut (one the clock asked for included) allows
    one correction: a kept document whose geometry, its CSS height
    included, moves after it, at the same size, asks once more for its
    settled layout, since a detached work has no clock to ask later; the
    correction is used when it is asked for and its own picture allows no
    other, and its current reply settles it whatever it brings: a null
    destination, another device size or a layout the card cannot take
    leaves the chat its picture and spends the correction: no report makes
    the correction ask again, but a demand that came meanwhile stays owed,
    and after a refused paint it asks once when a report makes that paint
    publishable. A move after that, or after a correction that published
    nothing, only marks the chat's picture as out of date: one mark per
    work in its mailbox (`PicturePost::setOutdated()`), posting nothing,
    which the turn that ends with nothing owed raises and the chat's next
    draw takes (`RendererCore::frame()`) to ask for the latest state. A
    geometry back at the chat's picture lowers it and a newer picture
    replaces it; it is raised again only for a geometry it was not raised
    for, whether the draw that took it got a new picture, the one the chat
    holds or none. A refresh of the pixels (an emoji-set change)
    keeps that record; a pixel drop and the route's end clear it. A paint
    that could not allocate its picture (a null destination, no paint
    limit) publishes nothing and is not asked for again until the size,
    the cut, the pixel-drop generation or the document's state changes,
    or until a later paint is refused (above). A frame painted
    while an emoji tile was still loading asks
    for a fresh frame once a tile lands or a quota retry comes, while the
    route lives. A large glyph a picture shows as its large-tile fallback,
    or not at all (its own image loading, refused while the 16 MiB pool is
    full, or failed), is recorded in that picture's waits by the glyph
    (`EmojiWaits`, one bit for each glyph of the emoji catalogue, made
    with the account and the work before any paint), and only that glyph's
    arrival asks for a fresh frame: of every card whose picture shows it
    so, whichever card's request loaded it, and of no other card, however
    many glyphs the picture waits for. A paint that published no picture
    keeps the waits of the picture the chat still shows. A change of the
    emoji set refreshes every card.
  - *Presses and grants.* An accepted primary press goes to the live
    route through `WidgetHost::sendPlatformPress()`, stamped at that call;
    `RendererPlatform::Down::Live` says only that Telegram sent it on a
    live route. One job of the client's dispatch thread hands the down to
    the stream's writer and, only when the writer admitted it for a route
    still live, starts the haptic window and, while visible, the one-use
    grant, both timed from that stamp. A down the writer refused (a route
    not live, a stream ending, a queue past its bound) grants and plays
    nothing, and its release opens no haptic window either. Its release,
    cancel, the widget leaving the screen or a detach
    `releaseDelivered()`: one privileged call within 2 s while visible, as
    widget-sandbox decides; losing the route revokes the grants and the
    press. A down buffered for a keep, sent to a kept route or lost grants
    nothing, and neither does a helper's report or a secondary press. The
    helper's renderer answers every pointer input with a Pointer reply,
    whether or not it decided a click: the replies to a primary mouse down
    and to the up that ends it, taken while the route is still active,
    only prove their delivery. When the renderer decides a click for that
    up (the press and the release on one element, or one inside the other,
    the decision its `click` event follows whether or not the widget
    listens), the glue sends a `Clicked` control with the press's number
    (the count of downs both ends keep) right after the up's reply, and
    `HostedInput::clicked()` takes it only for the press whose up was just
    answered: an observation, which grants nothing and changes no gesture
    decision. The harness counts the steps of the marked press alone
    ([Hosted widgets](test_widget_ops.md#hosted-widgets)); an up whose
    target the down's handler replaced gets its reply and no `Clicked`. A
    right press first asks the helper for
    the claim at its point (ordered after every earlier input) and sends
    its down only when that claim takes it; a right press's and a finger
    hold's `contextmenu` wait in the helper until the client's timely
    positive verdict for that press, and a negative one sends only the
    cancel. At most 256 input replies may be outstanding per widget; the
    next stops it as a checked stop on its last picture.
  - *Cursor.* The glue evaluates the cursor at the pointer's last position
    at two points: right after each
    pointer input it delivers (a touch or a hover), whose own Pointer reply
    reads the element's cursor before the new hover and active elements
    are restyled and so lags a move behind a `:hover`- or `:active`-only
    rule; and, while a pointer rests over the widget, after each step that
    rebuilt the document (new ops, a width change, a theme). Its
    `Glue::hand_at()` runs `CursorStyles::pointer()` (`cursor.rs` of
    twidget's `rust/crates/helper`) on the helper's renderer, with the hit
    from the no-event `elementFromPoint` local query and the hover and
    active elements chosen from the hit, a held primary press and the
    element it pressed by `pointer_targets()` of `rules.rs`. Its
    `Glue::track_press()` follows the primary press from the touches it
    delivers (where it went down, the element hit there, the documents
    parsed by then) and ends its `:active` for the rest of the press once a
    move past the slop finds another element or a reparsed document. It
    builds its cursor document lazily, at the first
    hit on an element (a hit on none is `auto`, with no document), drops
    it at hover leave, a freeze and a stop, and sends no DOM event. It
    keeps only whether the cursor is the hand, the one value Telegram
    reads, starting from what the renderer's own Pointer reply said: when
    the evaluation differs from the last value reported, it sends a
    `Cursor` control (`pointer` or `auto`) with the count of pointer
    replies sent before it. The client takes it only after exactly that
    many replies (any other count closes the route) and applies it as the
    answer to the last one, so a pointing hand over a replaced or
    reflowed `:hover` target turns back without another move, and a
    `:hover`- or `:active`-only hand shows at the input that reaches it. Telegram hands a hosted
    widget's cursor and selection to the chat through one report post
    (`ReportPost`): the latest value of each kind and at most one wake of
    the main thread, taken by `RendererCore::takeReports()`, which applies
    the selection, then the cursor (a hand only for the current input
    serial); menu and claim answers stay in the ordered answers. A
    stalled main thread therefore finds at most one cursor per widget,
    the newest, and the post is closed, dropping both, when the route
    deactivates, closes, detaches, freezes or stops.
  - *Canvas fonts.* A canvas `font` spelling gets Telegram's rule in the
    helper, the glue's `RendererRules::CanvasSpellings`
    (`tgw/tgw_renderer_rules.h`): a spelling over 512
    bytes is refused before it is parsed, one seen since the cache was
    last cleared answers from that cache (64 spellings, cleared when
    full), and at most 64 new spellings are parsed per refill. The glue
    applies it in the renderer's canvas font hook
    (`Renderer::set_canvas_font_hook`), installed right after it builds
    the renderer end on `Open`, so the renderer parses and caches no
    canvas spelling itself and the raw spelling never crosses the stream;
    an admitted one becomes the same font request as before, whose
    resolved families and font handle Telegram still checks, and a new
    font gets its em box as `Renderer::canvas_font()` gives it. A cached
    spelling keeps the font Telegram answered and asks nothing again. One
    Telegram refused (a zero handle, whatever the reason) is refused for
    the rest of the interval, with zero metrics and no request, also when
    the spelling cache was cleared and parsed it again; its first use
    after the next `CanvasRefill` asks once more, so 1,500 uses of a
    refused spelling make one request per interval, and a pool still full
    refuses again. At most 64 canvas font requests go out per interval,
    retries included; past them a spelling is refused in the helper like
    one past the parse limit. The refill is Telegram's: when
    `QtBackend::beginFrame()` refills (at most once per frame interval,
    never while the widget is held), `RendererWork` sends a `CanvasRefill`
    control ahead of the frame request it precedes, a tick's as well as a
    paint's. A spelling the rule or the request limit
    refuses is answered in the helper with no font (nothing drawn, a width
    of 0); the widget's first such refusal sends one `CanvasRefused`
    control, which Telegram notes
    as the widget's one `canvas-font-limit` diagnostic
    (`QtBackend::noteCanvasFontRefused()`). Telegram's own refusals of a
    font request (its check of the families, 16 new canvas fonts per
    frame, 32 per widget) are handled on its side, and the helper sends no
    `CanvasRefused` for them.
  - *Deaths.* The hosted host's `onDeath` reaches `HostLink::postDeath()`,
    which reads its own channel's `death()` once, at that admission. A
    `Stalled` or `Ordinary` death, a kill of the client's own (the
    helper ended, a flood, an unanswered request) included, records the
    run's decision there and freezes the widget's core on its last
    picture, which a widget still live on that core shows under the
    localized pill, with the diagnostic in its runtime log (`The widget
    stopped responding` for a `Stalled` death, *A renderer that stops
    answering* above). A
    `Spared` death only ends the widget's input
    (`RendererCore::hostFrozen()`); a marker goes to `widget.render` behind
    the traffic the helper sent before the end (`RendererWork::hostDied()`),
    and the run settles `Spared` once the work consumed that traffic, a
    credited answer still held there included (`HostLink::reached()`),
    unless a refusal of it recorded the run's decision first. The decision
    recorded first stays the run's: its posted application, a withdrawal
    at the core's retirement or the core's disposal (`SealRoute()` in
    `tgw_runtime.cpp`, `HostLink::forget()`) delivers it, whichever comes
    first, a refusal whose posted application was still delayed included.
    A disposal that finds none recorded ends that order:
    `Runtime::RunGone()` finalizes the run from the withdrawal, `Spared`
    here, at once or once the stream settles the route; traffic not yet
    delivered to the work is dropped (`HostLink::postTraffic()`), and no
    refusal can charge the run any more (`HostLink::localStop()` records
    nothing once the link is gone). What a final run does next is
    **Runtime log and deaths** below. An
    attached
    work publishes the stop only after it has consumed the traffic the
    helper sent before the death (its platform queue drained), and, when
    that traffic reported a paint-limit stop and the card is on screen,
    after the answer to its ask for the stopped frame, so the card shows
    that stopped frame. A stale answer (an older size, or
    a picture the chat dropped since) asks again, and no frame is asked
    for when none can be (not loaded, no size or cut, a blocked size, or
    a clean picture already painted). Off screen it waits for the drain
    alone. Once detached, or once the route's end reaches Telegram, it is
    published at once, as before. Every hold ends at the latest
    `SandboxEngine::kPlatformAnswerSeconds` (3 s) after the freeze, and
    the stop then goes out without the stopped frame: a live route that
    ends with no word to Telegram (the helper crashed or was killed, a
    flood or backlog cut by the client) leaves that ask unanswered. From
    the stop's publication until the chat retires its core, the live
    work asks the helper for no paint and paints no late answer, so the
    picture it published with or before the stop is the one the chat
    takes with it or before it, and the retirement sees that picture's
    height. A reflow still owed (an explicit demand, a current answer
    the stop consumed for a size, cut or height the published picture
    does not have, a clock-driven resize included, or a newer size whose
    paint waited behind an older request, that request's obsolete answer
    consumed in order) is asked for once, on the kept route, once the
    core is retired with a kept document; a pixel drop or the route's
    end before that cancels it.
  - *Kept renderers.* A loaded widget retired by an ordinary release or a
    natural stop keeps its document in the helper
    (`WidgetHost::keepRenderer`), so its still can reflow: at most 8 kept
    routes in the process, a keep in progress and a release not yet
    complete counted; with all 8 taken the newly retiring widget's route is
    closed instead. A checked refusal or a drain never keeps. Nor does a
    widget retired after its helper's end came first while its run still
    awaits that end's marker: its still holds the host and the route open
    (`Runtime::keepStill()`, `HostLink::holdRoute()`), outside the 8, until
    the work reaches the marker, and the route then closes; a disposal of
    that still first ends the hold and settles the run as *Deaths* above
    says. Kept routes
    do not hold the helper's 30 s idle exit; that exit, a crash or a
    release ends them, and their stills lose their documents. A route
    that ended for good also ends its work's wait for emoji, and the
    work's next turn frees its images, fonts and canvases and closes its
    account in the emoji cache (`QtBackend::releaseLive()`), whose large
    glyphs then make room for other cards; a kept route keeps them all.
    A large glyph's request still loading keeps its pending entry, by its
    own admission number, through that close until its completion: then
    its bytes return, the entry goes and the cards showing that glyph's
    fallback are asked to paint again, so a live one requests it itself.
    Each completion settles only its own request's reservation and entry,
    so one admitted before an emoji-set change cannot end a newer
    request's wait.
  - *Kept pictures.* Telegram keeps no drawing commands of a hosted
    widget, only pixels:
    - *Pictures on their way to the chat.* Each hosted work hands its
      pictures to the chat through one mailbox of its own
      (`PicturePost`, shared by the core and its render work): a helper
      frame once it passed its checks and was painted, a live widget's and
      a kept document's reflow alike. The painted picture goes into the
      mailbox at once, with its size serial, pixel-drop epoch, card area,
      CSS height and bottom anchor, replacing one the chat has not taken
      yet (the replaced one drops its pixels, and a client notice it
      carried passes to the newer one), and the next work turn publishes
      it. The chat takes that exact picture only at its own current size
      serial and pixel-drop epoch, checked under the mailbox's lock
      (`PicturePost::take()`), and accepts it
      (`RendererCore::applyLayout()`); an obsolete one stays for the size
      or drop the chat already posted to withdraw. The work's other
      answers (content and load, a stop, the route's close, menu and claim
      decisions) keep their own posts in order; a picture published with
      one of them is taken at that answer, and one published alone only at
      a post after the last answer before it, so it never lands before a
      stop, a close or a menu or claim decision that preceded it, and a
      turn that has only a picture posts one wake when no such post is
      still queued, never one per picture. The wake carries the core's
      weak pointer and no picture. A new size serial or pixel-drop epoch
      withdraws an untaken picture on the render thread, published or not
      (a new size owing the picture for the new size, a drop cancelling
      it); the core's destruction and the work's empty the mailbox at
      once, a wake still queued included. Retirement and route loss keep
      it. So each work has at most one undelivered picture, of its checked
      destination size; there is no process-wide allowance, and no request
      waits for another card's pictures. Each work has at most one request
      out, and each matching current reply is decoded once by the platform
      end, inside that route's metered work, and painted at once at its
      trusted final size, so eight static cards each needing a new picture
      take eight requests, eight decodes and eight accepted pictures
      however late the chat takes them.
    - *Counters.* `queued` and `queuedPeak` (`KeptBudget::Read()`) read the
      distinct backings of pictures on their way to the chat, from their
      holder's creation until the chat takes them or their last holder
      drops them (a release deferred past a backing being made included).
      `physical` and `physicalPeak` read the distinct hosted backings held
      (queued pictures and displayed copies), each counted once when the
      painter has made it, until its last holder drops it
      (`KeptBudget::Backing`, carried by the picture's holder,
      `KeptBudget::Pixels`); a picture's own image goes before its holder
      (`RendererLayout::pixels` is declared first), so a stale, replaced,
      withdrawn or closed picture stays counted until its bitmap is freed.
      An allocation that fails counts nothing.
      The making and its count are one step against releases on other
      threads (`KeptBudget::Acquisition`): a holder the chat drops while a
      backing is being made is freed right after it, so both counts see
      that overlap and none other.
    - *Memory.* A picture's holder is made right after its paint, and each
      distinct backing is counted when it is made. Fixed-size internal
      pieces (a post, its callable, a holder, a wake, the post that hands a
      committed emoji tile request over, a stop's fixed diagnostic) are not
      recovered when they cannot be allocated: that ends Telegram, as
      everywhere else in the client. What the helper's input sizes ends
      just the route when memory runs out: reading its message, whose size
      the caps bound before anything is allocated
      (`RendererPlatform::consume()`), the owned text made from a stop
      report or a selection before any callback gets it, and the checked
      painter's own workspace (`Executor::run()`: its group inks, dash
      patterns, text and stacks, and an emoji tile request's preparation,
      made before the cache changes: the eviction candidates it gathers,
      its batch slot and its request entry, for a large glyph's own
      request and its large-tile fallback together), a paint limit like any
      other: the frame ends its route with its temporary bytes returned and
      the picture the chat shows kept. A stop the helper reports is
      published to the chat with its reason before anything else is
      allocated for it, and the host's diagnostics get a fixed detail for
      it (`paint-bounds`: `The widget renderer stopped`), never another
      copy of the reason.
    - *One budget for kept cards.* A card that is not running keeps at
      most one image, its last accepted pixels (its still's displayed
      copy, `RendererCore::frameBytes()`). The runtime registry trims them
      on main (`Runtime::TrimKept()`, `KeptBudget::PlanTrim()`) at once
      after an unload and after any unloaded still's new picture, one of a
      message edited since included (its other notices stay off), in every
      placement: the pictures of unloaded cards
      stay within 48 MiB (`KeptBudget::kPictureLimit`), and while they do
      not, the card seen longest ago (oldest unload first, ties by work
      id) loses its picture completely (`RendererCore::dropFrame()`).
      Pictures of cards on screen (painted since their unload) are counted
      apart (`on screen`) and never dropped; they stay bounded by the
      per-picture limits. A card keeps the order of its first unload until
      it is painted again, through further unloads with no paint between
      (a draw clipped to its footer registers the card without painting
      it). A trim that drops writes the Debug line `Tgw: kept
      trim, unloaded pictures … of 50331648, dropped …, on screen …;
      queued … (peak …), pictures … (peak …).`
    - *Cards with no picture.* A card whose picture was dropped, or that
      changed width while not running, draws nothing on its card: the
      bubble at its last height, as before its first picture, under the
      stop pill when it stopped or failed. A kept document in the helper
      reflows at a new width and paints a dropped picture again when the
      chat draws the card; without one (beyond the 8 routes, after the
      helper's exit or a crash) the card waits. Drawn on screen again, a
      card that can start starts again (`Runtime::ScheduleStarts()`) and
      its new live core draws it; one that cannot (stopped, failed, past
      its crash limit or with newer content) keeps the empty card.
  - *Failure.* A helper that cannot start or refuses its lockdown ends its
    widgets with the diagnostic and counts towards the failure limit, as
    below. Where no helper is available the client builds only a refusing
    hosted client, and no Nova runtime, validator or litehtml document
    exists in the client either.
  - *Seams and probes.* The ops tap installs itself on the hosted host
    ([Hosted widgets](test_widget_ops.md#hosted-widgets)) and, in a
    Release harness, whose helper has no Debug tap, reads the theme the
    widget itself took after each application and the clicks the helper's
    renderer completed instead of the mirror
    ([Which reading each build uses](test_widget_ops.md#which-reading-each-build-uses)), the host watch
    reads the hosted host's diagnostics and its work scopes time only the
    client's share ([The helper's share](test_widget_host.md#the-helpers-share)),
    the macOS probe's serving variant takes its listing while widgets
    render ([Serving while rendering](test_widget_helper_probe.md#serving-while-rendering)),
    the Linux probe's helper writes its lines before it serves and
    then hosts its widgets (**Linux probe** below), and the Windows probe
    reports before Ready and its helper then hosts the widgets (**Windows
    probe** below).
- **Store, packaged and other platforms.** macOS, Windows and Linux have a
  helper. The non-Linux all-other platforms, FreeBSD among them, build
  none (no `TelegramWidgetHelper` target) and start none: their default
  stops every widget with `no sandboxed widget helper is available here:
  the widget helper is not available on this platform`. An explicit
  `helper` or probe value there fails closed as well: each widget dies
  with `the widget helper could not start:
  the widget helper is not available on this platform` until the failure
  limit below stops the attempts. On a Linux architecture other than
  x86_64 and aarch64 the default stops every widget the same way, with
  `no sandboxed widget helper is available here: the widget helper is not
  available on this architecture`, and an explicit value fails closed
  with `the widget helper could not start: the widget helper is not
  available on this architecture`. The Mac App Store build is no build
  without a helper any more: from macOS 12 it runs its engines in the
  App-Sandboxed XPC service of **The Mac App Store service** below, and it
  has no `Contents/Helpers` folder. Below macOS 12, where XPC has no peer
  code-signing requirement, its default stops every widget with `no
  sandboxed widget helper is available here: the widget helper needs
  macOS 12 or later in the Store build`, and an explicit Debug `helper` or
  probe value fails every start before any connection with `the widget
  helper could not start: the widget helper needs macOS 12 or later in the
  Store build`, each a failure towards the failure limit; that path has
  not run (no macOS before 27.0.1 was available).
  A macOS packaged build builds, stages and runs the same helper as every
  other non-store macOS build: the helper links no Qt, so the external Qt
  of the packaged workflow (Homebrew's) never reaches it. Every Linux build
  defines the helper. A package-managed build, one with packaged libraries
  (`DESKTOP_APP_USE_PACKAGED`) and no special target (distributions, Snap
  and Flathub), installs it into
  `${CMAKE_INSTALL_LIBEXECDIR}/TelegramDesktop/` and builds it at the same
  place relative to its built binary (`../libexec/TelegramDesktop/` with
  the default directories); every other build, the official tarball's
  (packaged too, with the special target `linux`) and the Docker
  development builds included, puts it into `TelegramHelpers/` next to the
  binary, and the install rule of a packaged one into
  `${CMAKE_INSTALL_BINDIR}/TelegramHelpers/` (**The shared helper**
  below). The release scripts, `Telegram/build/docker/build.sh` and
  `Telegram/build/build.sh`, carry `TelegramHelpers/TelegramWidgetHelper`
  as the build left it into the tarball and its v2 update package, and
  the canary workflow's Linux job into its update and its portable
  archive ([Verification](#verification)): outside Debug the helper
  target of a build in that layout has already stripped the file in its
  own POST_BUILD (`CMAKE_STRIP`, a full strip) before its digest was
  generated, and the scripts run neither `strip` nor `minidebug.sh` on
  it. An update creates a missing `TelegramHelpers/` with mode 0755, the
  umask applied, as the Linux Updater does for every install folder it
  adds, and replaces the file; whoever repackages the official tarball
  must keep that file, unchanged, next to the binary. A Telegram that
  keeps running from the same install while another instance installs an
  update, with another working folder or as another user of a shared
  root-owned install, keeps starting the helper file it held at launch
  (its helper's `/proc/<pid>/exe` then ends in ` (deleted)`), while a
  Telegram launched after the update holds and checks the new file; a
  package-managed build holds no file (**The shared helper** below). The
  Snap's `telegram` part builds with the prefix `/usr` and no special
  target, so it stages and primes
  `usr/libexec/TelegramDesktop/TelegramWidgetHelper`, which the renamed
  `usr/bin/telegram-desktop` finds. The Snap's `avif` part writes the
  forced `dav1d` link,
  `-Wl,--push-state,--no-as-needed,-ldav1d,--pop-state`, into the `Libs:`
  line of the `libavif.pc` it builds, so the link reaches only the
  executables that link the avif decoder, Telegram and the
  `codegen_emoji` build tool, and the Snap's helper, read before the Nova
  change, linked only glibc, libm and the C++ runtime, like the other
  builds' helpers. A Snap built
  with snapcraft staged the helper, and a Release Telegram with the test
  harness, linked as that Snap's, started it from the Snap's unpacked
  tree, unconfined. A Snap that snapcraft built from the recipe, with the
  test harness and a test overlay added to its Telegram, installed with
  strict confinement and started as the snap's app, ran its helper,
  byte-equal to the plain recipe's, under the app's AppArmor label
  ([Verification](#verification)).
  The Windows Store build is no build without a helper any more: it
  builds, packages and starts `TelegramWidgetHelper.exe` like the other
  Windows builds (**The Windows Store build** below). Windows before 7
  SP1, or one whose version cannot be read, stops every widget with `no
  sandboxed widget helper is available here: the widget helper needs
  Windows 7 SP1 or later`, and Windows 7 without update KB2533623 with
  `... the widget helper needs update KB2533623 on Windows 7`, because the
  default is false there; an explicit `helper` or probe value makes the
  seam fail every launch closed with the same reason, `the widget helper
  could not start: <reason>`, before it builds the helper's path, each a
  failure towards the failure limit. Neither path has run on such a
  Windows: any reading of them is a simulation on Windows 11 (**The
  Windows sandbox tiers** below). The Store build keeps the floor it had
  before the tiers: its package declares Windows 10.0.14316 as its
  minimum, and `ChooseTier()` in `widget_sandbox_win.cpp`, compiled with
  `OS_WIN_STORE` into its client and its helper alike, maps no version
  below 10.0.17763 (Windows 10 1809) to a tier, so a Store build before
  1809, or one whose version cannot be read, stops every widget with `...
  the widget helper needs Windows 10 1809 or later` the same way, and from
  1809 on it runs the full tier alone. That Store branch was read in the
  source and the CMake that defines `OS_WIN_STORE` only; no Store build
  has been built or run with it. The
  Windows default reads nothing else, so a Windows build with packaged
  libraries defaults to the helper as well; no such build has been run.
- **The shared helper.** One helper serves every widget of the client, of
  every account. The first host construction that needs one starts it from
  its `Build()` worker, and later widgets attach to the same process. On
  macOS it is `TelegramWidgetHelper`, a separate executable in the
  client's bundle at `Contents/Helpers/TelegramWidgetHelper`, next to
  `crashpad_handler` (the client takes `[[NSBundle mainBundle]
  bundlePath]` plus that path); the `TelegramWidgetHelper_bundle` target
  stages it there on every Telegram build outside the store build (the
  Mac App Store build's helper is **The Mac App Store service** below,
  which starts, signs and ends differently). The
  client starts it with `posix_spawn`, without LaunchServices, as its
  child with argv `[<path>, 3]` and an empty environment: the channel on
  descriptor 3, on descriptor 2 the client's stderr when that is not a
  terminal and the client's relay otherwise (the descriptor 2 paragraph
  below), `/dev/null` for stdin and stdout and no other descriptor
  (`POSIX_SPAWN_CLOEXEC_DEFAULT`), default signal
  actions, an empty signal mask and no exception ports. It starts it
  suspended (`POSIX_SPAWN_START_SUSPENDED`), with a binary preference for
  the client's own slice, arm64 or x86_64
  (`posix_spawnattr_setbinpref_np`), so a client under Rosetta starts the
  helper's x86_64 slice, and, on macOS 10.14 and later, as its own
  responsible process (`responsibility_spawnattrs_setdisclaim`), so that
  TCC does not count the helper's requests under the client's grants:
  `responsibility_get_pid_responsible_for_pid` of the helper returns the
  helper's own pid. Before it resumes the helper with `SIGCONT`, the
  client checks the dynamic code identity of the suspended child, the
  slice it actually runs (`SecCodeCopyGuestWithAttributes` by its pid,
  then `SecCodeCheckValidity` with one requirement), so a file put at the
  helper's path runs no code of its own unless it passes. A
  certificate-signed client builds that requirement at each start from
  `identifier "com.tdesktop.Telegram.WidgetHelper" and certificate leaf =
  H"<SHA-1 of the client's own leaf certificate>"`, read from the running
  client after it validated itself dynamically against its own
  designated requirement (`SecCodeCopyDesignatedRequirement`, then
  `SecCodeCheckValidity`), which binds the reading to the running image,
  so no build records a signer. Before the spawn it then checks the
  helper file statically, slice by slice (`SignerRequirement()` and
  `CheckSlice()` in `platform/mac/widget_helper_mac.mm`): for arm64 and
  for x86_64 it opens that slice
  (`SecStaticCodeCreateWithPathAndAttributes` with
  `kSecCodeAttributeArchitecture`; only a slice the file lacks,
  `errSecCSBadObjectFormat` from that call, is skipped), validates it
  against the identifier-and-leaf text (`SecStaticCodeCheckValidity`)
  and reads, from the same static code, its signed entitlement
  dictionary and its CDHash (`kSecCodeInfoEntitlementsDict`,
  `kSecCodeInfoUnique`). That dictionary, an empty one when the slice
  has none, must equal the build's `kEntitlements` (below) with every
  value's type: the client compares the two as CF's XML serializations,
  which keep the types and sort the keys, since `CFEqual` takes a boolean
  and the number 1 for equal. Every present slice is checked, the one
  the client does not run too, for the reason of **The Mac App Store
  service** below. The requirement of the suspended child is then
  `<identifier-and-leaf> and (cdhash H"<arm64>" or cdhash H"<x86_64>")`
  with the CDHash of each checked slice, one clause for a thin helper,
  so a file replaced after that reading fails the dynamic check. An ad
  hoc client, and a genuinely unsigned one (no signature, and no valid
  status from the kernel), requires `cdhash H"<digest>"` instead: the
  CDHash its build recorded for that slice in the generated
  `widget_helper_identity.h`, which
  `Telegram/cmake/widget_helper_identity.py` writes from the helper
  target's final file once it has checked that file's entitlements, so
  such a client reads none at the start. A mismatch, or a
  check that cannot complete, fails the start: the client kills the
  suspended child, or spawns none when its own identity or the static
  slice check is what failed (**Runtime log and deaths** below).
  The helper moves to the working directory `/` itself, in
  `WidgetHelper::Adopt()` before it adopts the channel. It has no Qt,
  lib_base, crl or framework, its Debug build neither: it is built from its
  own `_other/widget_helper*` sources, the POSIX stream and
  widget-sandbox's platform answers (`lib_widget_sandbox_platform`) over
  twidget's Rust serving, the engine, validator, served host and renderer
  with the helper's half of the ops tap in a Debug build
  ([test_widget_helper_tap](test_widget_helper_tap.md)), built by cargo for
  each architecture and joined by `lipo`, without Telegram's precompiled
  header and before Telegram. The C++ helper before it linked only
  libSystem and libc++; the Rust one has not been built on a Mac yet
  (twidget's `rust/PLAN.md`, stages H3 and H5).
  Only a Debug probe value adds arguments after the `3`: the mode, the
  client's working directory and Application Support path, as
  [test_widget_helper_probe](test_widget_helper_probe.md) describes; the
  probe `dlopen`s its frameworks only after its listing, and its serving
  variant takes that listing while the helper renders. A Release
  `TelegramWidgetHelper` accepts only the token: with any other argument
  it prints `Widget helper: unexpected arguments` and exits 1, and with
  another token, a descriptor 3 that is no socket or a failed `chdir` it
  prints `Widget helper: the channel could not be adopted` and exits 1,
  both before it reads the channel. Its build string is the number in
  `Telegram/build/version`, which equals the client's `AppVersion`; a
  helper of another version refuses the opening with `build "<client's>",
  this end's is "<helper's>"`. The helper target strips its file outside
  Debug and then signs it ad hoc with the hardened runtime, the
  identifier `com.tdesktop.Telegram.WidgetHelper` and
  `Telegram/Telegram/TelegramWidgetHelperDirect.entitlements`, in Debug
  and Release alike, before its CDHash is recorded; Xcode never signs
  that target (`CODE_SIGNING_ALLOWED NO`). That file is the helper's
  whole entitlement set, the Enhanced Security policy and nothing else:
  `com.apple.security.hardened-process` and its `.checked-allocations`,
  `.dyld-ro` and `.hardened-heap` as boolean true, and its
  `.enhanced-security-version-string` and
  `.platform-restrictions-string` as the string `2`, so the helper has
  no file, network, device or keychain entitlement. An edit of that
  file relinks, re-signs and re-pins the helper (`LINK_DEPENDS` under
  Ninja and Makefiles, the POST_BUILD that runs on every Xcode build).
  `widget_helper_identity.py` refuses a slice without the `runtime` flag
  or whose entitlements differ from the file's, types included
  (`check_slice()`: each value compared as `plistlib.dumps` output, the
  error naming each missing, unexpected or differently typed key, such
  as `com.apple.security.hardened-process is 1 (int) instead of true`),
  and writes the file's dictionary into the header as `kEntitlements`,
  its `plistlib.dumps(..., sort_keys=True)` XML as adjacent C string
  literals, one per line, for the start's check above. Ad hoc Xcode and
  Ninja builds, `build_mac.py`'s included, stage that file unchanged
  (`build_mac.py`'s universal app merges the two slices with `lipo`,
  which keeps each slice byte for byte), so each staged slice has the
  recorded CDHash, and `codesign --verify --deep --strict` of the Xcode
  app and of `build_mac.py`'s universal app passes. Release and
  certificate signing go through `Telegram/build/sign_mac.py`, which
  `build.sh`, the canary workflow, `build_mac.py`'s Debug app and a
  POST_BUILD step of Telegram's Xcode target call, the Mac App Store
  build's included. Each helper form has its file: the helper
  `TelegramWidgetHelperDirect.entitlements`, the Mac App Store service
  `TelegramWidgetHelper.entitlements`. For a certificate identity it
  first checks each staged form, before it signs anything with that
  identity (`widget_helper_identity.check_staged()`): each slice's
  entitlements must equal the form's file, so a staged extra grant
  fails with `macOS signing failed: <path>: the <arch> slice's
  entitlements differ from <file>: <differences>` and never receives
  the certificate. It then signs the other nested code with the
  caller's identity, options and entitlements, except the Mac App Store
  service and any Mach-O inside a `Resources` folder, and re-signs each
  helper form with that identity, the hardened runtime, its preserved
  identifier (`--preserve-metadata=identifier`) and its form's file as
  `--entitlements`; an ad hoc identity leaves the helper as built.
  After that last helper write every signing, nested-only and full, ad
  hoc included, checks each staged form (`check_helpers()`, through
  `widget_helper_identity.check()`): each slice needs the `runtime` flag
  and exactly its form's entitlements, under a certificate or an ad hoc
  signature, and then the form's own `codesign --verify
  --all-architectures --strict` must pass. It prints `Widget helper
  checked: Contents/Helpers/TelegramWidgetHelper (<slices>)` per form
  and otherwise fails, exit 1, with `macOS signing failed: <path>: the
  <arch> slice's entitlements differ from
  TelegramWidgetHelperDirect.entitlements: <differences>`, `macOS signing
  failed: <path>: the <arch> slice has no hardened runtime flag`,
  `macOS signing failed: <path>: codesign --verify --all-architectures
  --strict <path> failed: <codesign's error>` for a helper changed after
  its signature (a `strip` of a signed, unstripped helper) or `macOS
  signing failed: <bundle> has no widget helper`. Every signing, before
  it signs any nested code, and both checks first read each present
  form's path as CMake stages it without following links: `Contents`
  and `Contents/Helpers` must be directories and the helper a regular
  file; for the service `Contents`, `Contents/XPCServices`, the `.xpc`
  folder, its `Contents`, `Contents/MacOS` and `Contents/_CodeSignature`
  directories, and its `Contents/Info.plist`, the
  `Contents/MacOS/<CFBundleExecutable>` that plist names and
  `Contents/_CodeSignature/CodeResources` regular files
  (`check_layout()`). A link on that path, even to another folder of
  the app, which the generic signing would reach under its other name
  and Packer would skip, fails with `macOS signing failed: <path> is a
  link`, another kind of file with `<path> is not a directory` or
  `<path> is not a regular file`, a service plist without a plain
  executable name with `<plist> has no valid CFBundleExecutable` and a
  missing entry with Python's `No such file or directory` error, before
  any signature is read or written; links elsewhere in the app stay
  allowed. Each other file of the service bundle must then resolve
  inside the checked bundle: a link out of it fails with `macOS signing
  failed: <path> resolves outside <bundle>`. A full signing
  (`build.sh`'s, the canary's signed loop, `build_mac.py`'s Debug app)
  then signs the app without `--deep` and
  ends with its `codesign --verify --deep --strict`, which alone would
  pass another entitlement set: the app's seal records nested code by
  its designated requirement, not its CDHash, so a same-team re-sign
  with other entitlements keeps the app valid. The Xcode POST_BUILD
  (`--xcode`) signs and checks only the nested code when Xcode allows
  code signing, and Xcode signs the app after it; otherwise it leaves
  the nested code as built and checks nothing. `sign_mac.py --check
  <bundle>` runs only the final check and signs nothing, and
  `sign_mac.py --check-layout <bundle>` only the reading of each present
  form's path above, with its errors, and reads no signature. `build.sh`
  runs `--check` on a request-UUID resume (`request_uuid <id>` with an id
  other than `go`, which does not sign that bundle again) on
  `$ReleasePath/$BundleName` and on the bundle inside the reused setup
  dmg, attached read-only at a temporary mount point, before
  `Beginning notarization process.`. A
  failure stops the script with `Checking the widget helper in <name>
  failed!`, after `TelegramWidgetHelper not found in <bundle>!` for a
  bundle without the helper or `<bundle> in <setup file> is a link!`
  for an archived bundle that is a link, and a missing or unreadable
  dmg stops it with `<setup file> not found or not readable!`. The dmg
  check runs `hdiutil attach` and `hdiutil detach` in a process group of
  their own (a `python3` that calls `os.setpgrp()` and then executes
  `hdiutil`), so a `HUP`, `INT` or `TERM` sent to the build's process
  group or to the checking subshell lets them finish: the subshell only
  records the signal (a group signal also ends a running `sign_mac.py
  --check`), skips any inspection not yet started, then detaches
  whatever is mounted at its mount point and removes that folder once,
  and fails, for a signal that arrives during that detach too. The run
  doing the check takes such a signal, or one sent to it alone, only
  once the subshell has ended, and then stops with `Interrupted while
  checking <setup file>!`, so a universal run releases its build lock
  only after that detach, whether the check was its own or a thin
  run's. A
  universal run (`build.sh` without an architecture) packs both thin
  updates itself on every route. Right after taking its build lock it
  stops with `Update files already exist in <release folder> (arm64
  <file>, x86_64 <file>), set them aside before rerunning!`, naming
  each architecture whose final update name
  (`td-update-mac-arm-<version>` or `td-update-mac-x64-<version>`, with
  `-beta` on the beta channel) is taken by a file, folder or link,
  before any thin run, signing, submission or deploy output. It then
  hashes `TelegramWidgetHelperDirect.entitlements` into memory and
  stops with `TelegramWidgetHelperDirect.entitlements could not be
  read!` when it cannot. A fresh run and `request_uuid go` run both thin
  runs fresh, `request_uuid <id>` runs both fresh from the staged
  universal app before its own resume checks, `request_uuid_arm64 <id>`
  resumes the arm64 run with that id and runs the x86_64 one fresh, and
  `request_uuid_x86_64 <id>` runs the arm64 one fresh and then resumes
  the x86_64 one; a failed thin run stops the universal run, so the
  deploy folder receives only the two updates this run's thin runs
  packed. It starts each thin run as `<script folder>/build.sh <arch>
  request_uuid <id>`, the id empty for a fresh one, so a relative or an
  absolute path to the script reaches them, and keeps its build lock
  until that thin run has ended: a `HUP`, `INT` or `TERM` sent to the
  universal run during a thin run takes effect once the thin run has
  finished, or has ended on that signal too, and then stops it with
  `Interrupted while preparing the single updates!` before the next
  thin run, signing, submission or deploy output, its exit releasing
  the lock, so a rerun cannot share the release folder with a thin run
  still at work. Before it creates the deploy folder the universal run
  hashes the policy file again and stops with that read error, or with
  `TelegramWidgetHelperDirect.entitlements changed during the build!`
  when the two hashes differ, so a persistent edit during a thin run,
  the universal signing or check or a notarization wait ships nothing.
  A thin run refuses an existing update of its own with `Update file
  for version <version> already exists!`, every run refuses an existing
  deploy folder of its version, and a thin run started on its own
  (`build.sh arm64`, `build.sh x86_64 request_uuid <id>`) packs only its
  update. A thin run copies the staged universal app to
  `Telegram.<arch>.app` with `cp -R`, which copies links as links, and
  runs `--check-layout` on the copy before `lipo -thin` writes any slice
  into it, so a link on the helper's path, such as a linked
  `Contents/Helpers`, stops it with `Checking the widget helper in
  Telegram.<arch>.app failed!` before a thin slice replaces a file that
  link reaches. A thin run keeps its setup dmg through its checks,
  notarization, stapling and packing and removes it (`Setup file
  <file> removed.`) only after moving its finished update into the
  release folder: a thin `request_uuid <id>` run stopped before that
  move finds its dmg again when the same command runs again, and one
  stopped after it leaves a complete update for the existing-update
  guard. The canary
  workflow's macOS job runs `--check` on `Telegram.x86_64.app`,
  `Telegram.arm64.app` and `Telegram.app` when no signing certificate
  leaves them unsigned. The release strip of
  `build.sh` and the canary leaves the already stripped and signed helper
  byte for byte unchanged (measured with CLT 26.6's `strip`, which still
  warns that its changes will invalidate the code signature). The pin
  refuses a helper whose CDHash differs from the recorded one, such as
  another build's (an identical ad hoc re-sign can keep the CDHash),
  while the signer rule accepts a helper with the helper identifier that
  the client's own certificate signed, a re-signed release's included,
  only when each of its slices carries exactly this build's
  entitlements, and refuses before the spawn an ad hoc one, the build's
  own too, and a same-leaf one with another entitlement set (an added
  key, the integer 1 for a boolean or a missing key, on either slice). A
  copy signed with a certificate for a test needs `sign_mac.py` too:
  `codesign --force --deep` of the app gives the helper Telegram's
  entitlements and another identifier, which the start refuses. No
  helper build carries `get-task-allow`, so a
  debugger cannot attach to the helper; observe it from the client and
  through its stderr. A running client refuses every later start once its
  own executable or its signed `Info.plist` changed on disk, as a
  rebuild, a re-sign or a reinstall under it can do: start a fresh client
  after each. A client whose code a debugger modified may lose its valid
  status and refuse the same way (not measured). To test a refusal,
  replace only `Contents/Helpers/TelegramWidgetHelper` in a copy of the
  bundle: the client's own identity stays valid, and the check reads the
  replacement.
  On Linux it is `TelegramWidgetHelper` as well, built from the same
  POSIX stream and `_other/widget_helper.cpp`, with
  `_other/widget_helper_linux.cpp`, the Linux lockdown and the descriptor
  closing (`platform/linux/widget_{lockdown,descriptors}_linux.cpp`;
  Telegram no longer links the lockdown) in place of the macOS sources,
  without Telegram's precompiled header and before Telegram. As on macOS
  its engine, validator, served host and renderer are twidget's Rust
  serving, with the helper's half of the ops tap in its Debug build
  ([test_widget_helper_tap](test_widget_helper_tap.md)), and it reaches
  the helper as one static library: cargo builds `twidget_helper_ffi` for
  the host, always `--release`, into the build tree's
  `widget_helper_rust/libtwidget_helper_ffi.a`
  (`Telegram/cmake/lib_widget_helper_rust.cmake`,
  `build_widget_helper_rust.cmake`), fetching its crates into
  `widget_helper_rust/cargo` unless the toolchain is `prepare.py`'s, and
  Telegram links no copy. It links
  only glibc's objects, libm and the C++ runtime, its Debug build too,
  as the C++ helper before it did:
  the Docker build links the C++ runtime statically, so its NEEDED are
  `libc.so.6`, `libm.so.6`, `libpthread.so.0`, `libdl.so.2` and the
  loader, and a build that linked the C++ runtime dynamically added
  `libstdc++.so.6` and `libgcc_s.so.1`. Its file is
  `TelegramHelpers/TelegramWidgetHelper` next to the client's binary
  (`out/Debug/TelegramHelpers/TelegramWidgetHelper` in the Docker
  tree, the tarball layout) or, in a package-managed build,
  `<libexecdir>/TelegramDesktop/TelegramWidgetHelper`
  (`/usr/libexec/TelegramDesktop/` with the prefix `/usr`,
  `/app/libexec/TelegramDesktop/` inside Flatpak,
  `$SNAP/usr/libexec/TelegramDesktop/` in the Snap). The client finds it
  as `cExeDir()`, the canonical directory of its own binary that it
  resolved once at start from `/proc/<pid>/exe`, joined with the relative
  path CMake fixed at configure time (`TDESKTOP_WIDGET_HELPER_PATH`, a
  definition on `widget_helper_linux.cpp` only:
  `TelegramHelpers/TelegramWidgetHelper`, or the path from the bindir to
  the libexec folder, `../libexec/TelegramDesktop/TelegramWidgetHelper`
  with the default directories) and cleaned lexically (`QDir::cleanPath`).
  So a binary started through a symlink or renamed, as the Snap's
  `telegram-desktop` is, finds it. The path does not follow a folder
  moved under a running Telegram: from a moved tarball folder that
  Telegram keeps starting the file it held at launch, if it holds one
  (below; a reading of the code, not run), and a package-managed build,
  which looks the old path up at each start, misses its helper. An
  absolute `CMAKE_INSTALL_LIBEXECDIR`, such as `/usr/lib/x86_64-linux-gnu`,
  still gives a path relative to the configured prefix
  (`../lib/x86_64-linux-gnu/TelegramDesktop/TelegramWidgetHelper`), so
  `cmake --install --prefix <another prefix>` puts the helper into that
  absolute directory while Telegram moves, and that Telegram misses it;
  package tooling installs at the configured prefix, staged with
  `DESTDIR`, where the path holds. A package-managed build looks the
  file up at every start. In the `TelegramHelpers` layout, that of every
  build but a package-managed one, the client takes the file once, at
  launch, where it can start a helper (outside Flatpak, on x86_64 and
  aarch64): `Platform::WidgetHelper::Hold()`, the first call of
  `Platform::start()` and so before any widget, opens the path
  (`O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK`, so a symlink there is
  followed), moves the descriptor to 10 or above, close-on-exec, and
  keeps it for the rest of the process if it is a regular file (`fstat`,
  `S_ISREG`). It hashes nothing then and never closes that descriptor,
  so a file unlinked under it stays on disk until that Telegram exits.
  Before each start the client takes a duplicate of it
  (`F_DUPFD_CLOEXEC`, 10 or above), requires a regular file again and
  hashes the file with SHA-256 from its first byte (`pread`, which never
  moves the offset the duplicates share), which must equal, in lowercase
  hex, the single digest of the build's generated
  `widget_helper_identity.h`. When that open, the move or `fstat` fails,
  or the file is not a regular one, nothing is held, the main log has
  `Tgw: The widget helper is not held, each start opens it: <error>.` at
  launch (no start failure; for a missing helper `<error>` is `open
  failed: No such file or directory`), and each start opens the path
  itself with the same flags and checks the file the same way; a file a
  start opened is never kept.
  `Telegram/cmake/widget_helper_identity.py --kind sha256` writes that
  header per configuration from the helper target's final file
  (`out/Telegram/gen/widget_helper_identity/Debug/widget_helper_identity.h`
  in the Docker tree), and only these builds compile
  `widget_helper_linux.cpp` with `TDESKTOP_WIDGET_HELPER_PINNED`.
  Outside Debug that final file is already stripped: the helper target's
  own POST_BUILD runs `CMAKE_STRIP` on it with no options (GNU strip's
  `--strip-all`: no `.debug_*` section, no symbol table) before the
  generator reads it, as on macOS, and a configure of these builds
  without a strip tool fails with `TelegramWidgetHelper is stripped
  outside Debug, but no strip tool was found (CMAKE_STRIP).`. A Debug
  helper keeps its debug sections, and a package-managed build's helper
  is left as its build links it. The cloned child then executes the
  checked descriptor, never the path: it places it at descriptor 4 with
  close-on-exec (`dup3`), closes everything from 5, its copy of the held
  descriptor included, and calls `fexecve(4, ...)`, which glibc issues
  as `execveat(4, "", ..., AT_EMPTY_PATH)`, so a file put at the path
  after the check never runs, nor, in a Telegram that holds the file,
  one put there after launch, and the helper starts without
  descriptor 4. The main log has `Tgw: The widget helper passed the
  build identity check (SHA-256 <digest>).` before such a start. Any
  other bytes, another build's helper of the same version included,
  fail the start (**Runtime log and deaths** below), and so does
  anything that rewrites the file after its build, such as
  `cmake --install --strip` of such a build (a reading of the install
  script, not run). A held file rewritten in place fails the next start
  the same way. Replacing the file at the path does not reach a Telegram
  that holds another one: a Telegram whose held file fails the check
  (one launched while another instance's update was still being copied
  can hold the other version's helper) keeps failing until the failure
  limit stops its starts, and only a restart takes the file now at the
  path. A package-managed build pins and checks nothing: the main log
  has `Tgw: The widget helper is not pinned in a package-managed
  build.` before each start, on both spawn paths, and its cloned child
  executes the file by path (`execve`), so the build string is its only
  check. After an upgrade replaced the file under a running Telegram, in
  the `TelegramHelpers` layout that Telegram keeps starting the file it
  held (`/proc/<pid>/exe` of its helper then reads `<path> (deleted)`),
  and one that holds none checks the new file; in a package-managed
  build it runs the new file, whose build string differs, so the opening
  is refused until Telegram restarts. The client starts it as
  `[<path>, 3]`, without `-testagent`; a Debug probe value adds the same
  arguments after the `3` as on macOS, with the client's
  `QStandardPaths::GenericDataLocation` as the data path. When
  `cExeDir()` is empty nothing is held and both spawn paths fail with
  `the executable path is unknown`.
  The client clones it from a thread of its own that lives as long as the
  process, with only descriptors 0 to 3 open (0 and 1 `/dev/null`, 2 the
  helper's stderr as below, 3 the channel), in the working directory `/`,
  with every signal at its default action, an empty signal mask and the
  client's environment, and the cloned child executes the checked
  descriptor or, in a package-managed build, the helper's file. The same
  clone gives it its own user, PID, network and mount namespaces: inside
  them it is PID 1 and root of a user namespace that maps it to the
  client's user, with only a loopback interface, while the main log and
  `ps` show its outer pid. Where the system refuses that clone, the main
  log has `Tgw: The widget helper starts without namespaces: clone
  failed: <reason>.` and the client clones it again as a plain child.
  Inside Flatpak the Flatpak portal starts it instead, through
  `Spawn` with the flags 28 (`SANDBOX`, `NO_NETWORK` and `WATCH_BUS`): the
  same argv, `/app/libexec/TelegramDesktop/TelegramWidgetHelper 3` in an
  `/app` install, the working directory `/`, the same four
  descriptors and an empty environment table, so the helper gets the
  environment of the portal's sandbox, not the client's. That helper is
  not the client's child, and the pid in the main log is the portal's, not
  one in the client's PID namespace. Only a package-managed build, such as
  Flathub's, starts it there: the portal executes a path, so a build in
  the `TelegramHelpers` layout fails every start inside Flatpak with `the
  widget helper's build identity cannot be checked through the Flatpak
  portal`, before any portal call (a reading of the code, not run).
  On both Linux spawn paths, for the direct-download macOS helper above
  and for **The Mac App Store service** below, the helper's descriptor 2
  follows the client's stderr at each start. When that is not a terminal
  (`isatty`), as under `test-run`, whose `app_stderr.txt` is a file, it is
  a duplicate of the client's stderr, and the helper's lines land there
  unchanged. When it is a terminal, descriptor 2 is the helper's end of a
  one-way `AF_UNIX` stream socket (on Linux `/proc/<pid>/fd/2` reads
  `socket:[<inode>]`, on macOS `lsof -p <pid>` shows `unix` at fd 2),
  never the terminal: a thread of the client reads the other end and
  writes each line to the client's stderr with one `write`, every byte
  outside printable ASCII (0x20 to 0x7e) escaped as `\xNN` in lowercase
  hex and `\` as `\\`. A line longer than 8192 bytes keeps its first 8192
  and ends with ` [cut]`, a last line without a newline is relayed at the
  end, and a helper's bytes past its first 32768 are dropped, with one
  line `[widget helper output over 32768 bytes dropped]` in their place.
  The Mac App Store client sends its service that duplicate or that end
  as the channel message's `stderr`.
  The relay ends with its helper, before the main log's `ended` line
  below. On macOS, after a helper that was reaped (for the Mac App Store
  service, one whose `NOTE_EXIT` the client saw), the client first waits
  up to 1000 ms for the relay to end on its own, at the end of the stream
  once every queued byte was relayed, and only then shuts the relay's end
  and joins its thread, still before the `ended` line: macOS discards the
  bytes still queued on a socket end shut for reading, which could lose the
  helper's last line, written right before it exits. A helper that was
  not reaped ends its relay at once, as on Linux. A terminal that stops
  accepting writes blocks only that thread, and with it the reap of its
  helper (or the return of a Mac App Store start that failed after a
  service wrote to its relay); on macOS the join after the wait blocks
  the same way, so 1000 ms limits that wait, not the reap.
  At its entry, before it adopts the channel, the helper's
  `WidgetHelper::Adopt()` (`_other/widget_helper_linux.cpp`) accepts only
  the token `3`, names its process `TelegramWidgetHelper` with
  `PR_SET_NAME`, which the kernel keeps as `TelegramWidgetH`, the name an
  exec by path gives (an exec through a descriptor names it `4`, as
  kernel 6.6 does), limits glibc's malloc to one arena
  (`mallopt(M_ARENA_MAX, 1)`) and makes the preparation of **The Linux
  lockdown** below; it makes no `chdir`. A
  Release helper refuses as on macOS: `Widget helper: unexpected
  arguments` for any argument after the token, `Widget helper: the
  channel could not be adopted` for another token or a descriptor 3 that
  is no socket. The
  Windows helper is below. It has no harness, log or account data:
  everything a seam reads stays in the client. It is stopped 30 s after
  its count of open runtimes last fell to zero, and never sooner than 30 s
  after it last handed a widget its channel; the next widget starts a
  fresh helper with a new pid. The main log has `Tgw: Widget helper <pid>
  started.`, then `Tgw: Widget helper <pid> stream ended: <reason>.` and
  `Tgw: Widget helper <pid> ended, <status>.`. On macOS and Linux the
  status is `exit N` or `signal N`, with `sent SIGKILL after N ms` added
  when it had to be killed. Inside Flatpak the client takes that status
  from the portal's `SpawnExited` signal and kills through the portal's
  `SpawnSignal`, sent to the process group of the sandbox the portal
  started; a failed kill adds `SpawnSignal failed: <error>` in place of
  `sent SIGKILL after N ms`. On Windows it is `exit N`, or `exit 0x<code>`
  in uppercase hexadecimal from `0xC0000000` up, with `terminated after N
  ms` added when its job had to be killed, which ends it with `exit 1067`
  (`ERROR_PROCESS_ABORTED`): `exit 1067, terminated after 1000 ms` after
  the 1 s wait for its own exit. A helper that does not end even then
  reads `not reaped after N ms` before that addition. A helper that was
  never put into its job (a failed assignment on the AppContainer and
  Windows 7 tiers) is killed through its process handle instead, with
  `TerminateProcess failed: <reason>` in place of the addition when that
  fails, and on the Windows 7 tier a failed close of its desktop adds
  `, CloseDesktop failed: <reason>`. A stop by the client
  reads `the host stopped the stream`; that stop drops unsent frames, so
  the helper can exit 1 after it, with a `Widget helper: <reason>` line in
  `app_stderr.txt`. That exit is no helper failure: after a stop by the
  client only whether the helper was reaped counts (**Failure limit**
  below).
- **The Mac App Store service.** In the Mac App Store build
  (`OS_MAC_STORE`, `Telegram Lite.app`) the helper is the same
  `TelegramWidgetHelper` code packaged as the XPC service
  `Contents/XPCServices/TelegramWidgetHelper.xpc`, executable
  `Contents/MacOS/TelegramWidgetHelper`, with `CFBundlePackageType` `XPC!`
  and `XPCService` `ServiceType` `Application`
  (`Telegram/TelegramWidgetHelper.plist`). Its identifier is the main
  bundle's plus `.WidgetHelper` (`org.telegram.desktop.WidgetHelper` as
  built) and its entitlements those of
  `Telegram/Telegram/TelegramWidgetHelper.entitlements`: the App Sandbox,
  the one key it shares with Telegram Lite, and the direct helper's
  Enhanced Security set (**The shared helper** above), so it runs in
  its own empty container with no network, file, device or keychain
  entitlement. The target's POST_BUILD strips it outside Debug and signs
  the bundle ad hoc with the hardened runtime, that identifier and that
  file, in Debug and Release alike; the generated pin is that
  signature's CDHash (`widget_helper_identity.py --entitlements`
  requires each slice's `runtime` flag and its entitlements equal to
  the file's, types included, and writes them as `kEntitlements`), and
  `TelegramWidgetHelper_bundle` copies the bundle's `Info.plist`,
  executable and `CodeResources` into the app. An edit of the plist
  template or the entitlements relinks, re-signs and re-pins it. It links
  only libSystem and libc++. `sign_mac.py` leaves it as built for an ad
  hoc identity, since its build signature is the pin, and for a
  certificate identity first compares the staged service's entitlements
  with `TelegramWidgetHelper.entitlements`, then re-signs it with the
  hardened runtime, `--preserve-metadata=identifier` and that file as
  `--entitlements` (the identifier alone would drop the App Sandbox and
  the Enhanced Security keys), as it does the direct helper. It also
  skips every Mach-O inside a
  `Resources` folder, so Breakpad's `Resources/breakpadUtilities.dylib`
  keeps what each caller's own line gives it. `build.sh`'s macstore branch signs that
  dylib, then the app through `sign_mac.py` with the Mac App Store
  identity, `Telegram Lite.entitlements`, a timestamp and the hardened
  runtime; `build_mac.py`'s Store Debug signs the dylib ad hoc and calls
  `sign_mac.sign(bundle, '-')`; the Xcode Store target runs the same
  `sign_mac.py --xcode` POST_BUILD as other Xcode builds, without
  `--deep`. Each of these, the Xcode one when Xcode allows code signing,
  runs `sign_mac.sign()`'s check of the service against
  `TelegramWidgetHelper.entitlements`, with its own strict verify,
  after the service's last signature; `build.sh`'s and
  `build_mac.py`'s lines then sign the app and end with its `codesign
  --verify --deep --strict` (**The shared helper** above), so
  `build.sh`'s `productbuild` packages only a service that passed both.
  The Xcode POST_BUILD signs and checks only the nested code, and Xcode
  signs the app after it without a deep strict verify, so verify an
  Xcode product's app with `codesign --verify --deep --strict`
  yourself. `codesign --force
  --deep` keeps the service's identifier and gives it that command's
  entitlements: with
  `Telegram Lite.entitlements` (`build.sh`'s old line) Telegram Lite's
  nine, the network, camera, microphone, location and user file grants
  among them; with none (`build_mac.py`'s old ad hoc line) none, so the
  service loses the App Sandbox and its pinned CDHash. Neither set is
  the service's file, so `sign_mac.py --check` and a certificate-signed
  client's start refuse such a service.
  Telegram's `Launch()` (`platform/mac/widget_helper_store_mac.mm`) names
  the service after its own `[[NSBundle mainBundle] bundleIdentifier]`
  plus `.WidgetHelper`, so a copy re-identified for a test reaches its
  own service. It builds the requirement as on direct download, before
  it creates the channel or the connection: a certificate-signed client
  checks each present arm64 and x86_64 slice of its
  `Contents/XPCServices/TelegramWidgetHelper.xpc` statically against
  `identifier "<main bundle id>.WidgetHelper" and certificate leaf =
  H"<SHA-1 of the client's own leaf>"`, compares the slice's signed
  entitlements with the build's `kEntitlements` (the service's file, the
  App Sandbox included) and requires `<identifier-and-leaf> and (cdhash
  H"<arm64>" or cdhash H"<x86_64>")`; an ad hoc one requires `cdhash
  H"<pin>"`. The certificate form admits every checked slice because
  launchd starts the service's native slice even for a client under
  Rosetta, so binding the client's own slice would refuse its own
  service. The comparison is exact after Apple's Mac App Store
  re-signing too, so a distributed service whose entitlement dictionary
  differed in any key or type would refuse every widget start of that
  build; no Apple-signed build has been checked. The client sets
  the requirement with
  `xpc_connection_set_peer_code_signing_requirement`. XPC checks it only
  on replies and delivers an outgoing message before it checks the peer,
  so the first message, the hello, carries no descriptor. Only after XPC
  verified its empty reply does the client read the pid
  (`xpc_connection_get_pid`), watch it for `NOTE_EXIT` and send the
  channel message: `channel`, the child end of the socketpair, whose own
  copy it then closes, `stderr`, a duplicate of its descriptor 2 when
  that is no terminal and the helper's end of its relay otherwise (**The
  shared helper** above), whose own copy it closes when `Launch()`
  returns, and, in a Debug probe launch, `arguments`. After that reply it
  also refuses when the watched instance ended or the connection's pid
  changed (`the widget helper ended before it took the channel`), then
  sends the keep request, which the service holds unanswered until it
  exits: the pending reply keeps the service at Telegram's priority (31
  while serving; an XPC service with no request pending ran at 4) and out
  of the system's idle exit. Both replies share one 15 s deadline
  (`kStartWait`).
  The service runs `xpc_main()`: it accepts one peer connection and
  cancels any other, answers the hello, puts `stderr` on descriptor 2 and
  `channel` on 3, refuses a second `channel`, and starts one serve thread
  (8 MiB stack) that runs the direct helper's argument check,
  `WidgetHelper::Adopt()` (`chdir("/")`) and the direct helper's
  `WidgetHelper::Serve()` with the lockdown, so it hosts whole widgets
  as the direct helper does (**Hosted widgets on macOS,
  Linux and Windows** above); it
  `_Exit`s on any error event of its peer connection. It is a child of
  launchd with its own container as `HOME`, launchd's `/dev/null` on 0
  and 1, on 2 Telegram's stderr when that is no terminal and the relay's
  end otherwise, the channel on 3, and its responsible process is
  Telegram, so TCC would count its requests under Telegram's grants;
  after its lockdown it can look up no Mach service, tccd and the window
  server included, and keeps only the services it already holds
  ([test_widget_helper_probe](test_widget_helper_probe.md#mac-app-store)
  lists what it can still reach). A start took about 20 ms, 90 ms when it
  created the service's container.
  A start failure reads `the widget helper could not start: <error>`, the
  errors among them `stderr is not open`, `the bundle identifier of this
  build is unknown`, `the widget helper path is unknown`,
  the client's own identity errors and the certificate-signed client's
  static slice refusals of **Runtime log and deaths** (`the widget helper
  failed the signer check: the <arch> slice's entitlements differ from
  this build's` and the others there), which come before the channel and
  the connection exist and have no cleanup,
  `xpc_connection_set_peer_code_signing_requirement failed: <error>`, `the
  widget helper failed the signer check: <XPC error>` or the same with
  `the build identity check` (`Peer Forbidden` on macOS 27.0.1: XPC
  refused the peer requirement on the hello's reply, as it does for a
  service replaced after the static reading), `the
  widget helper service <name> is unavailable (<XPC error>)` (a bundle
  without the service under an ad hoc client: `Connection invalid
  (Connection init failed at lookup with error 3 - No such process)`;
  a certificate-signed client's static check fails first, with its
  `SecStaticCodeCreateWithPathAndAttributes` error), `the widget helper
  ended before it took the channel`, `the widget helper did not answer within
  15000 ms` or `the widget helper refused the channel: <reason>`; once
  the client watches a pid, after the hello's reply, the cleanup follows
  in parentheses, read from the service's `NOTE_EXIT` as in the `ended`
  line below, not from a wait status. As for
  the direct-download helper, when Telegram's stderr is a terminal and
  the relay of **The shared helper** cannot be made, the start fails
  before the client creates its connection, with `socketpair failed:
  <reason>`, `shutdown failed: <reason>` or `fcntl failed: <reason>`, or
  with a setup error the relay's socket shares with the channel
  (`getsockopt failed: <reason>`, `setsockopt failed: <reason>`); the
  terminal is never handed to the service instead. `fcntl failed:
  <reason>` is also the error when the duplicate of a stderr that is no
  terminal cannot be made.
  launchd, not Telegram, owns the process, and the App-Sandboxed client
  cannot signal it (`kill` gives `EPERM`), so `complete` waits for
  `NOTE_EXIT` and otherwise cancels the connection, whose error event ends
  a responsive service. The `ended` line's status is `ended`, `ended after its
  connection was canceled after N ms` (a serve thread stuck while the
  XPC queue still answers ends this way), or `did not end within N ms of
  its connection being canceled`, which is not reaped and latches `the
  widget helper was not reaped` for the process. A `SIGSTOP`ped service
  reads so, and launchd ends it about 5 s after Telegram exits. launchd
  defers the next start in the same Telegram process when the previous
  instance lived less than 10 s: the hello's reply then comes about
  10.04 s after the request, not 10 s after that spawn, about 5 s inside
  `kStartWait`. After an instance that lived longer the next start is
  immediate, even right after its end, and a new Telegram process starts
  at once. The manager releases its mutex during the
  start (**Timing** below). The idle stop, the crash counting and the
  failure limit are the manager's, as on direct download; after a kill
  the `stream ended` line reads `the engine process closed the stream`.
- **Mac App Store test runs.** A Store leg runs a copy with a test-only
  identifier, such as `com.tdesktop.tasktest.xpchelper.telegram`, never a
  production one: every App-Sandboxed identifier leaves a container
  (`~/Library/Containers/<id>`) whose folder and
  `.com.apple.containermanagerd.metadata.plist` a shell cannot remove
  (`EPERM`; remove them in Finder), and a production-named one would
  belong to the real Telegram Lite later. Clone the Store product (`cp -c
  -R`), set Telegram's `CFBundleIdentifier` to the test id, remove
  `CFBundleURLTypes` and `CFBundleDocumentTypes` so that the copy never
  becomes the `tg://` handler, set the service's `CFBundleIdentifier` to
  `<test id>.WidgetHelper` and re-sign the service ad hoc with that
  identifier, the hardened runtime and `TelegramWidgetHelper.entitlements`
  (exactly that file: `sign_mac.py` checks it, and the stand-in-signed
  copy compares it at each start),
  then sign the copy with `sign_mac.py --identity <stand-in>
  --entitlements <file> --runtime --keychain <throwaway keychain>`, never
  a real identity or the owner's keychains. The file is `Telegram
  Lite.entitlements` plus test-only keys: a
  `com.apple.security.temporary-exception.files.absolute-path.read-write`
  for the leg root, the only way the App-Sandboxed client reaches the
  portable account, the run directory and copied inputs (keep every input
  under it), and, for any signature without a Team ID (a self-signed
  stand-in or ad hoc), `com.apple.security.cs.disable-library-validation`,
  without which the
  hardened runtime refuses the embedded `Breakpad.framework` (`mapping
  process and mapped file (non-platform) have different Team IDs`); the
  service gets neither. Sign each identifier with one signer only: a
  container keeps its first signer, and another one risks a container
  prompt or refusal during a run, so a leaf refusal and an ad hoc pin
  refusal each take their own identifier. Launch with `workspace.py
  test-run --exe "<copy>/Contents/MacOS/Telegram Lite" --portable-root
  <leg> --run-dir <leg>/run`; inside the App Sandbox `HOME` and the
  single-instance socket are in the container, so `CFFIXED_USER_HOME` is
  not needed. `--env` values reach Telegram only, never the service. A
  shell may not read or list a signed copy's container (macOS App Data
  protection: `Operation not permitted`; `stat` works), so a service-side
  marker leaves through the stderr Telegram passed (`app_stderr.txt`) or,
  from a service refused before it got that stderr, as a darwin
  notification. When Telegram's stderr is a terminal the markers pass its
  relay (**The shared helper** above), escaped and bounded. `test-run`
  gives Telegram a file, so a terminal leg needs a pseudo-terminal whose
  master is read continuously from before the launch until Telegram
  exits: a master nobody reads blocks the relay thread and, through its
  join, the reap of that service. A start after an instance that lived
  under 10 s, such as every relaunch after a probe or a refused service,
  meets launchd's 10 s deferral. At the end unregister the copies
  (`lsregister -u`) and remove `~/Library/Application Scripts/<id>`; the
  containers stay until removed in Finder.
- **The Windows helper.** It is `TelegramWidgetHelper.exe`, a separate
  executable in the `modules\<arch>\` folder next to the client's own,
  `<arch>` being `x86`, `x64` or `arm64` for the client's architecture
  (`build_winarm`, `build_win64` or neither in CMake, `_M_ARM64`,
  `_M_X64` or `_M_IX86` for the client's `kHelperArchitecture`). The
  client takes the directory of the path `GetModuleFileNameW(nullptr)`
  gives and appends that folder and file name. Before it spawns it checks
  the file through the handle it will start: `HoldHelper()`
  (`platform/win/widget_helper_image_win.cpp`) opens the helper with
  `GENERIC_READ` and `FILE_SHARE_READ` (no write or delete sharing) and
  every folder on its path (from the root of its volume or share down to
  the executable folder, then `modules` and `modules\<arch>`)
  with `FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE` and no delete
  sharing, rejects a name-surrogate reparse on any of them, computes the
  Authenticode PE-image digest (the file without its checksum field, its
  certificate-table directory entry and the certificate table, the hash
  that signing does not change) through the held handle with
  System32 `wintrust.dll`'s `CryptCATAdminCalcHashFromFileHandle2`, and
  compares it with the digest pinned into this build. The pin is generated
  from the helper target at build time by `widget_helper_identity.py
  --kind authenticode` into `widget_helper_identity.h` (`kBuiltDigests`),
  so each build's client matches only the helper built with it, signed or
  not. The client keeps every handle held, creates the suspended process
  from the held file's own normalized path, and before it resumes confirms
  the child's image is that same file (its `PROCESS_NAME_NATIVE` name equals
  the held file's and its `FILE_ID_INFO` matches); then it attaches the
  initial token and resumes. On the Windows 7 tier the same pin is
  computed and every identity is read without the Windows 8 functions
  (**The Windows 7 tier** below). A digest mismatch, a file it cannot
  open that way, a failed digest call, a redirected path component or a
  started image
  that is not the held file stops widgets with a diagnostic, and a path
  it cannot build still fails with
  `GetModuleFileNameW failed: <reason>`, `the executable path is too long`
  or `the executable path has no directory`. Telegram's build depends on
  the helper target, which puts the file there. Every Windows package
  ships it, the Store build's AppX package included (**The Windows Store
  build** below): `setup.iss` installs it for every target, ARM64
  included, as
  `{app}\modules\<arch>\TelegramWidgetHelper.exe`; `build.bat` signs it
  right after `Updater.exe`, puts it into the update package as the entry
  `modules/<arch>/TelegramWidgetHelper.exe` and copies it into the
  portable ZIP's `modules\<arch>\`, so the setup, the update package and
  the portable ZIP all carry the signed file; the canary workflow's Windows
  job, an x64 build, signs and verifies it with the other binaries and
  packs it into its update and its portable ZIP as
  `modules/x64/TelegramWidgetHelper.exe`. The uninstaller removes it with
  the rest of `{app}\modules` (its `[UninstallDelete]`), and
  `Telegram.exe -cleanup` deletes it from `modules\x86`, `modules\x64` and
  `modules\arm64` (`DeleteMyModules()` in `specific_win.cpp`, which
  deletes only the files it names and removes only folders left empty). A
  layout without the file, such as an install or a portable copy made
  before the helper shipped, fails every start at the hold with `could
  not hold modules\<arch>\TelegramWidgetHelper.exe: CreateFileW failed:
  <reason>` until an update brings the file; with `modules\x86` present
  and the file missing, x86 reads `could not hold ...: CreateFileW failed:
  The system cannot find the file specified.`. The
  client starts it as `"<path>" <handle>`, where `<handle>`
  is the decimal value of the pipe handle it inherits, without
  `-testagent`, with the system directory as its working directory
  (`C:\WINDOWS\system32` as `GetCurrentDirectoryW` reads it, which an x86
  helper on 64-bit Windows holds open through WOW64 as
  `C:\Windows\SysWOW64`) and with the client's environment except
  `__COMPAT_LAYER`. Only a Debug probe value adds
  arguments after the handle, the same ones as on macOS: the mode, the
  client's working directory and its
  `QStandardPaths::GenericDataLocation`, each quoted for
  `CommandLineToArgvW` when it is empty or holds whitespace or a quote;
  the Windows probe reads the mode and the client's working directory, and
  a fixture launch adds the loopback port, `connect-control` and network
  targets ([test_widget_helper_probe](test_widget_helper_probe.md#windows-arguments)).
  Its stdin and stdout are null;
  its stderr is a copy of the client's when that is a file or a pipe, as
  `app_stderr.txt` is under `test-run`, and none otherwise, and the
  creation's handle list passes it nothing else. The channel is one
  overlapped duplex named pipe with an unpredictable name, one instance,
  remote clients rejected and access for the client's user only; the
  client opens the helper's end itself before the spawn. Both ends give
  each overlapped `ReadFile` and `WriteFile` a byte-count variable of its
  own that lives through the completion, since Windows 7 requires one,
  and take the count from `GetOverlappedResult`
  (`widget_channel_win.cpp`). The client
  creates the helper suspended with `CreateProcessAsUserW` on a lockdown
  primary token derived from its own: Low integrity, no privilege but
  `SeChangeNotifyPrivilege`, the user and every group but the integrity
  label deny-only, and the NULL SID `S-1-0-0` as its only restricting SID.
  Before it resumes the helper it attaches an initial impersonation token
  to the helper's main thread and reads it back: Low as well and
  restricted to the client's user and enabled groups, so that the helper's
  startup can load its DLLs. On the lowbox tiers both are AppContainer
  tokens of the helper's package, and on the two LPAC tiers the creation
  makes the primary a less privileged one (**The Windows lowbox** below);
  the Windows 7 tier keeps both unboxed. What follows is the full tier's
  launch, from Windows 10 1809; each older tier sets the part of it that
  its Windows version documents (**The Windows sandbox tiers** below).
  The creation puts the helper in a new job
  (one active process, kill on job close, death on an unhandled exception
  and the eight UI restrictions) and sets every mitigation the helper
  takes at its creation, from `SandboxPolicies()` in
  `widget_sandbox_win.cpp`, the one table the client's creation and the
  helper's readback share: DEP on x86, bottom-up and forced-relocation
  ASLR (high entropy on x64 and ARM64), extension points disabled, dynamic
  code prohibited, win32k system calls disabled, CET user shadow stacks in
  x64 builds where `IsUserCetAvailableInEnvironment` says the system offers
  them, only Microsoft-signed images, strict handle checks, non-system
  fonts disabled, System32 images preferred, and no child processes (the
  separate child-process attribute). The creation words are
  `0x1001101111010101` (8 bytes) on x86 and `0x1001101111110100
  0x10000000` (16 bytes) on x64 with CET, the set that
  `2026/11/15/move-the-windows-widget-helper-into-a-qt-free-executable-and-run-it-on-x64`
  measured candidate by candidate on the suspended child on x86 and x64
  (its `work/decisions.md`, P7); without CET an x64 creation passes the
  first word alone. The image-load policies that refuse remote and
  Low-labelled images depend on where the user put the files, and CFG and
  strict CET need another binary contract, so none of them is set. A
  creation the system refuses, the job included, is a start failure: no
  helper runs outside its job or without its tier's policies.
  The helper links no Qt, user32 or gdi32: it is built from the shared
  `main()` of `_other/widget_helper.cpp`, `_other/widget_helper_win.cpp`,
  the client's own Qt-free `platform/win/widget_channel_win.cpp` and
  `widget_sandbox_win.cpp`, which Telegram compiles too, and
  widget-sandbox's platform answers (`lib_widget_sandbox_platform`), with,
  as on macOS and Linux, twidget's Rust serving through the entry
  `_other/widget_helper_rust.cpp` (**Hosted widgets on macOS, Linux and
  Windows** above), its Debug build also the helper's half of the ops tap
  ([test_widget_helper_tap](test_widget_helper_tap.md)), without
  Telegram's precompiled header, as a Windows GUI image with
  `/ENTRY:mainCRTStartup` (so that no console is started) and the static
  CRT. That serving reaches it as one static library: cargo builds
  `twidget_helper_ffi` for the helper's target with `-Z build-std`,
  `+crt-static`, control-flow guard, link-time optimisation and overflow
  checks, always `--release`, into the build tree's
  `widget_helper_rust\twidget_helper_ffi.lib`
  (`Telegram/cmake/lib_widget_helper_rust.cmake`,
  `build_widget_helper_rust.cmake`), and only the helper links it:
  `Telegram.exe` on Windows links without any widget engine. The target is
  `x86_64-win7-windows-msvc` in an x64 tree, the one `prepare.py`'s
  `tdesktop_rust` stage builds for x64, and `i686-win7-windows-msvc` in a
  Win32 tree, whose std supports Windows 7 and links ADVAPI32; and
  `aarch64-pc-windows-msvc` for ARM64, whose helpers have not been built
  in Rust. An `i686-win7-windows-msvc` archive built before the
  engine's metering allocator reached its block headers through the
  system allocation's exposed provenance ended the process with
  `0xC0000374` (heap corruption) whenever it compiled a script that
  declares a class
  (`2026/11/21/build-the-windows-widget-helper-image-for-windows-7`, its
  `work/decisions.md`, W16). That was the fold of the allocator's header
  reads that
  `2026/11/21/remove-the-misaligned-nova-result-writes-and-turn-on-overflow-checks-on-x86-windows`
  diagnosed on the pc target with overflow checks, reached here through
  the win7 std's OS TLS slots, which keep the allocator an out-of-line
  call as the checks did, and that task's metering fix alone removes it
  (`2026/11/21/build-the-x86-windows-widget-helper-image-for-windows-7`,
  its `work/decisions.md`, D2). The archive the Win32 helpers link, built
  by the same recipe with overflow checks, compiled and evaluated W16's
  class scripts and the class cases of that diagnosis in a standalone x86
  driver, three processes each, while W16's archive still ended each of
  them with `0xC0000374`.
  Under `DESKTOP_APP_WIN7_BASELINE`, the Windows 7 baseline of the x86 and
  x64 builds, the helper links `/SUBSYSTEM:WINDOWS,6.01` as `Telegram.exe`
  and the Updater do, so its header names 6.01, Windows 7, as the lowest
  Windows version; ARM64 has no such baseline and keeps the linker's
  default version.
  The x86 and x64 helpers import statically only KERNEL32 and ntdll, and
  delay-load ADVAPI32, `bcrypt.dll` and, in the Debug build for the probe
  alone, `WS2_32.dll`. Their win7 std draws its random bytes through
  ADVAPI32's `SystemFunction036` (`RtlGenRandom`), which the x86 and x64
  images import by name. The Windows 7 SP1 export database
  `win7sp1_exports.txt` of the import gate that
  `.github/workflows/win7-gate.yml` runs on `Telegram.exe` records that
  function as ADVAPI32's own export. On Windows 10.0.26300, where the
  helpers were first measured, both ADVAPI32 copies, the System32 one and
  the SysWOW64 one that x86 processes load, forward it to
  `CRYPTBASE.SystemFunction036`, `cryptbase.dll` is no KnownDLL, and a
  lookup through the forwarder maps it: for a static import at process
  start, where Windows' parallel loader can open it on a worker thread
  that runs as the LPAC primary (P6 in **The Windows lowering** below),
  and for a delay import in the `GetProcAddress` of the CRT's delay-load
  helper. Windows 10 22H2 (10.0.19045.6466) has the same forwarder in
  both copies and no `cryptbase.dll` among its KnownDLLs
  (`2026/11/15/run-the-lpac-windows-widget-helper-on-a-windows-build-before-26300`,
  its `evidence/record.md`). So the delay-load notification hook of the
  x86 and x64 helpers
  (`__pfnDliNotifyHook2` in `_other/widget_helper_win.cpp`, compiled for
  x86 and x64, not for ARM64) answers that one import at
  `dliNotePreGetProcAddress`, before that `GetProcAddress`, with the
  helper's own function, declared `BOOLEAN WINAPI` like `RtlGenRandom`
  (stdcall on x86), which draws the bytes with `bcryptprimitives.dll`'s
  `ProcessPrng` where Windows has it, resolved in `WidgetHelper::Lockdown()`
  before the lowering, and with `BCryptGenRandom` and
  `BCRYPT_USE_SYSTEM_PREFERRED_RNG` where it does not (Windows 7), and
  returns whether that succeeded: under the impersonated initial token only
  one preferred-RNG `BCryptGenRandom` succeeds, `WarmUp()`'s, so the
  threads started after the lockdown draw their keys through `ProcessPrng`;
  the delay-load helper
  writes that address into the import table, and every other import
  resolves as without the hook. The x86 tables were read on the Win32
  Debug and Release-configured helpers on Windows 10.0.26300: static
  `KERNEL32.dll` and `ntdll.dll` (`NtWriteFile` and
  `RtlNtStatusToDosError`), and the delay descriptors `ADVAPI32.dll`
  (23 functions in Debug, 13 in Release), `bcrypt.dll` (`BCryptGenRandom`)
  and, in Debug only, `WS2_32.dll` (`WSAStringToAddressW` and 18
  ordinals), `ADVAPI32.dll` and `bcrypt.dll` spelled as `WarmUp()` binds
  them. These are the readings of the final helpers of
  `2026/11/21/choose-the-windows-widget-helpers-sandbox-tier-by-windows-version`
  (its `evidence/imports.md`): in Debug 170 static functions (KERNEL32
  168, ntdll 2) and 43 delayed ones on x86 and on x64. The fixed
  non-Store package SID removed the helper's only call to the delayed
  `ADVAPI32!FreeSid`. The repaired x86 and x64 Debug helpers of
  `2026/11/15/run-the-lpac-windows-widget-helper-on-a-windows-build-before-26300`
  (its `work/test.md`, Attempt 1 Run 4) import 170 functions statically
  and delay-load 42, 22 of them from `ADVAPI32.dll`, in the same three
  descriptors, with nothing added; their Release tables have not been
  read again. The clean x64 Store Debug helper measured in
  `2026/11/22/prove-the-windows-store-widget-helper-on-windows-10`
  added static `KERNEL32!GetCurrentPackageFamilyName` and delayed
  `bcrypt!BCryptHash` and dropped delayed `ADVAPI32!FreeSid` compared
  with its pre-repair image (213 imports before, 214 after). The x64
  Store Debug client added the same static family lookup and imported
  `BCryptHash` through delayed `ncrypt.dll`. Both non-Store Debug
  helpers, x86 and x64, matched all 212 reference entries.
  When
  `2026/11/21/build-the-x86-windows-widget-helper-image-for-windows-7`
  first read these tables, the Release `ADVAPI32.dll` descriptor had 12
  functions: the later task's Windows 7 lowering (`LockDownWindows7()`)
  added `OpenProcessToken`, for its own `TOKEN_QUERY` handle, to the
  Release descriptor of both the x86 and the x64 helper, 12 functions to
  13 each, while the Debug descriptors imported it already. Built for the
  pc target `i686-pc-windows-msvc` before, the same
  helpers imported statically ADVAPI32, KERNEL32, ntdll and the pc std's
  API set `api-ms-win-core-synch-l1-2-0.dll` (`WaitOnAddress`,
  `WakeByAddressAll` and `WakeByAddressSingle`), and delay-loaded
  `bcrypt.dll`, `bcryptprimitives.dll`, whose `ProcessPrng` was that
  std's random source, and the Debug `WS2_32.dll`. Windows 7 SP1, as the
  import gate's database records it, lacks three of those imports: the
  API set, KERNEL32's `GetSystemTimePreciseAsFileTime` and
  `ProcessPrng`. The win7 images import none of them.
  The ARM64 helper keeps the pc target's std, whose random source is
  `bcryptprimitives.dll`'s `ProcessPrng`, so by its build rules (no ARM64
  helper's tables have been read) it imports ADVAPI32 and the pc std's
  API set `api-ms-win-core-synch-l1-2-0.dll` statically and delay-loads
  `bcrypt.dll`, `bcryptprimitives.dll` and, for the probe, `WS2_32.dll`.
  No helper imports user32, gdi32 or Qt, and each binds its delay-loaded
  DLLs, `WS2_32.dll` aside, in its lowering (**The Windows lowering**
  below); only the Windows 7 tier's lowering loads `user32.dll`, at run
  time from the system directory (**The Windows sandbox tiers** below).
  The imports before the Nova change
  were read on helpers built with the QuickJS engine, the hosted helper's
  the same as the engine-only one's: their static imports were ADVAPI32 and
  KERNEL32, both KnownDLLs, loaded from the system directory
  (`/DEPENDENTLOADFLAG:0x800`); `bcrypt.dll` was delay-loaded in every
  configuration, and the Debug build's `WS2_32.dll`, delay-loaded too,
  had no caller but the probe. Its build string,
  argument gate and Release refusals are the shared `main()`'s, as on
  macOS and Linux (**The shared helper** above). At its entry, in
  `WidgetHelper::Adopt()` (`_other/widget_helper_win.cpp`), it first
  limits its later DLL loads to the system directory
  (`SetDefaultDllDirectories` with
  `LOAD_LIBRARY_SEARCH_SYSTEM32`, looked up at run time in the already
  mapped kernel32, because Windows 7 has it only with KB2533623, so no
  helper image imports it; without kernel32 or the function, or when the
  call fails, it refuses as below before it reads the token, while the
  client looks the same export up first on Windows 7 and stops at the
  nowhere place without it, **Platform default** above), then takes
  the token only as a decimal handle value without a sign, a leading zero
  or trailing text that is no pseudo-handle, and adopts only the
  byte-mode client end of a pipe, whose inheritance it clears; anything
  else is `Widget helper: the channel could not be adopted`, exit 1,
  before it reads the channel. The CRT's delay-load helper loads its DLLs
  with no search flags of its own, and every such load comes after
  `Adopt()`, so it inherits that limit.
- **The Windows sandbox tiers.** The Windows helper runs from Windows 7
  SP1 on, confined by one of four tiers, four strictnesses of the same
  launch. The owner chose them on 2026-11-20: the x86 and x64
  direct-download builds are Windows 7 SP1 baseline builds
  (`DESKTOP_APP_WIN7_BASELINE`), and widgets in direct downloads should
  work as broadly as possible, so every Windows Telegram runs on gets the
  helper, each in the strongest confinement its version has, while the
  full tier stays exactly as it was
  (`2026/11/21/choose-the-windows-widget-helpers-sandbox-tier-by-windows-version`;
  its `work/plan.md` (Tier table), `work/context.md` (section 3) and
  `evidence/call-ledger.md` name the documented floor and the source of
  every policy, attribute and function a tier uses, and its
  `evidence/record.md` joins them per tier). The tiers below the full
  one are the direct downloads': the Store build maps no version below
  10.0.17763 to a tier (**The Windows Store build** below). The version
  alone picks the tier: `CurrentSandbox()` in `widget_sandbox_win.cpp`
  reads it once per process with `RtlGetNtVersionNumbers`, looked up in
  the mapped ntdll, which no compatibility manifest lowers as it does
  `GetVersionExW`, takes the build from its low 16 bits, compares major,
  minor and build in that order and caches the version with its tier.
  Telegram and the helper compile that unit and each reads the version
  itself; nothing the helper receives (its arguments, the channel, the
  environment) selects a tier, no product switch does, and a failed
  reading selects none. By the version read:
  - **Full**, from 10.0.17763 (Windows 10 1809): the launch of **The
    Windows helper** above and **The Windows lowbox** below, unchanged:
    an LPAC primary and a regular AppContainer startup token of the
    package with no capability, the five creation attributes in the order
    handle list, mitigation policy, child-process policy,
    all-application-packages opt-out and job list, and the words
    `0x1001101111010101` (8 bytes) on x86 and `0x1001101111110100`, with
    `0x10000000` (16 bytes in all) where `IsUserCetAvailableInEnvironment`
    offers CET, on x64. The helper reads back, in order, DEP (x86 only),
    ASLR, extension points, dynamic code, win32k, child process, shadow
    stack (with CET), signature, strict handles, fonts and image load.
  - **LPAC 1703**, from 10.0.15063 (10 1703) through 1803: the full tier's
    tokens, attributes and first word, no CET word, and the readback
    without child process and shadow stack, whose queries Microsoft
    documents from 1709 and 2004. Its x86 creation equals the full tier's,
    and an x64 helper linked `/CETCOMPAT` may get shadow stacks without the
    word, so a full helper under this launch can find nothing weaker to
    refuse.
  - **AppContainer**, from 6.2 (Windows 8) through 8.1 and 10 1507 to 1607:
    regular AppContainer tokens of the same package for both, with no
    capability, no opt-out and no job-list attribute; the handle list, the
    mitigation policy (8 bytes) and, from 10.0.10586, the child-process
    policy. Its words hold the full set minus what its version lacks:
    dynamic code joins at 6.3 (8.1), fonts at 10.0.10240, Microsoft-signed
    only at 10.0.10586 and System32 images preferred at 10.0.14393, from
    `0x0000000111010101` on x86 and `0x0000000111110100` on x64 at 6.2 to
    the full tier's first word at 10.0.14393, and its readback is the
    full tier's list without child process and shadow stack and without
    each policy below its floor.
  - **Windows 7**, 6.1.7601 (7 SP1): the restricted pair at Low with no
    lowbox, the handle list and, on x86 alone, the mitigation policy as
    one 4-byte word, `PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE` (DEP
    with ATL thunk emulation off; a 64-bit word is illegal on 32-bit
    Windows 7), with the measures of **The Windows 7 tier** below.
  - **None**, below 6.1.7601 or with no reading: no helper starts, and
    every widget stops at the nowhere place (**Platform default** above).

  Every tier keeps the job's limits `0x2408` (one active process, kill on
  job close, death on an unhandled exception) and its UI restrictions
  `0xFF`, the pin through the held handle, no thread token after the
  lowering, the forgotten registry roots and the helper's own check
  before Ready. The two LPAC tiers put the helper into its job at the
  creation through the job-list attribute, a Windows 10 one. On the
  AppContainer and Windows 7 tiers the client assigns the suspended
  helper with `AssignProcessToJobObject` right after the creation, before
  the image check, the token attachment and the resume; a failed
  assignment keeps the error, terminates that helper through its process
  handle, never the job, and waits up to 1 s for its end with the same
  wait and texts as every other end (`Complete()` in
  `widget_helper_win.cpp`), failing the start with
  `AssignProcessToJobObject failed: <reason> (<end>, terminated after <n>
  ms)` (`TerminateProcess failed: <reason>` for the termination when that
  failed, `not reaped after <n> ms` for `<end>` when the wait ran out), so
  a parent job that forbids the assignment, such as one without breakaway
  on Windows 7, which nests no jobs, fails closed. When that wait does not
  confirm the end, the failed start keeps the helper's process handle and
  hands it to the client's reaping, as for an ended helper: no other
  helper starts while the reaping waits 1 s for the end and, if the
  helper still runs, terminates it again through its process handle and
  waits 1 s more, and an end it never confirms stops launches for the
  rest of the process. `Manager::reap()` in `tgw_remote.cpp` then logs
  the helper's pid as not reaped and latches `the widget helper was not
  reaped` only when no reason is latched yet; when that failed start was
  the third failure in a row, its own charge has already latched `the
  widget helper failed 3 times in a row`, which stays (**Failure limit**
  below). On those two tiers the assignment is not atomic with the
  creation: a client that dies in between leaves a suspended helper that
  never ran. Only the full
  tier reads the helper's child-process policy back (below 10.0.10586 the
  child-process attribute is not even set), so on the Windows 7,
  AppContainer and LPAC 1703 tiers the job is the one checked hold on the
  helper's children, and the helper's own job check there also refuses a
  job that allows breakaway, silent or not, with `the job's limits
  0x<flags> allow breakaway`.

  The client checks each tier's own pair. Before the creation it checks
  the lockdown and initial token shapes (on Windows 7 reading its own
  token through a real `TOKEN_QUERY` handle, since the pseudo token
  handles are Windows 8 ones) and, on the lowbox tiers, a regular
  AppContainer token of the package with no capability for both. Before
  the resume it checks the actual primary's shape and, on the LPAC tiers,
  an LPAC token, on the AppContainer tier an AppContainer token of either
  level, then the attached startup token the same way; the AppContainer
  tier leaves the `WIN://NOALLAPPPKG` attribute unread, since Microsoft
  marks its token class reserved and that tier never passes the opt-out.
  The texts and prefixes are those of **The Windows lowbox** below. The
  helper checks the set of the tier its own reading maps to (**The Windows
  lowering** below) and never learns which tier the client launched, so
  a helper whose reading maps to a stronger tier refuses before Ready
  wherever its launch lacks a protection that tier requires (a full
  helper under an AppContainer launch, at its LPAC check), and nothing
  refuses where the launch has the same effective protections. No
  refusal leads to a weaker tier: each is a start failure or a refused
  stream (**Failure limit** below).

  Every mechanism of every tier exists on Windows 11, so each tier is
  tested there: that task's disposable overlay overrides only
  `ReadWindowsVersion()`, in the client and in the helper, or in the
  client alone while the helper reads its own version, and every reading
  of a forced tier is such a simulation on Windows 11 build 26300
  (10.0.26300). The full tier alone has also run unforced on a real older
  Windows, Windows 10 22H2 (10.0.19045.6466) in a Hyper-V guest, in x86
  and x64 Debug builds: Telegram and the helper each read 10.0.19045 and
  chose it, that build accepted the tier's attributes and creation words,
  and its tokens' `WIN://NOALLAPPPKG`, the mitigation readbacks the full
  tier requires and the probe's socket denials read as on Windows 11,
  while the helper's package derivation failed there under the LPAC
  primary until the non-Store package SID became fixed data
  (**The Windows lowbox** below;
  `2026/11/15/run-the-lpac-windows-widget-helper-on-a-windows-build-before-26300`,
  its `work/test.md`). No other build from 10.0.17763 to 10.0.26299 and
  no bare-metal Windows before 26300 has run it. For the lower tiers,
  what only a real older Windows shows is field coverage
  that no record here claims: whether it accepts each attribute and flag
  as documented and reads them back as Windows 11 does, its loader, the
  win7 Rust std there, the Windows 8 functions really missing, nested
  jobs, KB2533623 missing, AppContainer networking on 8 and 8.1 and the
  `WIN://NOALLAPPPKG` reading on 1703 to 1803. The forced tiers' readings
  are that task's evidence campaign's (its `work/test.md` and
  `evidence/`), and they agree with the tier lines on this page and on
  [test_widget_helper_probe](test_widget_helper_probe.md#windows-tiers)
  ([Verification](#verification)). Compatibility modes were measured on
  Windows 11 build 26300 (10.0.26300.9711) only, with an x64 Debug pair
  and an x86 Release pair built with the harness: the Windows 8, Windows
  7 and Windows Vista (Service Pack 2) modes, set per user on the copied
  `Telegram.exe` in the Compatibility tab's form or given through
  `__COMPAT_LAYER`, move Telegram's `GetVersionExW` to the mode's
  version, yet both processes read 10.0.26300 and the full tier, the
  helper starts without the layer and reaches Ready, and the widget
  renders. Before that change the same modes moved Telegram's
  `RtlGetVersion` reading to 6.2.9200 (AppContainer; the x64 helper read
  the same and served under it) or to 6.1.7600 and 6.0.6002 (no tier, so
  no helper started), and an x86 helper that inherited the layer ended
  at DLL initialization. An x86 Debug client stalls at startup under
  these modes in the Debug CRT's heap check, so its x86 cells used the
  Release pair
  (`2026/11/22/prove-the-windows-widget-helper-tier-under-a-compatibility-mode`).
- **The Windows 7 tier.** Its path, from Telegram's launch to the
  helper's Ready, is built to call only functions that Windows 7 SP1 has,
  imported or looked up at run time (that task's audit of every call is
  its evidence), and of what Chromium's Windows 7 sandbox took it adds an
  alternate desktop, DEP with ATL thunk emulation off on x86 and heap
  termination (that task's `work/plan.md`, P-T8). Telegram's
  `UnsupportedReason()` first requires `SetDefaultDllDirectories` in
  kernel32, KB2533623's (**Platform default** above).
  - **Desktop.** For each helper the client creates a desktop with
    `CreateDesktopW` on its own window station, named
    `TelegramWidgetHelper-<client pid>-<32 hex digits>` (the prefix
    `kHelperDesktopPrefix`, then the client's pid and 128 bits of
    `BCryptGenRandom` with `BCRYPT_USE_SYSTEM_PREFERRED_RNG`, whose
    failure fails the start with `BCryptGenRandom failed: status
    0x<status>` before any desktop or helper exists), asking
    `DESKTOP_CREATEWINDOW`, `DESKTOP_READOBJECTS`, `DESKTOP_WRITEOBJECTS`
    and `WRITE_OWNER` (`CreateDesktopW` documents the two object rights as
    required with `WRITE_OWNER`), with no hook flag, no inheritable handle
    and the window station's default security, and passes
    `<station>\<name>` as the creation's `lpDesktop`. `CreateDesktopW` is
    documented to assign the new desktop to its calling thread, so the
    client calls it on a short-lived thread of its own and joins that
    thread before the launch goes on: the launching thread's desktop never
    changes, and no thread of the client is left on the helper's desktop.
    That thread only creates the desktop and keeps the handle and the
    call's error for the join. After the join, back on the launching
    thread and before any helper exists, the client owns the handle and
    labels the new desktop Low with no-write-up (`S:(ML;;NW;;;LW)`, the
    label `WinSta0\Default` carries) through `SetSecurityInfo` with
    `LABEL_SECURITY_INFORMATION`, which is what `WRITE_OWNER` is for; the
    owner and DACL stay as created. Unlabelled, the desktop counts as
    Medium, and the helper's Low startup token cannot connect to it
    (`user32.dll` fails to initialize there, `the widget helper is not
    sandboxed: LoadLibraryExW failed: error 1114`).
    It holds that handle
    with the helper's process and job handles, and it neither switches the
    input desktop nor changes its own window station. Once the helper's
    end is confirmed the client closes the desktop itself and lets go of
    the handle only when that close succeeds; a failed `CloseDesktop`
    adds `, CloseDesktop failed: <reason>` to the end's status and keeps
    the handle held. A handle still held, after a failed close or for a
    helper whose end is not confirmed, is closed unchecked when the
    client lets go of that helper: at the end of its failed start, or
    when it drops the helper's completion. A desktop it cannot
    create fails the start with `CreateDesktopW failed: <reason>`, or the
    station name's `GetProcessWindowStation failed: <reason>` or
    `GetUserObjectInformationW failed: <reason>`. A label it cannot set
    fails the start with `SetSecurityInfo failed: <reason>` (or
    `CreateWellKnownSid`, `InitializeAcl` or `AddMandatoryAce failed:
    <reason>` while building it): the creating thread has already exited,
    so the launching thread closes the desktop before the start fails,
    and no helper is created. Window messages are not access-checked
    between the windows of one desktop, so a desktop of its own keeps the
    helper from the user's windows. The window station
    stays Telegram's, because another one would change a process-wide
    resource of Telegram.
  - **Tokens.** The restricted pair of **The Windows helper** above, both
    Low and unboxed. Untrusted integrity, a lockdown default DACL and
    closing other handles before the lowering are not taken: the pair
    stays the one every tier checks, its default DACL is its own, and the
    helper inherits only the pipe and the stderr copy.
  - **Pin.** The pin of **The Windows helper** above without its Windows 8
    functions. The identity of each held folder, of the file and of the
    started image is `GetFileInformationByHandle`'s volume serial and
    64-bit file index, read on NTFS only (`GetVolumeInformationByHandleW`;
    `the <fs> file system has no reliable file index` otherwise, since FAT
    identifiers can change and ReFS's 64-bit ones are not unique), and
    never a zero index, the value of an unavailable file ID, which would
    make any two such files of a volume equal (`the file index is
    unavailable`). The
    digest is the same pinned Authenticode digest, computed without
    `wintrust.dll`. `widget_helper_identity.py` also derives the
    checksum's offset, the certificate-table directory entry's offset and
    the unsigned image's end from the built image, requires the SHA-256
    of the three ranges they bound to equal the catalog digest (`the
    helper image digest differs from its Authenticode digest` stops the
    build otherwise) and writes them into `kBuiltDigests`. Telegram reads
    the 8-byte directory entry at that offset through the held handle,
    accepts either no certificate table with the file ending at the
    unsigned end or a non-empty table, a multiple of 8 bytes, that starts
    exactly there and ends the file (`the widget helper image extent is
    invalid` otherwise), and hashes exactly those ranges in 64 KiB reads
    with BCrypt's SHA-256 (`the widget helper digest does not match this
    build` for any other bytes). Windows' PE subject interface provider
    was not taken: its SHA-256 on an unupdated Windows 7 SP1 is
    undocumented.
  - **Lowering.** `LockDownWindows7()` in `widget_sandbox_win.cpp`. Under
    the startup token, restricted to the user and the enabled groups at
    Low, the helper still opens what the user may read: it runs the
    warm-up's binds and random draw (without the mitigation functions and
    the package, which Windows 7 lacks), opens its own process token with
    `TOKEN_QUERY`, turns on heap termination on corruption
    (`HeapSetInformation(HeapEnableTerminationOnCorruption)`) and checks
    its desktop: the startup request (`GetStartupInfoW`) must be
    `<station>\TelegramWidgetHelper-<decimal pid>-<32 hex digits>` before
    it loads `user32.dll` from the system directory, an ordinary USER call
    (`GetDesktopWindow`) connects the serving thread, and the names of
    `GetProcessWindowStation()` and of `GetThreadDesktop()` of that thread,
    read with `GetUserObjectInformationW(UOI_NAME)`, must equal the
    request but for case (`the widget helper is not on its own desktop`,
    or the failing call with its error). Then it reverts, forgets the
    registry roots and checks no thread token, the one step every tier's
    lowering shares (`DropStartupToken()`), then the lockdown token through
    its own handle, the job, which must not allow breakaway either (**The
    Windows sandbox tiers** above), and, on x86, `GetProcessDEPPolicy`'s
    `0x3`, permanent (`the DEP policy reads 0x<flags>, permanent <0|1>`); the
    handle is closed when the lowering returns. The independent evidence
    of the desktop is an outside reading of `GetThreadDesktop` of the
    serving thread from Telegram's window station: a startup name records
    only what was asked.
  - **What it leaves.** By design, which only simulations on Windows 11
    read here, the Windows 7 tier leaves unconfined:
    - the network: without a lowbox nothing keeps native code from
      sockets. A restricted Low token with its job and policies created a
      socket and connected to a loopback listener in the helpers before
      the lowbox (**What the Windows lowering leaves** below), so the
      probe's socket and AFD lines of this tier are recorded as they
      read, never judged as denials;
    - win32k: no Windows 7 policy disables it, and the helper connects
      to USER in its own lowering, so user32 and win32k stay reachable on
      its own desktop of Telegram's window station, within the job's UI
      restrictions (no USER handles of processes outside the job, no
      clipboard, its own atom table, no desktop creation or switch,
      system parameters, display settings or exit);
    - the mitigations: no readback but x86's DEP, since
      `GetProcessMitigationPolicy` is Windows 8: no ASLR, extension point,
      dynamic code, strict handle, signature, font or image load policy,
      no child-process attribute (the job's one process and the lockdown
      token hold child creation), no SEHOP (no documented Windows 7
      readback) and on x64 no creation policy at all;
    - the heap: termination on corruption only, set in-process before the
      lowering and never readable back.
- **The Windows lowbox.** On the full, LPAC 1703 and AppContainer tiers
  (**The Windows sandbox tiers** above; the Windows 7 tier has no lowbox)
  `NtCreateLowBoxToken` boxes each of the two
  restricted tokens with one package SID, that of the fixed name
  `telegram.desktop.widget-helper`,
  with no capability and no handle; no AppContainer profile, named-object
  directory, file grant or loopback exemption is created. Outside the
  Store build that SID is the unpackaged result of
  `DeriveAppContainerSidFromAppContainerName` for the name, kept as fixed
  data (`kPackageRids` in `widget_sandbox_win.cpp`):
  `S-1-15-2-2297200083-2220774571-4131773200-153869053-3443934028-2520258059-2811718137`.
  The earlier derivation loaded `USERENV.dll` and through it
  `profext.dll`. It failed in the helper under its LPAC primary on Windows 10 22H2
  (10.0.19045.6466): there `profext.dll` imports its dependencies
  statically (10.0.26300 delay-loads them), and the loader's worker
  threads, which run as the primary, could not map that graph, so every
  confined start on x86 and x64 refused before Ready with `the widget
  helper is not sandboxed: DeriveAppContainerSidFromAppContainerName
  failed: 0x80004005`
  (`2026/11/15/run-the-lpac-windows-widget-helper-on-a-windows-build-before-26300`,
  its `work/decisions.md`, D1). The first start of the pre-repair x64
  Store Debug helper on the same guest gave the same refusal. In that
  run the packaged client's userenv call returned the same child SID
  recorded for the stand-in family on 10.0.26300.9711
  (`2026/11/22/prove-the-windows-store-widget-helper-on-windows-10`,
  its `evidence/first-start.md`).
  In the current Store build the client and helper each read their own
  package family name with `GetCurrentPackageFamilyName` and lowercase
  its ASCII letters. `BCryptHash` hashes the UTF-16LE bytes with
  SHA-256, excluding the terminating NUL. After `S-1-15-2-` the child SID
  contains the first seven little-endian DWORDs of that digest followed
  by the last four of `kPackageRids`. Each process caches its result in
  `ExpectedPackage()` for token creation or verification.
  On the two LPAC tiers the creation's
  attribute list adds `PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY`
  with `PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT`, which makes the
  helper's primary a less privileged AppContainer (LPAC) token, one that
  `ALL APPLICATION PACKAGES` grants do not reach; the AppContainer tier
  leaves it out, so its primary stays a regular AppContainer token, which
  those grants reach. The startup token stays
  a regular AppContainer token of the same package on purpose. In the
  experiment of
  `2026/11/14/confine-the-windows-widget-helpers-network-with-lpac-and-turn-it-on`
  (its `work/experiment.md`) a less privileged startup token could not be
  built without a privilege, a WOW64 helper started under one taken from
  another process only with an extra registry capability, and a startup
  token of another package or of none was downgraded to identification,
  so the helper died before its first frame.
  The client checks the pair it built: the lockdown token keeps the shape
  above, and both must be regular AppContainer tokens of exactly that
  package with no capability, or the launch fails with `the lockdown
  token check failed: <error>` or `the initial token check failed:
  <error>`. After the creation and before the resume it opens the
  helper's actual primary token and requires the lockdown shape and an
  LPAC token of that package with no capability (`the process token check
  failed: <error>`), then reads the attached initial token back and
  requires its shape and a regular AppContainer token of that package
  (`the attached token check failed: <error>`); on the AppContainer tier
  every one of these checks takes an AppContainer token of either level,
  reading no `WIN://NOALLAPPPKG`, and on the Windows 7 tier only the
  shapes are checked (**The Windows sandbox tiers** above). Either
  failure ends the suspended helper through its job and is a start
  failure (**Runtime log and deaths** below). Building the pair can fail
  with
  `NtCreateLowBoxToken is not available` or `NtCreateLowBoxToken failed:
  status 0x<status>`, and in the Store build also with
  `GetCurrentPackageFamilyName failed: error <n>` (the returned Win32
  error) or `BCryptHash failed: status 0x<status>` (the NTSTATUS). The
  AppContainer check's errors are `the token is not an
  AppContainer token`, `the token's AppContainer SID is not the widget
  helper's`, `the token has <n> capabilities`, `the token is not a less
  privileged AppContainer token` or `the token is a less privileged
  AppContainer token` (the level is read from the token's
  `WIN://NOALLAPPPKG` security attribute, because
  `TokenIsLessPrivilegedAppContainer` failed with error 87 on Windows
  10.0.26300, and on 10.0.19045.6466 too), `the token's AppContainer
  information is truncated`, `the
  token's security attributes version is <n>`, `the token's security
  attributes are malformed`, `the token's WIN://NOALLAPPPKG type is <n>`,
  `the token's WIN://NOALLAPPPKG has <n> values`, `the token's
  WIN://NOALLAPPPKG value is <n>` and `GetTokenInformation failed: error
  <n>`. The design makes no grant: the client opens the image, the
  helper imports statically only system DLLs, on ARM64 with the pc std's
  API set (**The Windows helper** above; the static imports of the
  QuickJS-era helpers were ADVAPI32 and KERNEL32, both KnownDLLs), it
  binds the delay-loaded DLLs it uses while it still impersonates (**The
  Windows lowering** below), and every later DLL comes from the system
  directory. The
  evidence campaign of
  `2026/11/15/move-the-windows-widget-helper-into-a-qt-free-executable-and-run-it-on-x64`
  (its `work/test.md`, Attempt 1 Run 3, Win32 Debug only, with the
  QuickJS engine) started
  `TelegramWidgetHelper.exe` to Ready that way through the product
  `Launch()` of account-free clients with fresh ordinary permissions:
  from the checkout's `out\Debug\modules\x86\`, from `setup.iss`'s files
  plus the helper in a new per-user folder under `%APPDATA%`, from
  `build.bat`'s portable ZIP plus the helper extracted with
  `Expand-Archive` into `Downloads`, and from that folder moved whole to
  `C:\Tmp`. No AppContainer-family ACE, package profile, mapping or
  storage key appeared, and no grant was needed. With execute access to
  the helper's image denied to the user, the start failed closed with
  `CreateProcessAsUserW failed: Access is denied.`, the engine that asked
  for it died with that error and none ran in process, and the
  byte-exact restore brought Ready back in the same client. The
  same-binary helper's no-grant runs, which also covered an updater-style
  `CopyFile` over the executable and a copy on the WSL distribution's 9P
  share, are under [Verification](#verification).
- **Lockdown.** On macOS the helper locks itself down once it has accepted
  the client's opening and holds the bundles, before any runtime exists and
  before it answers `Ready`. It first checks, once per helper, that it
  holds no connection a widget must never reach: it lists its Mach port
  names, looks up the preferences, directory, window server, render
  server, TCC, LaunchServices, distributed notification, keychain, camera,
  audio, pasteboard and location services (`kForbiddenServices` in
  `_other/widget_helper_mac.cpp`), and refuses when a looked-up right was
  already held. It releases only the reference each lookup added and lists
  the names again, which must be unchanged. A service that is not
  registered passes; a failed call, or names that still differ after three
  tries, refuse. The check's first lookup is the process's first
  `bootstrap_look_up`, which lazily adds the host port, a voucher and a
  reply port ([test_widget_helper_probe](test_widget_helper_probe.md)), so
  a first try's names can differ and the next try compares them again.
  libSystem's logging and notify connections are not in the list. Then
  it applies a deny-default seatbelt profile with `sandbox_init`, which
  opens libSystem's logging connection itself and allows little beyond
  reading system libraries and the dyld cache, then sets its descriptor
  ceiling to zero, so it opens no new file or socket while the channel and
  the standard descriptors stay open
  ([test_widget_helper_probe](test_widget_helper_probe.md) lists what it
  keeps and what it is denied). When any step fails, the
  helper refuses the stream, prints `Widget helper: <reason>` to its stderr
  and exits 1: `the widget helper holds a connection to <service> before
  lockdown` for a held connection, the failed call such as
  `bootstrap_look_up(<service>) failed: <error> (<code>)` when the check
  cannot tell, `sandbox_init failed: <error>` for the profile and
  `setrlimit failed: <error>` for the ceiling. The main log has `Tgw:
  Widget helper <pid> stream ended: the engine process refused the stream:
  <reason>.`, its widgets die with `the widget engine process ended`, and
  the refusal counts towards the failure limit below. No widget ever runs
  in the client instead. A hosted widget's served host, validator and
  renderer are built only at its hosted start, after `Ready`, so the
  lockdown, its profile, its forbidden services and its descriptor ceiling
  are taken before any of them exists.
  The Mac App Store service runs the same lockdown with four
  differences, each `#ifdef OS_MAC_STORE` in `_other/widget_helper_mac.cpp`.
  Its service check takes `BOOTSTRAP_NOT_PRIVILEGED` (1100) as not held:
  the App Sandbox denies three of the names (LaunchServices, the camera
  assistant registration and the desktop location registration) from
  before any initializer runs. `sandbox_init` fails there with `EPERM`,
  which it takes as the App Sandbox's confinement; libsandbox then prints
  `sandbox initialization failed: Operation not permitted` to stderr once
  per service, no refusal. Any other failure still refuses, and a service
  that was not sandboxed gets the profile. After the zero descriptor
  ceiling it closes every descriptor that the failing call added
  (libsandbox's syslog socket to logd) and sets its process limit to zero,
  so `fork` and `posix_spawn` fail with `EAGAIN` while threads still start;
  `setrlimit failed: <error>` refuses. Its last step, `DropBootstrap()`,
  cuts every new Mach lookup with public calls only: it clears
  `bootstrap_port` and the bootstrap special port, unregisters the ports
  that `mach_ports_lookup()` would return (launchd registers one that it
  serves) and removes every send reference of the bootstrap name,
  libxpc's own included. libxpc's bootstrap pipe still names it and keeps
  sending to it until a send fails, so the step then makes one
  `bootstrap_look_up()` of the first forbidden name through the cleared
  port: its send to the gone name fails and breaks the pipe at once,
  before Mach can reuse that name for another right. From then on
  `bootstrap_look_up()`, `bootstrap_parent()` and
  `xpc_connection_create_mach_service()` fail, while libxpc, libdispatch
  and logging keep serving through the rights the service already holds.
  A failed call refuses with `<call> failed: <error> (<code>)`
  (`task_set_special_port`, `mach_ports_register`, `mach_port_get_refs`
  or `mach_port_mod_refs`), a bootstrap name still in the space
  afterwards with `the widget helper still holds its bootstrap port`, and
  a lookup that succeeds with `the widget helper can still look up Mach
  services`; any error code of that lookup passes. The App Sandbox still
  lets it reach more than the profile would, but nothing that needs a
  lookup: Telegram's process information, the services it already holds,
  IOKit and file metadata
  ([test_widget_helper_probe](test_widget_helper_probe.md#mac-app-store)).
- **The Windows lowering.** The Windows helper drops its impersonation in
  the framework's lockdown callback, after it accepted the opening and
  holds the bundles and before it answers Ready, so before any runtime
  exists (`LockDownHelper()` in `widget_sandbox_win.cpp`, which
  `WidgetHelper::Lockdown()` calls). It lowers by the tier its own version
  reading maps to: `LockDownLowBox()` on the full, LPAC 1703 and
  AppContainer tiers, described here, and `LockDownWindows7()` on the
  Windows 7 tier (**The Windows 7 tier** above); with no tier it refuses
  with `the widget helper needs Windows 7 SP1 or later` (in the Store
  build `the widget helper needs Windows 10 1809 or later`). While still
  impersonating it checks that the mitigation readback is available (`the
  mitigation policy functions are not available` otherwise), binds the
  delay-loaded imports
  with `__HrLoadAllImportsForDll`, on x86 and x64 those of `ADVAPI32.dll`
  and then those of `bcrypt.dll`, on ARM64 those of `bcrypt.dll` and then
  those of `bcryptprimitives.dll`, each by the exact name the linker gave
  its descriptor (the CRT's `PiddFromDllName()` in `delayhlp.cpp` compares
  names case-sensitively). On the lowbox tiers it then reads
  `ExpectedPackage()`. The Store helper derives the child SID using
  the family lookup and the just-bound `bcrypt!BCryptHash`. The
  non-Store helper uses its fixed SID (**The Windows lowbox** above).
  The `USERENV.dll` and `profext.dll` mappings observed in
  `2026/11/14/confine-the-windows-widget-helpers-network-with-lpac-and-turn-it-on`
  came from the earlier userenv derivation. Warm-up then draws random
  bytes once to start the provider a widget's `crypto.getRandomValues`
  uses later. `bcrypt.dll` was the image's one static import that is no
  KnownDLL, and the system directory does not let the LPAC primary open
  it, so it is delay-loaded and the impersonating main thread maps it.
  While the helper still imported it statically, 3 of 5 confined launches
  on x86 and on x64 died before any frame with `0xC0000135`, and a load
  trace pointed to Windows' parallel loader opening it on a worker thread
  that runs as the primary
  (`2026/11/15/move-the-windows-widget-helper-into-a-qt-free-executable-and-run-it-on-x64`,
  its `work/decisions.md`, P6). On ARM64 the pc target's Rust runtime
  adds `bcryptprimitives.dll`, its random source, which is not a KnownDLL
  on every Windows either, so the ARM64 helper delay-loads it too and
  binds it right after `bcrypt.dll`. On x86 and x64 the win7 runtime's
  random source is ADVAPI32's `SystemFunction036`, which the delay-load
  hook answers with the helper's own function over `ProcessPrng` or
  `BCryptGenRandom` (**The Windows helper** above), so the ADVAPI32 bind
  follows no forwarder into
  `cryptbase.dll`. The x86 and x64 helpers bind the whole ADVAPI32
  descriptor because ADVAPI32 is still called after the
  reversion: the steps and checks after it call
  `RegDisablePredefinedCacheEx`, `RegCloseKey`, `OpenThreadToken`,
  `GetTokenInformation` and more, the Debug probe calls
  `IsTokenRestricted`, and the Rust runtime calls `SystemFunction036`
  during widget work. Left lazy, any of these first called after the
  reversion would resolve its delay import after the lowering.
  `RevertToSelf` itself is no such case: its lazy thunk would resolve on
  its first call, before that call drops the impersonation. Telegram,
  which compiles the same unit, never runs this lowering. A failed bind stays
  closed before Ready either way
  (`__HrLoadAllImportsForDll()` and `__delayLoadHelper2()` in MSVC
  14.44's `delayhlp.cpp`). A name that is missing or in another case
  matches no descriptor, so `__HrLoadAllImportsForDll` binds nothing and
  returns `0x8007007E` (`HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND)`) to the
  warm-up, which refuses with
  `__HrLoadAllImportsForDll(<dll>) failed: 0x<hresult>`, here
  `0x8007007e`, `<dll>` being `ADVAPI32.dll` or `bcrypt.dll` on x86 and
  x64 and `bcrypt.dll` or `bcryptprimitives.dll` on ARM64. A matching
  descriptor never makes it return a failure: it calls
  `__delayLoadHelper2` for each of the descriptor's entries and returns
  `S_OK` after the last, and `__delayLoadHelper2` raises `0xC06D007E`
  (`ERROR_MOD_NOT_FOUND`) when `LoadLibraryEx` cannot load the DLL and
  `0xC06D007F` (`ERROR_PROC_NOT_FOUND`) when neither the delay-load hook
  of the x86 and x64 helpers nor `GetProcAddress` resolves a function of
  the loaded DLL. No
  failure hook is set and nothing catches either exception, so the
  helper dies before Ready, and so before any frame, with
  `exit 0xC06D007E` or `exit 0xC06D007F`. No run has seen either from the
  warm-up; the `exit 0xC06D007E` that **What the Windows lowering
  leaves** below records ended a helper later, during widget work before
  the random warm-up existed. Then the helper reverts to its lockdown
  token, disables the predefined registry key
  cache and closes the cached roots, and verifies itself: no thread token
  (these three are `DropStartupToken()`, the step the Windows 7 lowering
  shares), the lockdown token above, an LPAC token of exactly its package
  with no capability (the AppContainer check's errors above; on the
  AppContainer tier an AppContainer token of either level, the one rule
  `CheckAppContainerToken()` applies to the client's checks too), its
  job's limits and UI restrictions (and, on every tier but the full one,
  no breakaway, **The Windows sandbox tiers** above), and every
  mitigation of its tier's creation read back
  (**The Windows helper** and **The Windows sandbox tiers** above); it
  sets no mitigation itself. The first step
  that fails refuses the stream: the helper writes `Widget helper: the
  widget helper is not sandboxed: <error>` to stderr and exits 1, the main
  log reads `Tgw: Widget helper <pid> stream ended: the engine process
  refused the stream: the widget helper is not sandboxed: <error>.`, and
  the widget dies. The errors
  after the revert carry Windows error numbers, such as
  `RegDisablePredefinedCacheEx failed: error N` or
  `GetProcessMitigationPolicy(<policy>) failed: error N`, or name the
  reading that failed, such as `the token's integrity 0x2000 is not Low`,
  `the token's user is not deny-only`, `the token's restricting SIDs are
  not exactly S-1-0-0`, `the job's limits are 0x<flags> for <n>
  processes`, `the job's limits 0x<flags> allow breakaway` or `the
  mitigation policy <policy> reads 0x<flags>`.
- **Windows probe.** With `probe` the client passes the mode to the
  Windows helper, and the lockdown hook of its Debug build adds the
  native probe of `test/test_widget_helper_probe_lockdown_win.cpp`
  ([test_widget_helper_probe](test_widget_helper_probe.md#windows)) to the
  lowering, once per helper: it takes the probe's positive controls while
  the helper still impersonates, lowers the helper
  and, after a successful lowering and before Ready, writes one `Widget
  helper probe: <line>` per line to its stderr (`app_stderr.txt` under
  `test-run`); its widgets then run as with `helper`. A failed lowering
  refuses the stream as with `helper` and writes no probe line. The lines
  that start `before lowering:` are its positive controls, taken after the
  opening was accepted and before the lowering, under the startup token: a
  127.0.0.1 listener with one accepted connection, the helper's working
  directory, the client's working directory (the probe argument the client
  passes, its `cWorkingDir()` with native separators; `before lowering:
  client working directory: not given` when it is empty) and `%APPDATA%`
  opened for listing, `HKEY_CURRENT_USER\Software` opened, `whoami.exe`
  found in the system directory, and the time zone (`bias <minutes>`); a
  passing listener, directory or registry control reads `ok`, and the
  `whoami.exe` lookup `present`.
  On the lowbox tiers the startup token reaches a directory only where it
  grants the package or `ALL APPLICATION PACKAGES`: the checkpoint named
  in **What the Windows lowering leaves** read `error 5` for the client's
  working directory and
  `%APPDATA%` before the lowering, so their `after lowering:` denials have
  no control in the helper, and the same paths opened by the client are
  their positive control. The Windows 7 tier's startup token, restricted
  to the user and the enabled groups at Low with no package, opens them
  (**The Windows 7 tier** above): in the forced Windows 7 probes
  (simulations on Windows 11) they read `ok` before the lowering and
  `denied (error 5)` after it
  ([test_widget_helper_probe](test_widget_helper_probe.md#windows-tiers)).
  The lines that start `after lowering:` try `CreateProcessW` of that
  `whoami.exe`, a TCP socket and a 1 s connect to the listener (`socket:
  not tried, the loopback listener did not start` when Winsock failed
  before or the listener's socket was not created), the same directories
  (two when the client's working directory was not given),
  `HKEY_CURRENT_USER\Software` and
  `HKEY_LOCAL_MACHINE\SOFTWARE`; read back the thread token, the process
  token (on the Windows 7 tier through the probe's own handle, opened
  before the lowering and closed after the row) and the job; name the
  helper's own tier and version, `tier: <tier>, version
  <major>.<minor>.<build>` (`full`, `LPAC 1703`, `AppContainer` or
  `Windows 7`), and, on the Windows 7 tier alone, the window station and
  desktop of its serving thread, `desktop: <station>\<desktop> (thread
  <tid>)`; read the helper's package identity through
  `GetCurrentPackageFullName`, which `ProbePackage()` looks up at this
  point, after the lowering, in the already mapped kernel32, a lookup
  that binds no import slot (`package identity: none
  (APPMODEL_ERROR_NO_PACKAGE)` in a process without one, the package full
  name in an installed Store package, **The Windows Store build** below,
  `GetCurrentPackageFullName failed: error N`, or `not available
  (GetCurrentPackageFullName is missing)` where kernel32 lacks the
  function from Windows 8 on);
  read back the win32k disable of the helper's creation
  (`win32k system calls: DISABLED (ProcessSystemCallDisablePolicy
  0x<flags>)`, `ENABLED (...)` when `DisallowWin32kSystemCalls` is clear,
  or `not read (GetProcessMitigationPolicy(4) failed: error N)`, never
  `DISABLED` for a failed read); read back eleven mitigation policies, one
  `mitigation <name>: 0x<flags>` or `error N` row each for `DEP`, `ASLR`,
  `dynamic code`, `handles`, `win32k`, `extension points`, `signature`,
  `child process`, `shadow stack`, `font disable` and `image load`; and
  run a thread, an event, `OpenProcess` of its own pid, 32 random bytes
  and the time zone again. The package, win32k and mitigation rows are
  read only from the version whose documentation has their query, and
  read `not read (<call> is documented from <version>)` below it, as
  every one of them does on the Windows 7 tier but x86's DEP, read there
  through `GetProcessDEPPolicy`
  ([test_widget_helper_probe](test_widget_helper_probe.md#windows-tiers)).
  A full-tier report has the rows it had before the tier row, in order,
  plus that row. A refused process,
  socket, connect, directory or key reads `denied (error N)` only for
  access denied (5, or `WSAEACCES` for the socket and the connect) and
  `failed (error N)` for any other error, so a connect that timed out is
  no denial; a success reads `ALLOWED`, a completed connect `CONNECTED`.
  `OpenProcess` of its own pid reads `opened` or `error N`. The native
  probe reads no argument but the client's working directory. After its
  lines the harness writes its own: with Winsock, fresh TCP and UDP
  sockets of both families, the fixture's loopback port and numeric
  network targets and, with `connect-control`, preopened sockets used after
  the lowering; with or without it, native `\Device\Afd\Endpoint` opens;
  each before and after the lowering with the same verdict rules (`denied`
  only for `WSAEACCES` or `STATUS_ACCESS_DENIED`); their arguments, lines
  and controls are in
  [test_widget_helper_probe](test_widget_helper_probe.md#windows). The
  macOS probe is a different one, which refuses after its report
  ([test_widget_helper_probe](test_widget_helper_probe.md)), and so is the
  Linux one (**Linux probe** below); on the non-Linux all-other platforms
  no helper starts, so nothing is written.
- **What the Windows lowering leaves.** The readings below were taken
  before the tiers existed, first under the restricted token alone, then
  under what is now the full tier, but for the Nova helpers' Ready
  modules at the end, read under the unforced full tier. The Windows 7
  tier runs without a
  lowbox, so the first of them applies to it by design, and what else it
  leaves is under **The Windows 7 tier** above; the AppContainer tier
  keeps the lowbox without the LPAC opt-out. Without the lowbox the Windows
  helper was no network boundary. In the Windows runs of
  `2026/11/10/run-widget-engines-in-a-sandboxed-helper-process-on-windows`
  (its `work/decisions.md`, I1 to I3) and in the probe run of
  `2026/11/14/run-widget-engines-in-a-restricted-helper-on-windows-behind-a-debug-switch`
  (its `work/test.md`, Run 2), the lowered helper created a TCP socket
  and connected to the probe's live loopback listener
  (`socket(AF_INET, SOCK_STREAM): ALLOWED, connect to 127.0.0.1:<port>:
  CONNECTED`): the restricted token, the job and the mitigations alone do
  not keep native code from the network. With the lowbox, the checkpoint
  of `2026/11/14/confine-the-windows-widget-helpers-network-with-lpac-and-turn-it-on`
  (its `work/checkpoint-2.md`, Win32 Debug on Windows 10.0.26300) read
  `socket(AF_INET, SOCK_STREAM): denied (error 10013)` after the lowering,
  with the pre-lowering self-listener `ok`. That task's evidence campaign
  (its `work/test.md`, Attempt 1 Runs 1-2, on the same host with the
  firewall services running and no loopback exemption for the package)
  measured four socket kinds, `AF_INET` and `AF_INET6` with TCP and UDP:
  after the lowering every fresh socket of these kinds that the probes
  asked for was refused at creation with `WSAEACCES`, and the native
  `\Device\Afd\Endpoint` open with the probe's copy of Winsock's endpoint
  attribute for each kind with `STATUS_ACCESS_DENIED`, each after the same
  call succeeded in the helper before the lowering (the same AFD bytes
  also opened in an ordinary process). Listeners of the helper's parent on
  127.0.0.1, `::1` and the host's own LAN and link-local addresses, of a
  sibling process on 127.0.0.1 and `::1`, and one in the running WSL
  distribution received every ordinary control before, during and after
  the helper and nothing from it. `TelegramWidgetHelper.exe`, built with
  the QuickJS engine, read the same socket and AFD denials on x86 and on
  x64, while listeners of the client
  on 127.0.0.1 and `::1` received every control of the client and of new
  sibling processes and nothing from it; the other targets and
  `connect-control` were not run with it ([Verification](#verification)).
  Native code in the lowered helper could therefore create no TCP or UDP
  socket over IPv4 or IPv6 and open no AFD endpoint for those kinds, and
  the ordinary helper held no socket from before the lowering (its
  handles are below). Sockets that the probe's
  `connect-control` opens before the lowering delivered nothing after it:
  TCP connects to the host's own addresses timed out and one to WSL was
  refused with `WSAEACCES`, and UDP sends reached no listener; the
  timeouts and unreceived sends are no denial. Not established: which
  layer refuses (AFD's check of the token or the filtering platform), the
  behaviour with the firewall service stopped, other socket families and
  types and their AFD endpoints, name resolution, ICMP, other protocols,
  and routing off the machine (the WSL distribution is a separate network
  stack behind the same machine's virtual switch).
  win32k system calls are disabled from the helper's creation, and its
  verification requires `DisallowWin32kSystemCalls`: on x86 and on x64 the
  suspended child read `ProcessSystemCallDisablePolicy` 0x5 before its
  first instruction, the probe `win32k system calls: DISABLED
  (ProcessSystemCallDisablePolicy 0x5)`, an outside reader the same
  through every widget run, and a creation without the flag read 0x4 and
  was refused with `the mitigation policy 4 reads 0x4`. The 0x4 bit is the
  LPAC creation's `DisallowFsctlSystemCalls`, not a win32k disable (the
  checkpoint above). The same-binary helper that `TelegramWidgetHelper.exe`
  replaced could not take it: `Telegram.exe` loads user32 during its
  static initialisation, so its runtime disable failed with error 19
  (`ERROR_WRITE_PROTECT`) without the lowbox and with error 5 under it.
  Measured with the QuickJS engine while the helper served engines alone,
  after Ready, on x86, it held as File handles only the WOW64 pair
  of its working directory (`\Windows` and `\Windows\SysWOW64`, traverse
  access only), the crypto devices `\Device\KsecDD` and `\Device\CNG` of
  the random warm-up, the pipe and the stderr copy, the only inheritable
  handle; as Key handles only two read-only ones, the Image File
  Execution Options root and `Nls\Sorting\Versions`; and no section,
  mutant, symbolic link, window station, desktop, socket or AFD endpoint.
  The same held after 10 s of widget work. On x64 at Ready its File
  handles were `C:\Windows\System32`, the same crypto devices, the pipe
  and the stderr copy, with the same two keys and no socket or AFD
  endpoint. An open relative to a kept key is checked
  against its target key; the probe's fresh opens were denied. The
  same-binary helper had kept more: user32's window station and desktop,
  the `\KnownDlls` and `\KnownDlls32` directories and seven read-only
  `HKEY_LOCAL_MACHINE` keys under the lowbox (Run 2 of the campaign above).
  A thread on the lockdown token, in a scratch process that
  `2026/11/10/run-widget-engines-in-a-sandboxed-helper-process-on-windows`
  launched with the helper's tokens, job and policies (its
  `work/logs/phase-4-impl-4.notes.md`), could not load the DLLs it tried
  that the process had not mapped (`bcrypt.dll`, `propsys.dll` and the
  KnownDll `ws2_32.dll`), so a path that needs a delay-loaded DLL the
  helper had not mapped before the lowering can end it: before the random
  warm-up existed, a widget's `crypto.getRandomValues` ended the helper
  with `exit 0xC06D007E`. `TelegramWidgetHelper.exe` binds the delay
  imports it needs outside the probe before the lowering: `ADVAPI32.dll`
  and `bcrypt.dll` on x86 and x64, and on ARM64, which imports ADVAPI32
  statically, `bcrypt.dll` and `bcryptprimitives.dll` (**The Windows
  lowering** above). At ordinary Ready and after
  widget work, measured with the QuickJS engine while it served engines
  alone, its modules were its own image and
  system DLLs only: on x86 ntdll, KERNEL32,
  KERNELBASE, ADVAPI32, msvcrt, sechost, RPCRT4, bcrypt, userenv,
  ucrtbase, profext and bcryptprimitives beside it, with WOW64's six x64
  modules, and 13 x64 modules on x64; none from outside the system
  directories, no user32, gdi32, win32u, Qt or `WS2_32.dll`, and
  `bcrypt.dll` from the system directory. On x86 the first start of a copy
  at a new path also mapped `apphelp.dll` from there. With Nova the same
  13 names were read at Ready on both architectures on 10.0.26300.9711
  on 2026-11-21
  (`2026/11/21/choose-the-windows-widget-helpers-sandbox-tier-by-windows-version`,
  its `evidence/full-tier.md`, E7). Since the non-Store package SID is
  fixed data (**The Windows lowbox** above) `userenv`, `ucrtbase` and
  `profext` no longer load: at Ready the repaired x86 and x64 Debug
  helpers held 11 modules of their own machine, the image, ntdll,
  KERNEL32, KERNELBASE, apphelp, ADVAPI32, msvcrt, sechost, RPCRT4,
  bcrypt and bcryptprimitives, the same on 10.0.26300.9711 and on
  Windows 10 22H2 (10.0.19045.6466), `apphelp.dll` on the first start
  from a freshly staged path, with no `cryptbase.dll` and nothing else.
  The x86 helper's x64 WOW64 view was the six modules on 10.0.26300 and
  ntdll, wow64, wow64win and wow64cpu on 10.0.19045, where neither
  `wow64base.dll` nor `wow64con.dll` appeared in that view
  (`2026/11/15/run-the-lpac-windows-widget-helper-on-a-windows-build-before-26300`,
  its `work/test.md`, Attempt 1 Runs 1 and 3). The warm-up covers
  the paths those runs found, not every load a later path can need.
- **The Windows Store build.** In the Windows Store build
  (`OS_WIN_STORE`, the special targets `uwp`, `uwp64` and `uwparm`) the
  helper is the same `TelegramWidgetHelper.exe` with its own pin:
  `Telegram/CMakeLists.txt` includes `init_standalone_target.cmake` and
  `telegram_widget_helper.cmake` in every Windows build, so the Store
  build compiles the helper and its client pins the Store helper's own
  Authenticode digest. The Store build keeps the eligibility it had
  before the sandbox tiers: the Store branch that decides it is in
  `ChooseTier()` of the shared `widget_sandbox_win.cpp`, where
  `OS_WIN_STORE` maps no Windows before 10 1809 (10.0.17763) to a tier,
  so its client stops every widget below that with `no sandboxed widget
  helper is available here: the widget helper needs Windows 10 1809 or
  later`, the reason of the earlier 1809 floor, although its package
  declares Windows 10.0.14316 as its minimum. Its package SID comes from
  a separate Store branch, in `DerivePackage()` (**The Windows lowbox**
  above). From 1809 on the Store client and helper each map their own
  version reading to the full tier alone, and the client starts the
  helper like the other Windows builds, with the same `Launch()`, hold,
  pin, started-image check, tokens, job and creation policies of that
  tier (**The Windows sandbox tiers** above). `OS_WIN_STORE` reaches
  the helper too:
  `init_standalone_target.cmake` copies `common_options`' definitions,
  among them the one `cmake/options_win.cmake` adds for
  `build_winstore`. `build.bat` signs
  `modules\%Platform%\TelegramWidgetHelper.exe` after `StartupTask.exe`
  for every Store target and copies it into the AppX folder's
  `modules\%Platform%\` (which it creates on ARM64, where no `d3d` folder
  is made), stopping at a failed copy, before `MakeAppx` packs that
  folder. The package therefore carries the helper at the same relative
  path next to `Telegram.exe`, and an installed package starts
  `<package root>\modules\<arch>\TelegramWidgetHelper.exe`, `<package
  root>` being its install folder, by default
  `C:\Program Files\WindowsApps\<package full name>`. `build.bat` does
  not sign the package; signing keeps the pinned digest, and a Store
  processing that changed the helper's image would fail every start at
  the pin, as an installed test package whose helper differed in one
  byte did (no Store processing was tried). From an installed x64 test
  package on Windows 10.0.26300.9711 ([Verification](#verification)) the
  hold took every folder from the volume's root through `Program Files`,
  `WindowsApps` and the package root down to `modules\x64` and the file,
  and the pin and the started-image check passed; an unpackaged process
  of the same user was refused that folder access on `WindowsApps` and on
  another package's root (error 5). The packaged client ran in a package
  job that allows breakaway, started through its alias and through
  activation alike: limit flags `0x800`, `JOB_OBJECT_LIMIT_BREAKAWAY_OK`
  alone, with no silent breakaway, process limit or UI restriction.
  `CreateJob()` (`widget_sandbox_win.cpp`) adds
  `CREATE_BREAKAWAY_FROM_JOB` whenever the client's immediate job allows
  breakaway and not silent breakaway, so the helper leaves the package
  job for its own job, which read back as in the other builds (limits
  `0x2408`, one active process, UI `0xFF`, the helper its only member);
  a helper that a test variant of the client left in the package job
  refused itself before Ready with `the widget helper is not sandboxed:
  the job's limits are 0x800 for 0 processes`. Its tokens, LPAC level and
  mitigations and the probe's denials read as on x64 but for four
  measured differences, each with its reading and status on
  [test_widget_helper_probe](test_widget_helper_probe.md#windows-store):
  the package SID is a child AppContainer SID of the client's package,
  since the derivation runs in a packaged process, and an ACE naming the
  parent package admitted the helper neither before nor after the
  lowering; the probe's `CreateProcessW` row reads `failed (error 31)`
  instead of `denied (error 5)`, with no child created and the
  child-process policy and job limit unchanged, and comes with the
  package, since the same tree's x86 and x64 non-Store helpers read
  `denied (error 5)` (what returns it was not traced); the helper has the
  package's identity; and the probe helper and the ordinary helper alike
  hold two more System32 modules, `kernel.appcore.dll` and `combase.dll`,
  three read-only `HKEY_LOCAL_MACHINE` handles, 7 unnamed events and more
  `EtwRegistration` handles than the same tree's non-Store helpers, and
  no other extra handle type, all coming with the package (the client
  loads both modules packaged or not).
- **Windows Store test runs.** A Store leg installs a test package with a
  stand-in identity and publisher, such as
  `TelegramDesktopAiStoreTest.Telegram` by `CN=Telegram Desktop AI Store
  Test`, never the production `TelegramMessengerLLP.TelegramDesktop` or
  its publisher. Its manifest is the production one with that identity,
  `ProcessorArchitecture="x64"`, test display names and
  `AppListEntry="none"`, without the `tg`, `tonsite` and `ton` protocol
  extensions, so that it never offers itself as a handler of the owner's
  links, with the startup task kept disabled, the same Application Id and
  `Dependencies`, and one
  `uap3:Extension Category="windows.appExecutionAlias"` with a
  `desktop:ExecutionAlias` such as `tdesktop-ai-store-telegram.exe`;
  Windows accepted the alias with the production `MaxVersionTested`
  10.0.14316.0. Lay the package out from the build output as
  `build.bat`'s Store lines do (`Telegram.exe`, `StartupTask.exe`,
  `modules\x64\TelegramWidgetHelper.exe`,
  `modules\x64\d3d\d3dcompiler_47.dll`, the AppX assets and the `makepri`
  resources), pack it with `makeappx pack`, sign it with the stand-in's
  PFX through `signtool sign /fd SHA256 /f <pfx> /p <password>`, never
  through a certificate store search, and install it with
  `Add-AppxPackage`; a Debug build needs no runtime DLL beside it (the CRT
  is static). Windows installs it only once the stand-in certificate is
  trusted in the local machine's Trusted People store, an elevated action
  that the owner takes and undoes at the end: trust in the current user's
  Trusted People store is not enough (`0x800B0109`), an unsigned package
  with an executable is refused without elevation (`0x80073D2B`), and a
  Developer Mode loose registration would run from its loose folder, not
  from `WindowsApps`. Launch through the alias, `workspace.py test-run --exe
  %LOCALAPPDATA%\Microsoft\WindowsApps\<alias> --portable-root <leg>
  --run-dir <run>`: the alias passes the `--env` values, the redirected
  stdout and stderr (`app_stderr.txt`) and the exit code, and the process
  it starts is the packaged client with the package's identity.
  `test-run`'s `-workdir <leg>\TelegramForcePortable` selects the account,
  since portable mode never engages there: it looks for
  `TelegramForcePortable` next to `Telegram.exe`, in the read-only
  package folder. `test-run`'s own path-scoped cleanups match the alias
  path, which never equals the running image under `WindowsApps`, so
  bracket each launch with `workspace.py test-cleanup --exe <package
  root>\Telegram.exe` and `--exe "\\?\<package
  root>\modules\x64\TelegramWidgetHelper.exe"` (the helper runs under the
  held file's `\\?\` DOS final path, so its `Win32_Process.ExecutablePath`
  carries that prefix and a plain path never matches), never with
  `--delete-exe`. A packaged client's writes outside its working folder
  land in the package's private locations: the Store build creates
  `%APPDATA%\Telegram Desktop UWP` before `-workdir` applies, and it
  appeared as
  `%LOCALAPPDATA%\Packages\<family>\LocalCache\Roaming\Telegram Desktop UWP`,
  never in the real `%APPDATA%`; its `HKEY_CURRENT_USER` writes, such as
  the URL handler registration a fresh working folder makes, left the
  user's `HKCU\Software\Classes\tg`, `tonsite` and `ton` unchanged in
  every launch, while the package's private hive under
  `%LOCALAPPDATA%\Packages\<family>\SystemAppData\Helium` was written (its
  contents were not read). Bracket every launch with an owner-state
  snapshot and compare all the same. A package activation,
  `IApplicationActivationManager::ActivateApplication` of
  `<family>!Telegram.TelegramDesktop.Store` with the arguments, starts the
  client the way production does, with the alias route's identity and
  package job, but passes only the arguments: the client read no test
  variable, started in the activating caller's working directory and
  wrote its test log into `<workdir>\test_evidence`; with an account-free
  root it quit at the harness's no-test-account gate. At the end remove
  the package with `Remove-AppxPackage`, which removed its alias and its
  `%LOCALAPPDATA%\Packages` folder, and have the owner remove the
  certificate.
- **The Linux lockdown.** At its entry, in `WidgetHelper::Adopt()` before
  it adopts its channel, the Linux helper asks the kernel for `SIGKILL`
  when its parent ends (`PR_SET_PDEATHSIG`) and closes every descriptor
  above 3 (`PrepareHelperProcess()`). It locks itself down where the macOS
  helper does, once it has accepted the client's opening and holds the
  bundles, before any runtime or engine thread exists and before it
  answers `Ready` (`LockDownHelper()` in
  `platform/linux/widget_lockdown_linux.cpp`, compiled only into the
  helper). In this order it:
  1. refuses unless `/proc/self/task` lists exactly one thread;
  2. reads its time zone once (`tzset()`) and keeps it;
  3. asks the kernel for its Landlock ABI and builds its seccomp filter;
  4. empties its root: it first unshares a mount namespace of its own, so
     a helper run as root never changes the host's mount table, then makes
     every mount private, mounts a read-only `tmpfs` on `/tmp`, makes it
     the root with `pivot_root`, detaches the old root and changes to the
     new `/`;
  5. drops every capability (`capset`);
  6. sets `PR_SET_NO_NEW_PRIVS`;
  7. when the kernel reported an ABI, applies a Landlock ruleset with no
     rule that handles the filesystem rights of that ABI, up to ABI 5's
     device `ioctl`, TCP bind and connect from ABI 4, and abstract Unix
     sockets and signals from ABI 6;
  8. installs the seccomp filter with `seccomp(2)` and
     `SECCOMP_FILTER_FLAG_SPEC_ALLOW`, or with `prctl(PR_SET_SECCOMP)` when
     that call fails.

  The filter, built for x86_64 or aarch64, allows reads and writes on the
  channel and stderr (the channel's `recvfrom` and `sendto` only
  with the stream's and the probe's flags and no address; other
  flags, `MSG_OOB` among them, or an address get `ENOSYS`), memory
  management without executable memory (an executable `mmap` or
  `mprotect` gets `EPERM`; `madvise` only with `MADV_DONTNEED` and the
  thread-stack guard values of glibc 2.42 and later), futexes that only
  wait and wake (`FUTEX_WAIT`, `FUTEX_WAKE` and `FUTEX_WAIT_BITSET`),
  glibc's own thread creation, naming (`PR_SET_NAME` only) and end,
  `clock_gettime` and `gettimeofday`, random bytes, signal set-up and
  signals to the helper itself. It ends the helper with signal 31
  (`SIGSYS`) for `execve`, `execveat`, `socket`, `connect`, `ptrace`,
  `process_vm_readv` and `process_vm_writev`, on x86_64 also `fork` and
  `vfork`, for a `clone` that does not create a thread, and for any call
  of another architecture or of the x32 ABI. It answers every other call,
  opening a file, sleeping and returning from a signal handler included,
  with `ENOSYS`; `clone3` gets `ENOSYS` as well, so glibc falls back to
  `clone`. The list was measured on `TelegramWidgetHelper` with glibc
  2.28, 2.39 and 2.41 and lost 16 entries that nothing called after the
  lockdown and no linked code reaches there: `clock_getres`, `time`,
  `nanosleep`, `clock_nanosleep`, `sched_yield`, `rt_sigreturn`,
  `PR_GET_NAME`, four futex operations and five `madvise` values. Nothing
  in the helper sleeps after its lockdown, and a sleep added there would
  return at once, since libstdc++ retries `nanosleep` only on `EINTR`. The
  source note
  `tasks/2026/11/10/run-widget-engines-in-a-sandboxed-helper-process-on-linux.md`
  lists every allowed call with its caller and the rule for adding one. A
  step that fails refuses the stream as on macOS: the helper writes
  `Widget helper: <refusal>` to its stderr and exits 1, the main log
  reads `Tgw: Widget helper <pid> stream ended: the engine process
  refused the stream: <refusal>.`, its widgets die with `the widget
  engine process ended`, and the refusal counts towards the failure limit
  below. The refusals are
  `the widget helper cannot count its threads: <E>`, `the widget helper
  has <N> threads before its lockdown`, `chdir failed: <E>` when the old
  root was already detached, `capset failed: <E>`,
  `prctl(PR_SET_NO_NEW_PRIVS) failed: <E>`, `landlock_create_ruleset
  failed: <E>`, `landlock_restrict_self failed: <E>` and
  `prctl(PR_SET_SECCOMP) failed: <E>`, where `<E>` is an error name such as
  `EPERM`, or `errno<N>`; on a Linux architecture other than x86_64 and
  aarch64, where Telegram never starts it, a helper started by hand
  refuses with `the widget helper has no seccomp filter for this
  architecture`. The main log never lists the layers that applied:
  the namespace fallback line says that the namespaces did not, and
  seccomp, no-new-privileges, the capability drop and an available
  Landlock each apply or refuse. Two cases are silent. A kernel without
  Landlock, whose ABI query fails (`ENOSYS`, or `EOPNOTSUPP` when Landlock
  is off at boot), leaves the helper to seccomp alone. A root the helper
  could not empty, because a step of the empty root before its last change
  of directory failed (for an ordinary user without a user namespace,
  inside Flatpak and under Snap, where snapd's seccomp filter answers its
  `unshare` with `EPERM`), stays the root it was given. Seccomp still
  answers every path call with `ENOSYS` in both, and in a Debug build the
  probe's `layers:` line shows them (**Linux probe** below).
- **Linux probe.** With `probe` or `probe-unsandboxed` the client passes
  the mode, its working directory and its
  `QStandardPaths::GenericDataLocation` after the `3`, inside Flatpak
  through the portal's argv, and the lockdown hook of the Debug
  `TelegramWidgetHelper` runs the probe of
  `test/test_widget_helper_probe_lockdown_linux.cpp`
  ([test_widget_helper_probe](test_widget_helper_probe.md#linux)) once per
  helper, on serve's thread before any engine thread exists. With
  `probe` it first forks three copies of the helper, one at a time: the
  first shows Landlock and the empty root with the seccomp install made to
  fail, the other two lock down and try `execve` and `socket`, which must
  end them. Then the helper locks itself down for real, reads its own
  state and descriptors and goes on serving widgets as with `helper`,
  hosting them, so its `lockdown:`, `after
  lockdown:` and `layers:` lines are those of the helper that then renders
  them. Its
  lines, normally six, each one `Widget helper probe: <line>` on its
  stderr (`app_stderr.txt` under `test-run`), start `landlock alone:`,
  `execve:`, `socket:`, `lockdown:`, `after lockdown:` and `layers:`. A
  refused real lockdown writes `lockdown: FAILED <refusal>` and no line
  after it, and refuses the stream as with `helper`. With
  `probe-unsandboxed` neither the copies nor the helper lock down, its
  `lockdown:` line reads `lockdown: skipped (unsandboxed control)`, and
  the helper serves its widgets unconfined. A probe helper that serves is
  no helper failure.
- **Threads.** With the helper the client runs one `widget.client` thread
  per engine and no `widget.sandbox` or `widget.validator`; the host's
  `twidget-host` stays, and the channel adds `widget.reader`,
  `widget.writer` and `widget.monitor` per helper and a `widget.answer` per
  runtime that asked a synchronous question. On macOS and Linux a client
  whose stderr is a terminal also runs one relay thread per helper, with
  no name of its own (**The shared helper** above). The `widget.sandbox`
  engines and each widget's validator and renderer threads run in the
  helper (**Hosted widgets on macOS, Linux and Windows** above), and the
  client's share of each renderer runs on the one `widget.render` thread,
  which answers the helper's requests (`widget.answer` waits for each);
  the macOS helper's Debug probe adds a `widget.probe` thread in its
  serving variant.
  Without a helper the client has no `widget.sandbox`
  and no `widget.reader`, `widget.writer` or `widget.monitor`: where none
  is available (Windows before 7 SP1, Windows 7 without update KB2533623
  or the Windows Store build before Windows 10 1809, the Mac App Store
  build before macOS 12, a Linux architecture other than x86_64 and
  aarch64, a build without the private Qt API), each widget takes the
  refusing hosted route, whose `WidgetHost` has no validator (*Failure*
  under **Hosted widgets on macOS, Linux and Windows** above).
  Windows threads carry none of these names (the
  framework names its threads only on Apple platforms and Linux): there
  the `TelegramWidgetHelper.exe` child, the runtime log's start note
  (`Started, its engine, validator and renderer run in the widget
  helper.`) and the main log's `Widget helper <pid>` lines show where the
  engines run.
- **Seams.** [test_widget_ops](test_widget_ops.md),
  [test_widget_host](test_widget_host.md) and
  [test_widget_bot](test_widget_bot.md) act in the client: the options
  override edits the options before the `Job` (they reach the helper with
  the hosted start), the host watch merges `WidgetHost::diagnostics()` on
  the client's `twidget-host` thread, and the played bot answers at
  `Runtime::botRequested()` through the responder the client engine handed
  the host. The tap is installed on the hosted host by `Build()` and takes
  the validated batches from its `onRenderOps` and, in Debug, the rest
  from the records the helper's tap mirrors
  ([Hosted widgets](test_widget_ops.md#hosted-widgets)). The disposable
  overlay's work scopes time only the client's share of a widget's
  renderer ([The helper's share](test_widget_host.md#the-helpers-share)).
  The client's Qt provider of `tgw_intl.cpp` answers the engines' `Intl`
  questions over the channel.
- **Runtime log and deaths.** The note of a host's start reads `Started,
  its engine, validator and renderer run in the widget helper.` with the
  helper and `Started, its engine does not run.` where no helper is
  available. The summary and its first `Died:` line, which the packs parse,
  are unchanged. A hosted route that the client's own checks closed (a
  refused list or a malformed report) is a stop the desktop decided
  itself, read with `WidgetDesktopStop()`, never a death of the host. Where no helper is
  available every widget dies at its start with `no sandboxed widget
  helper is available here: <reason>`, in the summary's `Died:` line and
  the host watch's `dead`: a fatal stop that counts towards the widget's
  crashes in a row like any other, with no `Tgw Error` line and no helper
  start. A helper that ends
  under live widgets (killed, crashed or cut
  off by a stream violation) ends each of their hosts with `the widget
  engine process ended`, the host watch's `dead` and the summary's `Died:`
  line alike, whatever the stream decided for that widget; the helper's
  own reason is only in the main log's `stream ended` line, `the engine
  process closed the stream` for a kill. What counts is each hosted run's
  own decision, applied once on the main thread (`Runtime::FinishRun()`;
  *Deaths* above says which decision is the run's):
  - `Stalled` (*A renderer that stops answering* above) or `Ordinary`: the
    run is charged (below), whether or not a widget shows its stop. A
    widget still live on that run's core stops fatally once the core's
    posted stop reaches it (`Runtime::coreStopped()`), keeping its last
    picture under the localized pill when it had loaded, with
    `Stopped (fatal): The widget stopped responding` or, for `Ordinary`,
    the localized stop label (`Stopped (fatal): Widget stopped`); an
    attempt discarded before its start that still owns its request stops
    with the run's reason (`Runtime::stopForRun()`). A run whose message
    was removed while its stop was held shows no stop and is still
    charged as below. At
    an unexpected end that was no timeout (a kill, a crash, a stream
    violation) the stream decides `Ordinary` for each route taking part
    that had a message unanswered then and `Spared` for the others, or
    `Ordinary` for every route taking part when none had one
    (`Connection::blameLocked()`); the owner's stop decides no route
    (`Connection::endLocked()` skips that blame). An engine's own death on
    a live helper reads `Ordinary` too, since no end decided its route.
  - `Spared`: no charge from that end. Only a run whose final is `Spared`,
    whose host had started and whose content is still current earns its
    widget a start of its own (`Runtime::applyFinal()`): a widget still
    live on that run is released (`Released to restart after its widget
    helper ended.`, `Runtime::restartSpared()`), and the start then comes
    with no repaint, scroll or input, in the replacement helper the manager
    launches and locks down as for any start, the message's crash count
    unchanged. That start needs what any start needs
    (`Runtime::wantsStart()`), among them the card on screen, its content
    current and the message's count below the two-crash cutoff, which the
    charge of another run of the same message can have reached; and after
    the third helper failure the manager launches no helper for it
    (**Failure limit** below).
  - `Refused`, a refusal Telegram itself made of the widget's own traffic
    (*A renderer that stops answering* above): charged once with its own
    reason, also on a route the stream spared; it earns no start.
  - `None`, a run withdrawn before any end decided it, with no decision
    recorded (its view unloaded or its message removed first): neither
    charged nor spared. A decision recorded before the withdrawal, a
    refusal included, stays the run's (*Deaths* above), and a
    construction whose engine had claimed its own death before a guard
    of its start, the two-crash cutoff or a stale or capacity discard of
    a recovery attempt withdrew it reads `Ordinary`, with the localized
    stop label and no end of the stream (`Runtime::RunGone()`).

  A run's final survives an unload, a held delivery, the view's or the
  message's removal and a construction discarded before its host started,
  and the tap counts it
  ([test_widget_ops](test_widget_ops.md#hosted-deaths-and-final-charges)).
  A charge moves the crash count of the message and content serial the
  run was built for (`Runtime::ChargeRun()`) only while that message's
  memory lives, its content is still that serial and the count is below
  the cutoff, and only then, while the run's runtime lives, is it noted,
  `Counted a crash, N of 2: <reason>`; a run of HTML that an edit replaced
  since leaves the new content's count alone. A present note proves that
  an accepted charge moved that count. Its absence rules out no final, and
  the note names no sampled work and says nothing of the tap's barrier:
  the tap's finals do. Two charges in a row stop the widget: a later start
  reads `Stopped: 2 crashes in a row.` and starts no host, while the
  accounting of runs that already took part goes on. A release or
  destruction of a healthy live widget (its core has reported content and
  not stopped, its content is current and the count is below the cutoff)
  ends the streak, its count back to 0 (`Runtime::breakCrashRun()`),
  unless withdrawing its route finds its helper's end or a decision
  already recorded, which keeps the count; a charge its run takes after
  such a release counts from no less than the count the release ended.
  While another run of
  the same message and content is undecided (its construction not
  returned yet, its core disposed before its final, or its link deciding,
  `RunsDeciding()`), a start waits (`Runtime::wantsStart()`), keeping the
  paint that asked for it and writing no note; the scheduler considers it
  again once that construction returns and is bound to its run, or that
  run's final is applied. An attempt already underway when such a run is
  found is discarded (`Runtime::resourcesReady()`, `constructed()`),
  noting `Released, a run of the same widget is still being decided.`
  unless its own route had already ended, died or been decided. A helper
  ended because a renderer stopped answering (*A renderer that stops
  answering* above) reads `Tgw: Widget helper <pid> stream ended: a hosted
  renderer left a platform message unanswered for more than 3000 ms.` in
  the main log, then its `ended` line. For the direct-download helper
  (`CompleteChild()` and `CompleteWith()` in `widget_reaping_posix.cpp`)
  that line is `<status>, sent SIGKILL after N ms` when the helper missed
  the 1 s exit wait and the kill ended it (`signal 9, sent SIGKILL after
  1002 ms` in the campaign, for a helper stopped with `SIGSTOP`) and its
  status alone when it exited within that wait (`exit 0` in the
  campaign). The Mac App Store build's `Complete()`
  (`widget_helper_store_mac.mm`) cancels the service's connection instead
  of signaling it, and its `ended` line reads as **The Mac App Store
  service** above lists; that path was read, not run.
  The stream decides each live route at that end
  (`Connection::blameLocked()`): the run of each route it decided
  `Stalled` (its timed message overdue; one or several) is charged once,
  and the end spares the others (*A renderer that stops answering*
  above). The `Stalled` or `Ordinary` case above says which widget, if
  any, then reads `Stopped (fatal): The widget stopped responding` in its
  runtime log, under the same localized pill. A
  widget that had already stopped keeps its own reason and count, since a
  later stop changes neither (`RendererWork::freeze()`,
  `Runtime::coreStopped()`). The app measured one overdue widget alone
  and one beside a live sibling; two overdue routes of one stream both
  read `Stalled` in the scripted timed cases
  ([Verification](#verification)). A widget
  whose helper cannot start dies with `the widget helper could not start:
  <error>`, the seam's error such as `posix_spawn failed: <reason>` on
  macOS (`posix_spawn failed: No such file or directory` when the bundle
  of an ad hoc or unsigned client has no
  `Contents/Helpers/TelegramWidgetHelper`, which such a client never
  checks before the spawn; a certificate-signed client's static check
  opens the file first, below) or a Windows gate error. On Windows the file
  is checked through the handle it is started from (**The Windows helper**
  above), so a missing file reads `could not hold
  modules\<arch>\TelegramWidgetHelper.exe: CreateFileW failed: The system
  cannot find the file specified.`, and the other gate refusals are `the
  widget helper digest does not match this build` (another build, a
  modified helper or a foreign executable at the path), `the widget helper
  identity of this build is unknown` (no pin for this architecture), `the
  widget helper path is redirected: <item>` (a name-surrogate reparse at
  `modules` or `modules\<arch>` or the file), `<function> is not available`
  or `CryptCATAdminCalcHashFromFileHandle2 failed: <reason>` (catalog
  hashing), and `the image check failed: the started image is not the
  checked file` (the suspended child is not the held file). On the
  Windows 7 tier (**The Windows 7 tier** above) the pin adds `the widget
  helper image extent is invalid` (a certificate table that does not
  start at the generated unsigned end or does not end the file), `the
  <fs> file system has no reliable file index` (a volume that is not
  NTFS), `the file index is unavailable` (a zero file index), the failed
  call of the identity (`GetFileInformationByHandle`,
  `GetVolumeInformationByHandleW`), of the reads (`GetFileSizeEx`,
  `SetFilePointerEx`, `ReadFile`, or `ReadFile read <n> of <m> bytes`) or
  of the hash (`BCrypt<function> failed: status 0x<status>`) in place of
  the catalog's, and its desktop `BCryptGenRandom failed: status
  0x<status>` (the name's random part), `CreateDesktopW failed:
  <reason>` or, for its Low label, `SetSecurityInfo failed: <reason>`
  (`CreateWellKnownSid`, `InitializeAcl` or `AddMandatoryAce` before
  it); the AppContainer and Windows 7 tiers add
  `AssignProcessToJobObject failed: <reason> (<end>, terminated after <n>
  ms)` (**The Windows sandbox tiers** above), which, when `<end>` is not
  an exit, is followed by the end lines of the reaping like an ended
  helper's.
  The main log adds `Tgw Error: The widget helper could not start:
  <error>.`. The Mac
  App Store build's errors are listed under **The Mac App Store service**.
  On macOS outside the Mac App Store build a certificate-signed client
  refuses a helper that fails the static slice check of **The shared
  helper** before any spawn, with no cleanup: `the widget helper failed
  the signer check: the <arch> slice's entitlements differ from this
  build's` names the first slice, arm64 before x86_64, whose signed
  entitlements are not exactly the build's (an added, missing or
  differently typed key, on the slice the client runs or the other one),
  and `the widget helper failed the signer check: the <arch> slice:
  <call> failed: <message> (<status>)` one it cannot open, validate or
  read: `SecStaticCodeCheckValidity failed: code failed to satisfy
  specified code requirement(s) (-67050)` for a helper of another
  signer, an ad hoc one or one with another identifier,
  `SecStaticCodeCreateWithPathAndAttributes failed: cannot find code
  object on disk (-67068)` for a missing file, and that call's status
  100013 (`UNIX[Permission denied]`) for a file without read permission.
  The same prefix with `the <arch> slice: the CDHash is unreadable` or
  `the <arch> slice: the entitlements are unreadable` is a slice whose
  signing information lacks them, and with `there is no arm64 or x86_64
  slice` a helper with neither; `the widget helper entitlements of this
  build are unreadable`, without the prefix, is a build whose
  `kEntitlements` does not parse. A helper that passed that check, and
  any helper under an ad hoc or unsigned client, can still fail the
  dynamic check of the suspended child, and is then killed before it
  runs: the error is `the widget helper failed the signer check: <call>
  failed: <message> (<status>)`, or the same with `the build identity
  check`, followed by the cleanup of the suspended child in parentheses,
  its wait status `(signal 9, sent SIGKILL after N ms)`; a failed resume
  reads `kill failed: <reason>` with the same cleanup. Under a
  certificate-signed client a helper replaced after the static reading,
  such as a same-leaf copy with other entitlements, and under an ad hoc
  or unsigned client any helper but the build's own, another build's
  included, read `SecCodeCheckValidity failed: code failed to satisfy
  specified code requirement(s) (-67050)` there; under such a client a
  helper file without read permission reads
  `SecCodeCopyGuestWithAttributes failed: UNIX[Permission denied]
  (100013)`. A refusal of the client's own identity comes before any
  spawn, with neither that prefix nor a cleanup, and names the failing
  call: `SecCodeCheckValidity failed: the code on disk does not match what
  is running (-67034)` after the running client's own executable changed
  on disk, `SecCodeCopyDesignatedRequirement failed: code object is not
  signed at all (-67062)` after an unsigned file replaced it, and
  `SecCodeCopySelf failed: object file format unrecognized, invalid, or
  unsuitable (-67049)` when a replaced universal executable moved the
  running slice. Without a CDHash recorded for the running slice it is
  `the widget helper identity of this build is unknown`, and with
  incomplete signing information `the signature of this build is
  unreadable` or `the signing certificate of this build is unreadable`.
  As on Linux below, when the client's stderr is a terminal and the relay
  of **The shared helper** cannot be made, the start fails before any spawn
  with `socketpair failed: <reason>`, `shutdown failed: <reason>` or
  `fcntl failed: <reason>`, or with a setup error the relay's socket
  shares with the channel (`getsockopt failed: <reason>`, `setsockopt
  failed: <reason>`); the terminal is never handed to the helper instead.
  `fcntl failed: <reason>` is also the error when the duplicate of a
  stderr that is no terminal cannot be made.
  Each is a start failure for the failure limit below. The call name and
  the status are the stable part; the message is the system's.
  On Linux the error is `clone failed: <reason>` when the clone without
  namespaces fails as well, or `<call> failed: <reason>` for another
  failed call of the spawn. In the `TelegramHelpers` layout the check of
  **The shared helper** gives `fcntl failed: <reason>` when it cannot
  duplicate the held descriptor, `fstat failed: <reason>` and, only in a
  Telegram that holds none, `open failed: <reason>` (`open failed: No
  such file or directory` when `TelegramHelpers/TelegramWidgetHelper` is
  missing), and the cloned child, before it
  became the helper, `dup3 failed: <reason>` and `fexecve failed:
  <reason>` (`fexecve failed: Permission denied` for the pinned bytes
  without their execute bit); in a package-managed build the child gives
  `execve failed: <reason>` (`execve failed: No such file or directory`
  when the package's libexec helper is missing). When the client's stderr
  is a terminal and the relay's socket cannot be made, the error is
  `socketpair failed: <reason>`; the terminal is never handed to the
  helper instead. The check itself refuses with `the widget helper is not
  a regular file`, `the widget helper could not be read` or `the widget
  helper failed the build identity check: SHA-256 <actual>, expected
  <pinned>`, both digests in lowercase hex, the last for any bytes but
  the pinned ones: another build's helper, a modified file or another
  executable. The other errors are `stderr is not open`, `the widget
  helper is not available on this architecture`, `the executable path is
  unknown` on both spawn paths when the client has no directory of its
  own (an empty `cExeDir()`) and, inside Flatpak, `the widget helper's
  build identity cannot be checked through the Flatpak portal` for a
  build in the `TelegramHelpers` layout, or for a package-managed one `the
  Flatpak portal is unavailable: <message>` or `Spawn failed: <message>`,
  with the D-Bus error's message. Inside Flatpak the portal's sandbox,
  not the client, executes the file: when a `Spawn` that returned a pid
  started nothing it can run, the stream ends before `Ready`, and that
  end counts towards the failure limit the same way (a reading of the
  manager's code, not run). The Linux `<reason>` is the system's message
  without a period, such as `No space left on device`.
  The Windows `<reason>` is the system's message with its own period,
  such as `Not enough quota is available to process this command.` in an
  enclosing job limited to one active process, so that line ends in `..`.
  On Windows a start that fails between the creation and the resume, while
  the helper's primary token is checked or the initial token is attached
  and checked, ends the suspended helper through its job first and adds
  that end's status in parentheses; a failed job assignment on the
  AppContainer and Windows 7 tiers ends the helper, which is in no job
  yet, through its process handle instead.
- **Failure limit.** A start failure and an unexpected end are helper
  failures; an end after at least 60 s of helper uptime starts a new run at
  one, and a finished idle stop resets the run. The third failure in a row
  stops launches for the rest of the process: later widgets die with `the
  widget helper failed 3 times in a row`. The end of a helper whose
  renderer stopped answering (**Hosted widgets on macOS, Linux and
  Windows** above) is such an unexpected end, one failure like a crash, under the
  same run, limit and latch. A `Launch()` that throws counts
  as one start failure, `the widget helper could not start: <what()>`. A
  helper that did not exit even
  when killed stops them the same way: `Manager::reap()` logs its pid as
  not reaped whether or not a reason is latched, and latches `the widget
  helper was not reaped` only when none is latched yet; when the helper's
  unexpected end was the third failure in a row, that end has already
  latched `the widget helper failed 3 times in a row`, which stays. A
  start failure whose `Launched` still carries a completion
  (on Windows, a helper the client could not put into its job and whose
  end its first wait did not confirm) counts as one start failure and is
  then reaped like an ended helper before any other helper starts
  (`Manager::launch()` in `tgw_remote.cpp`), so that helper too stops
  them when its end is never confirmed: `Manager::reap()` logs its pid
  as not reaped and latches `the widget helper was not reaped` only when
  no reason is latched yet; when that start was the third failure in a
  row, its own charge has already latched `the widget helper failed 3
  times in a row`, which stays. An end charges only the widgets the
  stream did not spare (**Runtime log and deaths** above), and a spared
  one that can start
  again does so in the next helper, so the same widgets can carry this
  limit: one that hangs every helper it runs in stops at its own second
  crash, before the third failure, while its spared sibling keeps
  restarting. Once a quit has stopped the helper,
  widgets die with `the widget helper was stopped`. None of these runs a
  widget anywhere else. The stop of every widget where no helper
  is available is no helper failure: no start is attempted, so nothing
  counts towards this limit or stops launches.
- **Timing.** Queries and probes cross the channel; a hosted widget's
  layout queries do not, they are answered by the renderer in the helper,
  while its renderer's backend requests cross the other way and wait for
  the client's `widget.render` thread (**Hosted widgets on macOS,
  Linux and Windows** above).
  `SandboxStats::queries` and `queryMs` are counted by the engine in the
  helper and reach the client with its stats, so each uncached query's
  time includes both channel hops and the client's answer. The host's own
  bound on a query answer stays in the client, and a theme change's
  `probe()` is a
  channel probe bounded at 10 s. A widget that starts a helper spawns it
  synchronously when its build asks for its hosted channel
  (`Tgw::HostedChannel()`), before the host's creation time
  that `loadedMs` counts from, so its `loadedMs` does not include the
  spawn itself; it can include the helper's own start-up and lockdown
  before its engine answers. On macOS the spawn includes the identity
  checks of **The shared helper**, under a certificate-signed client the
  static reading of each helper slice too, Security calls without a time
  bound of their own, and on Linux in the `TelegramHelpers` layout the read and
  SHA-256 of the whole helper file; in the Mac App Store build the start
  waits for the service's two replies, at most 15 s together, and takes
  about 10 s when launchd defers it. Inside Flatpak it is a synchronous
  portal call,
  bounded by the portal proxy's 10 s timeout and preceded, until the
  client has its proxy, by that proxy's creation, whose D-Bus calls have
  GDBus's default bound of 25 s each. The client's helper manager does
  not hold its mutex across the start: `Manager::launch()` in
  `tgw_remote.cpp` marks the helper as launching, and other widgets'
  channel requests wait for it while the main thread's manager calls do
  not (one ran 15 ms after its post during a 10 s deferred Store start).
- **Quit.** A quit stops the helper, live or idle, once no widget host,
  build, sample or theme job is left, as one more tgw job that holds the
  quit (`Tgw prevents quit, stopping widget sandboxes...`); `Tgw doesn't
  prevent quit any more.` can appear twice, before and after that job. The
  stop waits up to 1 s for the helper to exit, then kills it (on Windows
  by terminating its job, inside Flatpak through the portal, in the Mac
  App Store build by canceling its XPC connection) and waits up to 1 s
  more; an outstanding theme probe can hold the quit for its 10 s
  before that. The drain closes the manager's launch admission first,
  before it releases the views (`Runtime::Drain()` calls
  `RefuseRemoteEngines()`, `Manager::refuse()`): from then on a request
  for a hosted channel, one waiting for a helper being launched or retired
  included, gets `the widget helper was stopped` and launches nothing,
  while a launch already underway finishes. The stop itself reaches the
  stream only on the manager's reap worker (`Manager::stop()`,
  `Manager::reap()`), after `Runtime::IsQuitPrevent()`'s drain and once
  `BusyStoppingEngines()` finds no job left, and the stream keeps its
  first end
  (`Connection::endLocked()` in widget-sandbox's `remote_stream.cpp`).
  So the stream's end reads `the host stopped the stream`, the owner's
  stop, when that stop is the stream's first end; when the stream ended
  first for an answer that ran out, the end is the unanswered one of *A
  renderer that stops answering* above. The time of the quit request
  alone does not decide which. In the campaign a quit 0.044 s after the
  admission of a frame request whose helper was stopped (`SIGSTOP`) ended
  the stream with the owner's stop 0.063 s after that admission, then
  killed the helper after the 1 s exit wait (`signal 9, sent SIGKILL after
  1011 ms`), and no helper started after it.
  Since the stop waits for build jobs, a build that was already
  starting a Mac App Store service when the drain began holds the quit
  for that one start; builds waiting for it are refused at the drain.
  Measured Store quits: 0.1-0.2 s
  with a live service, 1.1 s with a stuck serve thread and 2.1 s with a
  `SIGSTOP`ped service, which launchd ended about 5 s after Telegram
  exited.
  When the client dies without a quit, the helper reads the
  end of its channel and exits by itself (the Mac App Store service also
  on its connection's end). The helper's path is not the client's, and
  `test-run`'s path-scoped kill matches only the client's executable (on
  Linux by `/proc/<pid>/exe`, on Windows by the process's
  `ExecutablePath`): it never kills a helper or lists one in
  `stragglers_killed_after`, and after it kills a quiet client the helper
  ends on the end of its channel, on Windows and Linux also as below. A
  run that must show no helper outlived it looks for the helper's own file
  itself: `Contents/Helpers/TelegramWidgetHelper` on macOS,
  `Contents/XPCServices/TelegramWidgetHelper.xpc/Contents/MacOS/TelegramWidgetHelper`
  in the Mac App Store build, a process whose `/proc/<pid>/exe` is the
  client's `TelegramHelpers/TelegramWidgetHelper` or libexec helper on
  Linux, the client's `modules\<arch>\TelegramWidgetHelper.exe` on
  Windows. On Windows the client's death also closes the helper's job,
  whose only handle the client holds, and the job's kill on close ends the
  helper even when it is not reading. On Linux the
  kernel kills a helper the client cloned when the client dies, also when
  it is not reading: the helper sets `PR_SET_PDEATHSIG` at its entry, and
  its parent is the client's spawning thread, which lives as long as the
  client. Inside Flatpak the portal kills the helper's sandbox when the
  client leaves the session bus (`WATCH_BUS`).
- **Loopback.** The framework's own Debug switch `TW_ENGINE_LOOPBACK=1`
  acts only where `twidget::SandboxEngine::make()` builds an engine in the
  client, and Telegram links no engine on any platform, so the switch has
  nothing to act on there. Leave it unset.

## Readings and refusals

| Operation | Reading or refusal |
| --- | --- |
| `InjectWidgetMessage` | Empty, `not injected: ...` (not registered, history gone or not at its bottom, list not shown, payload does not unpack) with nothing added, or `the injected message got id ..., not the armed ...`. |
| `CheckWidgetMessage` | Fills `shape`; empty, or `no HtmlWidget view: ...` (not a local outgoing client-id message, not a `Tgw::WidgetMedia` of the bytes, `its media's flags, state or widget id are not the injected ones (the media has ...)` comparing `Tgw::WidgetMedia::fields()` with the record, absent and present-empty bytes and an absent and a zero id told apart, `it comes via bot <id or none>, not the injected <id or none>` comparing `HistoryItem::viaBot()` with `fields.viaBot`, no laid-out card of the list's own). `shape` includes the record's `flags=<copy\|query\|background or none> chat=<bytes or none> public=<bytes or none> widgetId=<id or none>`: sizes, never the state's contents (`chat=0` is present and empty). |
| `RuntimeLogDecided` / `FinishRuntimeLog` | `log.source`: `requestLog`, `logText` (the runtime kept no callback), `logText-at-bound`, `unavailable`; `log.chars`, `log.pending`. |
| `log.summary` | The raw `Tgw::Runtime::_summary` (the `Summarize()` text) with `log.summaryState` and `log.summaryWork`: the key of the host that summary was sampled from (`Tgw::Runtime::_summaryHost`, the `RendererCore::workId()` of that host's core), read with the text, whether or not the summary has a `Died: ` line; 0 and empty text without a summary. |
| `RevalidateWidgetCard` | Empty, or what changed: show refused, list gone, no hover, view gone or another, card not ready, core, card or view geometry. |
| `WidgetDesktopStop` | `RendererCore::stopReason()` verbatim, or empty: `core` 0, no own runtime or no current core, another core (the older still when the watched core stopped before its first delivered picture), a runtime not `Stopped` (a stop whose posted answer has not reached the chat yet; a stop off screen that `release()` already rearmed reads `Released`), a stop that is not fatal, or a final ordinary or stalled charge counted on the tap (`fatalFrozen` above 0). Empty is no desktop stop seen, never proof that the widget is alive. |
| `FixtureBarrierDecided` | The removal wait: true once there is no tap, the tap finished (`WidgetTapFinished()` of [test_widget_ops](test_widget_ops.md#readings-and-refusals): every factory released, every wrapper destroyed and every hosted work selected for the runtime's accounting final), or `kWidgetBarrierBound` (10 s) passed since the removal. It does not say which. A caller tests `message.tap` first, since with no tap there is nothing to read: the conformance runner then counts its reading not final (`readingFinal` needs a tap), and the attack audit judges a default reading, balanced and empty, so that file did not run. With a tap it checks `WidgetTapFinished(ReadWidgetOps(*message.tap))`, a finished reading taken at or past the bound included; only when that is false has the barrier timed out with an incomplete reading, which proves no absence and which the caller reports with `WidgetTapCounts()` and stops before another injection. |
| `WidgetTapCounts` | The counts `WidgetTapFinished()` compares, for a diagnostic: `factories 1 of 2 released, wrappers 1 of 1 destroyed, hosted works 0 of 1 final`. The packs' aborts and rows and the stress-kit runner's barrier write it. |
| `WidgetLogSummaryOwn` | Whether `log`'s summary is the one of the host the request was made for (`Fresh` or `Final`, below): the only summary whose `Died: ` line and `summaryWork` may describe that host, so the only one a reader takes a cause from. |
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

Only `Fresh` and `Final` describe the host the request was made for
(`WidgetLogSummaryOwn()`). The conformance runner's
[bridge reader](test_widget_conformance.md#bridge-calls-and-uncaught-errors)
refuses the other two by name. An `Absent` summary has no text and work 0.
A `Stale` one may hold an older host's `Died: ` line and work: both packs'
hosted death readings keep that line and `summaryWork` in their rows as
diagnostics and take no cause from either, so a charge then reads
`charged, reason unmeasured`
([test_widget_ops](test_widget_ops.md#hosted-deaths-and-final-charges)),
refused by name only for a conformance entry with `stops`
([test_widget_conformance](test_widget_conformance.md#readings-and-refusals)).
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

`RemoveFixtureMessage()` hides a shown layer, disarms the record's tap and
host watch, destroys the message by id and then fires the history change
the removal parks (`HistoryItem::removeMainView()` calls
`notifyHistoryChangeDelayed()`, which only
`Data::Session::sendHistoryChangeNotifications()` fires), as after an
injection and as the product's deletion paths do
(`delete_messages_box.cpp`, `api_updates.cpp`);
`HistoryWidget::handleHistoryChange()` recounts the list in that call, so
the next reading has no row and no height for the removed message. Before,
the list kept the removed card's height until some later trigger, for
seconds. Call it from a stage's run, never from an until. The fixture's
release still destroys its messages with its own loop.

## Verification

Executed: the macOS arm64 Debug build of the moved module and its attack
caller, and a comparison of every moved body with the attack pack before
the move, after its renames (the conformance task's evidence records the
command and its output). The attack audit's own self-check
stage, its three verdict controls and three accepted regression files
(`activation-animation-events.html`, `api-fetch.html`,
`fallback-at-boot.html`) ran on the shared fixture in the first launch of
the macOS arm64 Debug campaign of
`2026/10/24/run-the-widget-conformance-sets-through-the-chat-list-and-report`
(its `work/test.md`, Attempt 1, Run 2, Tests 1, 2 and 7). The self-checks
passed, all three files' rows matched the audit's accepted launch, and
the 66 attack stage and Check names matched in order. The only N/A was
the `not-live` click show for `fallback-at-boot.html`.
The conformance runner ([test_widget_conformance](test_widget_conformance.md))
also exercised the fixture's summary reading and text injection in
that campaign. The merged run file had `observed.bridge` for all 120
runs, with 16 calls total and no `host` judgement for a refused bridge
reading (Run 3, Test 3). The runner injected four text fillers for
`auto/20-client-behaviour` in the light launch (Run 2, Test 4), before
the media-field campaign of
`2026/10/26/move-widgets-to-widget-sandbox-v0-2-2-and-give-them-their-state-and-message-context`.
The media-field parameter
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
`WidgetDesktopStop()` ran through both runners in the Windows Win32 Debug
client at DPR 2 in the evidence campaign of
`2026/10/31/stop-a-widget-on-its-last-picture-when-the-paint-bounds-refuse-its-frames`
(its `work/test.md`, Attempt 1). In the stress-kit runner (Runs 1 to 3,
Tests 2, 5 and 8), the stopped entries recorded their own
`Paint limit exceeded:` reasons in `dead`, naming filter or path work.
In the conformance smoke (Run 8, Test 21), the paint-stop entry's `dead`
carried the desktop's filter-work reason and the host counted-cap
entry kept its own `Text limit exceeded:` reason.
The [one-column window](#one-column-window) (its reading, target, move and
settle, moved from the stress-kit runner, and
`AppendWidgetFixtureOneColumn()`) ran in the Windows Win32 Debug client
in the evidence campaign of
`2026/10/31/put-the-conformance-runners-window-in-the-one-column-layout`
(its `work/test.md`, Attempt 1, and `evidence/index.md`). In Runs 1 and
5 the conformance runner put the window in one column before its first
injection, from a normal window at the two-column threshold and an
inactive maximized window, respectively. Both cores in each smoke measured
390 CSS px without a runner refusal. The maximized window stayed inactive
through teardown and ended normal. The stress-kit smoke confirmed
placement on the screen with the highest refresh rate, in one column
and on top (Run 3). Before the
move the same reading, target and settle ran inside the stress-kit
runner's Windows passes ([test_widget_stress](test_widget_stress.md#verification)).

The runs below that name `in-process` or `helper-engines`, or a helper
that served engines alone, predate
`2026/11/14/build-release-telegram-without-the-in-process-widget-renderer`,
which removed both values and every way to run widget code in Telegram.

The [remote engines](#remote-engines) first ran in the same-binary macOS
helper, the client's own `Contents/MacOS/Telegram -widgethelper 3` that
`TelegramWidgetHelper` later replaced, then still unsandboxed, in the
macOS arm64 Debug client in the evidence campaign of
`2026/11/13/run-widget-engines-in-a-shared-unsandboxed-helper-on-macos-behind-a-debug-switch`
(its `work/test.md`). The attack audit over all 133 corpus files and the
conformance runner over the 133-run automatic matrix ran once with
`TDESKTOP_WIDGET_ENGINES=helper` and once with `in-process` on the same
executable, with the host watch, the validated-ops tap and the played bot
in use (Runs 9 and 11): 0 leaks in both modes, identical conformance
verdicts, and identical attack rows apart from two timing-dependent
outcomes, whether the click show of `request-copy.html`'s growing card
settled in time (refused once in process, accepted in every other launch)
and the box count in `paint-backdrop-replay.html`'s paint-limit reason
(300 through the helper, 100 in process, the same stop). The stress kit's
`layout-queries/000016.html` with its measure options took 1.075 and
1.081 ms per uncached query through the helper against 0.964 and 0.968 ms
in process in that Debug build (Run 11). The lifecycle (one shared
helper, the idle exit, a SIGKILL and the restart, the failure limit and
the quit) ran through a disposable overlay in that campaign (Runs 4-5 and
7). None of it has run on Linux; the Windows runs are below.

The same-binary macOS helper, sandboxed, ran in the macOS arm64 Debug
client in the evidence campaign of
`2026/11/10/run-widget-engines-in-a-sandboxed-helper-process-on-macos`
(its `work/test.md`). The attack audit over all 133 corpus files and the
conformance runner over the 133-run automatic matrix ran once with no
widget switch, in the sandboxed helper, and once with `in-process` on the
same executable, with the host watch, the validated-ops tap and the played
bot in use (Runs 5-7): 0 leaks in both modes, the same conformance
verdicts, and the same attack rows apart from the same two
timing-dependent outcomes (the `request-copy.html` show flipped once in
each mode in a paired rerun; `paint-backdrop-replay.html`'s box count was
again 300 through the helper and 100 in process); every default partition
ran in one locked helper. With no switch the lifecycle ran through a
disposable overlay (Run 5): one helper, locked before its widgets loaded,
its idle exit 30 s after the last release, and a fresh locked helper for
the next widget. A SIGKILL stopped both cards on their last pictures with
every pixel outside the stop pill unchanged, re-entry restarted them in a
fresh helper locked before they loaded, and a second kill reached the
crash limit. A real lockdown failure, a client that had sandboxed itself
with an `(allow default)` profile, made every helper refuse with
`sandbox_init failed: Operation not permitted` before its widgets loaded,
and after the third the next widget died with `the widget helper failed 3
times in a row` without a fourth helper (Run 3). None of this has run on
Windows, Linux, the store build or a packaged build: the store build then
kept its engines in process, and packaged builds were then excluded from
the macOS helper default.

`TelegramWidgetHelper` ran in the macOS 27.0.1 arm64 Debug client in the
evidence campaign of
`2026/11/14/run-macos-widget-engines-in-a-qt-free-helper-executable` (its
`work/test.md`), with no widget switch unless one is named. One helper
started at the first widget's demand and served both widgets of the
fixture: `Contents/Helpers/TelegramWidgetHelper`, a child of the client,
with argv `[<path>, 3]`, no environment variable, the working directory
`/`, default signal actions, descriptors 0 and 1 `/dev/null`, 2 the
client's stderr and 3 the channel socket and no other, read locked by a
parent-side `sandbox_check` before each widget loaded. The engines'
`widget.sandbox` threads ran in it and none in the client, and the notes
read `Started, its engine is remote.`. It exited 30 s after the last
release, and the next widget started a fresh, locked helper (Run 6
Test 3). A SIGKILL stopped both cards on their last pictures with no
pixel outside the stop pill changed, re-entry restarted them in a fresh
locked helper, a second kill reached the crash limit, and a third kill,
of a fresh widget's helper, latched the helper for the process (Run 6
Tests 4-6). A quit with live widgets stopped the helper 61 ms after the
request, before the client exited; when `test-run` killed a quiet
client, it listed only the client in
`stragglers_killed_after`, and the helper ended within the same 116 ms
sample of an external sampler (Run 6 Tests 7-8). `in-process` started no
helper (Run 6 Test 9). A client that had sandboxed itself with an `(allow
default)` profile made three fresh helpers refuse with `sandbox_init
failed: Operation not permitted` before their widgets loaded, and the next
widget died latched without a fourth helper (Run 6 Test 2); an APFS clone
of the bundle without the helper file failed three starts with
`posix_spawn failed: No such file or directory`, started no helper and
ran no engine in process (Run 5 Test 3). The attack audit over all 133
corpus files and the conformance runner over the 133-run automatic matrix
ran with no switch and with `in-process` on one executable (Runs 9-11):
every default partition ran in one locked helper, 0 leaks in both modes,
identical conformance verdicts, and identical attack rows apart from
`paint-backdrop-replay.html`'s box count, 300 through the helper and 100
in process in four pairs with the same stop and op stream. The probe's
runs are on [test_widget_helper_probe](test_widget_helper_probe.md#verification).
Outside the app (Runs 1-3), the helper built alone, before Telegram, from
the two widget archives and its own sources with no precompiled header,
and Telegram's build staged it; the Debug and Release helpers of the
official CLT 26.6 and SDK 26.5 targets (x86_64 at 10.13, arm64 at 11.0,
`-Werror`) imported only `libc++.1` and `libSystem.B`, the Release one
without the probe; the Release argument gate, and a `Ready` only after
the lockdown, held natively on arm64 and on x86_64 under Rosetta; a store
build of that time, before the Mac App Store service, compiled and linked
with no helper target, file or `Helpers` folder; and the helper's CMake stage built in packaged mode
(`DESKTOP_APP_USE_PACKAGED`) beside a placeholder Telegram target, no
packaged Qt being installed. Not run: a packaged or store app, a Release
client, widgets on x86_64, Linux and any macOS but 27.0.1.

The signed start and its identity checks ran on macOS 27.0.1 arm64 in the
evidence campaign of
`2026/11/14/sign-and-verify-the-macos-widget-helper-executable` (its
`work/test.md`, Runs 1-3), with self-signed stand-in identities in place
of the Developer ID: `build.sh`'s steps and `sign_mac.py` gave the
universal app and both thinned update apps a helper signed by stand-in A
with the hardened runtime, its identifier and no entitlement; that client
served a card from its own helper, its own responsible process, while a
helper signed by another identity, an ad hoc helper and one with another
identifier were each killed in the suspended check before they ran, and
replacing the running client's main executable refused before any spawn.
The ad hoc Ninja arm64 and `build_mac.py` universal Debug clients each
served from their own helper and refused the helper of another build of
the same version at the build identity check, and under Rosetta the
universal client's x86_64 slice and the unsigned Ninja x86_64 client each
served from the helper's x86_64 slice with its recorded CDHash. A helper
file without read permission failed the check three times
(`SecCodeCopyGuestWithAttributes`, 100013) with no engine in process,
and the next widget died latched without a fourth start; not run: a
Developer ID signature or notarization, a certificate-signed Xcode build,
a Release client, the store and packaged builds and macOS before 27.0.1.

The stop of every widget where no sandboxed helper is available ran on
macOS 27.0.1 arm64 in the evidence campaign of
`2026/11/14/stop-widgets-in-builds-without-a-sandboxed-helper` (its
`work/test.md`, Run 1), in a copy of the `OS_MAC_STORE` Debug build of
that time, before the Mac App Store service replaced its stop,
re-signed ad hoc with no entitlement, so outside the App Sandbox and able
to read a copied portable account, and the unchanged helper default
ran in the direct-download Debug client. With no switch the store
copy stopped its two fixture cards at their start: each host watch's
`dead` and each summary's `Died:` line read
`no sandboxed widget helper is available here: the widget helper is not
available in the Store build`, the notes had `Started, its engine does not
run.`, the headers `Crashes in a row: 1 of 2.`, and `loadedHosts`,
`batches` and `ops` stayed 0; a sampler reading the client's threads and
children at most 3.6 ms apart saw no `widget.sandbox`, `widget.reader`,
`widget.writer` or `widget.monitor` thread and no child process while it
saw the refused hosts' `widget.client`, `widget.validator` and
`twidget-host`. The main log had one `Tgw` line, `Tgw: Widget engines do
not run, no sandboxed widget helper is available here: the widget helper
is not available in the Store build.`, and no `Tgw Error`. With
`in-process` the same copy loaded its card with the note `Started.` and a
`widget.sandbox` thread in the client, the positive control of that
sampler; with `helper` its card died with `the widget helper could not
start: the widget helper is not available in the Store build`, one
`Tgw Error` line for its one host, no `widget.sandbox` thread and no child
process. With no switch the direct-download client loaded its card through
its bundle's `TelegramWidgetHelper` child, with `Started, its engine is
remote.`, no `widget.sandbox` thread in the client and the main log's
`Tgw: Widget engines run in the widget helper.`. The `unifdef -U_DEBUG`
view of `tgw_remote.cpp`, `Q_OS_WIN` left unresolved, read no
`TDESKTOP_WIDGET_ENGINES` and no `in-process`, chose only the helper or no
engine and installed a channel factory for both, the only
`setRemoteChannelFactory` calls in `Telegram/SourceFiles`. An unsandboxed
store build keeps its single-instance socket in the TCC-guarded
`~/Documents` (`SingleInstanceLocalServerName()`), so the store legs set
`CFFIXED_USER_HOME` to an empty run-owned folder with a short path, since
a socket path must fit in 104 bytes; the socket, the downloads and
`Library/Sounds` went there, but `cfprefsd` still wrote
`~/Library/Preferences/org.telegram.desktop.plist`, removed afterwards.
Not run: the Windows Store build, Windows before 10 1809, Linux on other
architectures and the non-Linux all-other platforms, whose reasons were
only read, any Windows or Linux compile, a store build under its App
Sandbox, a Release client (the non-Debug rule was only preprocessed) and
the `probe` and `probe-unsandboxed` values.

The Mac App Store service ran on macOS 27.0.1 (26A434) arm64 in the
evidence campaign of
`2026/11/14/run-mac-app-store-widget-engines-in-an-xpc-service-helper`
(its `work/test.md`), in App-Sandboxed copies of the Store Xcode Debug
product (arm64, deployment target 12.0) made as **Mac App Store test runs**
describes, signed through `sign_mac.py` by self-signed stand-ins, with a
disposable overlay. With no switch the fixture's card loaded in one
service under the copy's `XPCServices`, a child of launchd read
App-Sandboxed in every sample, signed by the stand-in with the App Sandbox
only; Telegram had no `widget.sandbox` thread and no child, the logged
requirement named the service's identifier and the stand-in's leaf, which
XPC accepted, stderr held exactly one `sandbox initialization failed`
line, the card rendered, the service's running threads read priority 31
through a 30 s hold, and its responsible process was Telegram (Runs 12 and
31); after the lockdown repair it held only descriptors 0 to 3 (Run 31).
`in-process` on the same copy started no service. A copy whose service
another stand-in signed and an ad hoc copy whose service did not match the
pin failed three starts each with `failed the signer check: Peer
Forbidden` and `failed the build identity check: Peer Forbidden`, their
services received only the hello (markers the overlay posted from inside
them) and never Telegram's stderr, and the next widget died latched; a
copy without the service failed with `is unavailable (Connection invalid
(Connection init failed at lookup with error 3 - No such process))`;
`probe` refused three services, and a `Launch()` that threw once was one
counted failure before the next widget's service served (Run 14). A
SIGKILL 12.8 s into a service's life ended its stream with `the engine
process closed the stream`, and re-entry served the card from a fresh
service started in 25 ms; a second kill reached `Crashes in a row: 2 of
2.` without a latch; a start right after that kill, 0.74 s after the
killed service had started, took 10044 ms while a posted main-thread
manager call ran 15 ms after its post; the idle stop
came 29.96 s after the last release; and quits were bounded as **Quit**
says, a stuck serve thread ending with `ended after its connection was
canceled after 1002 ms` and a `SIGSTOP`ped service with `did not end
within 1002 ms of its connection being canceled` and `Tgw Error: The
widget helper <pid> was not reaped, it is not started again.` (Run 32).
The direct helper still started and restarted in the direct-download
client. The attack audit over all 133 corpus files and the conformance
runner over the 133-run automatic matrix ran with no switch and with
`in-process` on one copy (Runs 33-36): every default launch ran in one
App-Sandboxed service, 0 leaks in both modes, 0 semantic differences in
conformance, and identical attack rows apart from
`paint-backdrop-replay.html`'s box count, 300 in the service and 100 in
process in three pairs with the same op stream. Outside the app (Runs
1-3): the Xcode Store service was `org.telegram.desktop.WidgetHelper`, ad
hoc with the hardened runtime and the App Sandbox only, its CDHash equal
to the pin and unchanged by an unchanged rebuild, every other nested
identity as before; the official CLT 26.6 and SDK 26.5 Ninja Store trees
(x86_64 at 10.13, arm64 at 11.0, `-Werror`) built the service and the
changed Telegram objects, rejected an unguarded macOS 12 XPC call, linked
the Release service with libSystem and libc++ only and no private name,
and merged the two slices into a universal service with both pins;
`build_mac.py`'s and `build.sh`'s Store lines (the latter with a stand-in)
signed the service as above and passed `codesign --verify --deep
--strict`. Once the lockdown cut every new Mach lookup, the evidence
campaign of
`2026/11/16/cut-new-mach-lookups-from-the-mac-app-store-widget-service-after-its-lockdown`
(its `work/test.md`, Runs 13-16) reran the serve leg, the kill leg and
the attack and conformance comparison on a copy made the same way: card
A served for 30 s with its engine remote from a service with one
`sandbox initialization failed` line, no `Widget helper:` refusal,
descriptors 0 to 3 and priority 31; a kill 12.8 s in was followed by a
fresh service in 28 ms, a second kill by `Crashes in a row: 2 of 2.`
without a latch, the idle stop 30.15 s after the last release and
Telegram's exit within 0.13 s of the quit; and against `in-process` 132
of the 133 attack files gave identical rows (the replay file's box count
again 300 in the service and 100 in process), the 133 conformance runs 0
semantic, verdict and judgement differences, with 0 leaks and every
default launch in one service that never refused.
Not run: macOS before 27.0.1, so the service on macOS 12-26
and the below-12 stop; Apple's Mac App Store re-signing and review, under
which the leaf is Apple's, shared by every Mac App Store app, so the
signer clause binds the identifier and that leaf rather than Telegram's
own; a trusted Apple leaf or a Team ID; a Release client; x86_64 at run
time; a Screen Recording grant; `build.sh`'s `productbuild`.

The Mac App Store service's relay (**The shared helper** above) ran on
macOS 27.0.1 (26A434) arm64 in the evidence campaign of
`2026/11/16/give-the-mac-app-store-widget-service-a-descriptor-2-that-is-not-telegrams-terminal`
(its `work/test.md`, Runs 3-5), in a copy of the Store Xcode Debug
product made as above, with a disposable overlay that logged `Overlay:
relay end pid=<pid> relay=<yes|no> reaped=<0|1> drainMs=<n|none>` for
each completion. Telegram's stderr was `app_stderr.txt` (Run 3) or the
slave of a raw pseudo-terminal whose master a driver outside the App
Sandbox read continuously into a capture, from before the launch until
Telegram exited (Runs 4 and 5); `lsof` ran in that driver, never in
Telegram. The App-Sandboxed Telegram Lite read its own `isatty(2)` as 1
under the pseudo-terminal and 0 under the file, where every completion
read `relay=no`. Under the terminal `lsof` read Telegram's fd 2 as the
slave and the fd 2 of both services, the one killed below and the fresh
one, as `unix`, neither as a character device; the killed one's, read on
both sides while it lived, was paired both ways with a Telegram socket
other than the channel's at its fd 3 (Run 5). Three services that each
wrote, right before `_Exit(1)`, a Debug refusal line holding ESC, BEL,
BS, CR, DEL, `\` and U+009B put it on the terminal once, escaped
(`\x1b[31mRED\x1b[0m\x1b]0;TASK-TITLE\x07\x08\x0d\x7f\\\xc2\x9b`), and no
line of the capture held a byte outside 0x20 to 0x7e (Run 4); with the
file the same lines landed raw (Run 3). Every reaped completion under the
terminal, five in all, read `relay=yes reaped=1 drainMs=0`: the drain
returned at once, so nothing queued was discarded and nothing else held
the service's end. A service killed with `SIGKILL` logged `stream ended:
the engine process closed the stream.`, its relay's ending and `ended,
ended.` in that order, and 3.3 s later Telegram held neither that
relay's socket nor the channel's; re-entry started a fresh service in
21 ms with a relay at its fd 2, and the card was live again (Run 5). A
stopped service at the quit ended `did not end within 1002 ms of its
connection being canceled` with `relay=yes reaped=0 drainMs=none`, its
relay ending in the same millisecond, and Telegram exited 2.01-2.13 s
after the quit, as without a relay (Run 5). `lsof` showed the fresh
service's sockets at the killed one's addresses, so such addresses pair
only while both ends live. The probe's readings of that descriptor are
on [test_widget_helper_probe](test_widget_helper_probe.md#verification).
Not run: a terminal emulator, a stopped terminal or an unread master, a
relay that could not be made and a start that failed after its relay was
made (both read from the code only), and any macOS but 27.0.1 arm64.

On Windows the [remote engines](#remote-engines) first ran in the
same-binary helper, the client's own `Telegram.exe -widgethelper <handle>`
that `TelegramWidgetHelper.exe` later replaced, in the Win32 Debug
(x86) client on one Windows 11 Pro 10.0.26300 machine in the evidence
campaign of
`2026/11/14/run-widget-engines-in-a-restricted-helper-on-windows-behind-a-debug-switch`
(its `work/test.md`, Runs 1-3). With `helper` one `-widgethelper` child of
the client served every widget; after its lowering it was Low, restricted
to `S-1-0-0` with its user and every group but the integrity label
deny-only and only `SeChangeNotifyPrivilege`, without a thread token, the
only member of its job (limits `0x2408`, UI `0xFF`), with every expected
mitigation set and only `app_stderr.txt` inheritable, and its `Date`,
random bytes and `Intl` matched the in-process values. With `probe` its
`CreateProcessW`, three directories and `HKEY_CURRENT_USER\Software` read
`denied (error 5)` after passing `before lowering:` controls, the socket
and the connect to the live listener read `ALLOWED` and `CONNECTED`, and
win32k read `not applied (SetProcessMitigationPolicy(4) failed: error 19)`.
With the Windows default still off then, unset and `in-process` started
no helper. A killed helper left both cards' last pictures unchanged
outside the stop pill; showing them again restarted both in one new
helper, and a second kill stopped them at the two-crash limit. A quit
left no helper, live or idle; with a client write blocked on
a suspended helper the helper ended with `exit 1067, terminated after 1000
ms` and the client exited 1.3 s after the quit. A client in a job of one
active process failed three starts with `CreateProcessAsUserW failed: Not
enough quota is available to process this command.`, none in process, and
the next widget died with `the widget helper failed 3 times in a row`; a
suspended helper ended 12 ms after its client was killed (Run 2). A scratch
driver launching the real executable with the product's channel, tokens,
job and policies read the initial impersonation token of the suspended
helper before its resume, and its launches with a Medium token, a Low
unrestricted token, the user as a second restricting SID, no job, or
dynamic code allowed each refused before Ready at its own check, while the
full confinement reached Ready (Run 1). The attack audit over all 133
corpus files in a basic group and the conformance runner over the 133-run
automatic matrix in a private chat and that group gave 0 leaks and the same
keyed rows and verdicts with `helper` and with `in-process` on one
executable, apart from `request-copy.html`'s click show, refused once in
process and accepted in both modes of a paired rerun (Run 3). Runs 1-3 ran
on this task's code before it adopted the lockdown seam of
`2026/11/10/run-widget-engines-in-a-sandboxed-helper-process-on-macos`:
the helper then chose its probe from the `TDESKTOP_WIDGET_ENGINES` value it
inherited from the client, its command line carried only the handle, and
the framework pin was this task's own lockdown-callback commit. On the
adopted code the affected checks were rerun as Attempt 2 Runs 1-2 of that
`work/test.md`: the non-Debug and Release-harness preprocessing held no
switch, probe argument or harness call, the seven driver launches refused
or reached Ready as before, and the landed lockdown test passed natively
and failed when `Serving::run` no longer called the lockdown (Run 1); the
ordinary helper's command line ended at its handle, `probe` arrived as
`probe <working dir> <data dir>` after it, reported the same denials,
`ALLOWED`/`CONNECTED` and error 19 and went on serving widgets, unset and
`in-process` started no helper, and `probe-unsandboxed` started one helper
that refused with `the widget helper probe is not available on this
platform` before any widget loaded (Run 2). Not run:
x64, ARM64, the Store build, Windows before 10 1809, a Release runtime
(the non-Debug gate was only preprocessed) and macOS or Linux compilation
of the shared files.

The [Windows lowbox](#remote-engines), its network denials and the
Windows default then ran in the same-binary helper on the same machine,
Win32 Debug (x86 on WOW64) on
Windows 11 Pro 10.0.26300.9711 with the firewall services running, every
profile on and no loopback exemption for the package, in the evidence
campaign of
`2026/11/14/confine-the-windows-widget-helpers-network-with-lpac-and-turn-it-on`
(its `work/test.md`): Attempt 1 Runs 1-5 before the switch was turned
on, Attempt 2 Run 1 after it. A scratch driver that built the channel,
tokens, job and policies with the product's functions, mirrored its
launch and checks and started the real executable's `-widgethelper`
entry read the suspended child before its resume (Attempt 1 Run 1). Its
actual primary was Low, restricted to `S-1-0-0`, with its user and every
group but the integrity label deny-only and only
`SeChangeNotifyPrivilege`, and an LPAC token (`WIN://NOALLAPPPKG` 1) of
the package SID the driver derived itself, with no capability; its
attached startup token was a regular AppContainer token of the same
package at the impersonation level, with no capability. Only the pipe
and the stderr file were inherited, Ready came 50 ms after the Opening,
and no thread held a token after it. Six defective primaries (no lowbox,
both tokens boxed for one other package, no LPAC attribute, the
`internetClient` or the `registryRead` capability, the user as a second
restricting SID) were each refused by the parent's check of the actual
child token, never resumed and ended through the job with `exit 1067`;
resumed on purpose, the real helper refused each before Ready with `the
widget helper is not sandboxed:` and the same reason. Five defective
startup tokens (no lowbox, another package, a capability, an LPAC token,
an identification-level token) were refused on the attached token before
the resume. A helper created outside the product's job and one created
without the dynamic-code policy refused before Ready (`the job's limits
are 0x2000 for 0 processes`, `the mitigation policy 2 reads 0x0`); a
creation that set the signature policy always off built no defect, since
the helper still enabled it itself. A wrong-build Opening was refused as
a protocol error, not by the lockdown. The launcher, channel and `tgw`
sources and the framework pin were unchanged since the task above, so
its evidence for the entry, the wire, the pipe and the lockdown callback
carries.

On the network (Attempt 1 Runs 1-2), Winsock's own
`\Device\Afd\Endpoint` call, captured in ordinary x86 and x64 processes,
passed a 57-byte attribute whose first 48 bytes equal the probe's, and
the probe's bytes opened there. In every probe helper the four socket
kinds and their AFD endpoints succeeded before the lowering and were
refused after it, with `WSAEACCES` and `STATUS_ACCESS_DENIED`, and the
native probe's self-listener read `ok`. TCP and UDP listeners of the
driver on 127.0.0.1 and `::1` and of a sibling process on the same
addresses (Run 1), and of the client on those, on the host's own LAN
address and on a scoped link-local IPv6 address, with one in the running
WSL distribution, a separate network stack on the same machine (Run 2),
received every ordinary control sent to them before, during and after
the helper, by the driver in Run 1 and by the client and a sibling
process in Run 2, and no helper marker; after the lowering every target
the helper was given read `socket creation was denied`. Sockets that
`connect-control` preopened before the lowering delivered nothing after
it: TCP timed out towards the host's own addresses and was refused with
`WSAEACCES` towards WSL, and no UDP send was received. That is recorded
as delivering nothing, not as a denial; the ordinary helper holds no
socket. Both actual tokens had zero capabilities, and the exemption list
held entries but none for the package.

Through the product `Launch` with `helper` (Attempt 1 Run 2) one helper
served two static cards and a widget reading `Date`, random bytes and
`Intl`. Read from outside, its primary had the shape above, none of its
seven threads held a token, it was its job's only member (limits
`0x2408`, UI `0xFF`) with every expected mitigation read back, its
64-bit view held 35 modules, its own image and system DLLs only
(`userenv.dll` and `profext.dll` from the package-SID derivation, no
`WS2_32.dll`), and only `app_stderr.txt` was inheritable. The widget's
values were those of the `in-process` control, and each runtime's thread
started after the lowering. With `probe`, `CreateProcessW`, the three
directories and both registry keys read `denied (error 5)` while the
client opened the same paths in the same run, the probe's readbacks
matched the outside reading, and the win32k disable read `not applied
(SetProcessMitigationPolicy(4) failed: error 5)`. The helper's handles
after Ready are the ones **What the Windows lowering leaves** lists.

In ordinary helper mode (Attempt 1 Run 3) a killed helper stopped both
cards on their last pictures, with no pixel outside the stop pill
changed; showing them again started one new helper with the same token,
and a second kill reached the two-crash limit with no third start. A
quit left no helper, with a live helper (the client exited 300 ms after
the request), an idle one (273 ms) and a suspended one with a client
write pending past both 64 KiB pipe quotas (`exit 1067, terminated after
1000 ms`, the client 1314 ms after the request; without the quit the
10 s write-stall monitor ended it). A client in a job of one active
process failed three starts with `CreateProcessAsUserW failed: Not
enough quota is available to process this command.`, none in process,
and the next widget died with `the widget helper failed 3 times in a
row`; a suspended helper ended 12 ms after its client was killed, while
the observer held no handle to its job. Two copies of the client running
at once each had their own helper, single-member job and pipe under the
same package SID, and stopping either, the first-started by its quit or
the later-started by a kill, left the other's helper serving. Windows
Defender's real-time protection stayed on, with no Defender or
CodeIntegrity event in the window; the helpers mapped only their own
image and Microsoft-signed system DLLs, and the one injected third-party
module observed, `nvspcap.dll`, was in the client only.

Account-free copied clients with fresh ordinary permissions (Attempt 1
Run 4) started their own image as the helper through the product
`Launch`, reached Ready with the token above (a loaded runtime, and `42`
from the helper's JS engine for `6*7`) and ended `exit 0`: `setup.iss`'s
three files for this build in a new per-user folder under `%APPDATA%`,
that executable after an updater-style `CopyFile` over it (which kept
the file and its descriptor), `build.bat`'s portable ZIP (`Telegram.exe`
and `d3dcompiler_47.dll`, no `Updater.exe`) extracted with
`Expand-Archive`, that folder moved whole to another path, and a copy on
the WSL distribution's 9P share. With one ACE denying its user
`FILE_EXECUTE` on the running copy's executable, `Launch` returned
`CreateProcessAsUserW failed: Access is denied.`, the engine Tgw's
factory made died with `the widget helper could not start: ...` before
loading, and no child process ran; the byte-exact restore brought both
back in the same client. No AppContainer-family ACE was added or present
in the layouts, their data roots or the folders around them, no package
profile, mapping, storage key or named-object directory appeared, and
the task changed no setup, packaging or CMake file.

The attack audit over all 133 corpus files in a basic group and the
conformance runner over the 133-run automatic matrix in a private chat
and that group, in 26 launches of one executable (Attempt 1 Run 5),
gave 0 leaks and the same keyed rows with `helper` and `in-process`, the
same 15 stops included, and the same conformance verdicts, stages and
report rows, with the same baseline failures in both modes; five check
details and one transition crop differed by timing, which focused paired
reruns decided. Every helper launch ran its engines in exactly one
helper, read from outside after the pack with the token above.

After the switch was turned on (Attempt 2 Run 1), a client with no
`TDESKTOP_WIDGET_ENGINES` in its environment, as the client itself read
it, logged `Tgw: Widget engines run in the widget helper.` and served
its widgets in one helper with the token, threads, job, mitigations,
modules and inheritance above, and no line named the variable;
`in-process` on the same executable started no helper and gave the
control values. With the variable unset, an enclosing one-process job
made three starts fail with the quota error above: the widgets stopped
with `the widget helper could not start: ...` without loading, nothing
ran in process, the latch held after the limit was lifted, and a fresh
client then started its helper again. Preprocessed with the non-Debug
and the Release-harness definitions, `tgw_remote.cpp` read no
environment, made no probe call and opened the entry and chose the
helper only through `Enabled()`, which is `UnsupportedReason().isEmpty()`
in every variant. The Store and pre-1809 refusals in
`UnsupportedReason()` are unchanged since the task above, and
preprocessing with `OS_WIN_STORE` turns `IsWindowsStoreBuild()` true.

With the same-binary helper the Windows default was measured only in
Win32 (x86 on WOW64) Debug builds on Windows 11 build 26300. Not run with
it: x64, ARM64, a Release runtime (the non-Debug selection was only
preprocessed), the Store build, Windows before build 26300 (the 1809
floor was read and preprocessed, never run), a target on another
machine, socket families and types, and their AFD endpoints, other than
`AF_INET` and `AF_INET6` with TCP and UDP, name resolution, ICMP and
other protocols, a stopped or disabled firewall service, which layer
refuses the sockets and AFD endpoints (AFD's check of the token or the
filtering platform), a downloaded portable ZIP with its mark of the web,
and macOS or Linux compilation of the changed `test_widget_helper_probe`
sources. win32k system calls stayed enabled in it (the probe's disable
read error 5 under the lowbox), and `TokenIsLessPrivilegedAppContainer`
is unreadable on this build (error 87), so the less privileged level is
read from `WIN://NOALLAPPPKG`.

`TelegramWidgetHelper.exe` then replaced the same-binary helper and ran on
the same machine, Windows 11 build 26300 (10.0.26300.9711, 26H2) on x64,
where `IsUserCetAvailableInEnvironment(USER_CET_ENVIRONMENT_WIN32_PROCESS)`
read 1 in an x64 process and 0 in a WOW64 one and no system or
image-name mitigation override existed, in the evidence campaign of
`2026/11/15/move-the-windows-widget-helper-into-a-qt-free-executable-and-run-it-on-x64`;
its `work/test.md` (Attempt 1, Runs 1-4) is the record, and its
`work/test-overlay.patch` keeps the disposable scenario. It ran, with the
QuickJS engine, in the Win32 Debug build (x86 on WOW64) and in an x64
Debug build of the same commit, and no reading is carried from one
architecture to the other.
The probe's lines are on
[test_widget_helper_probe](test_widget_helper_probe.md#verification).

On both architectures a native driver built the channel, tokens, job and
creation attributes with the product's functions and started the
committed images (Run 1). Both were built before Telegram, each into its
build's `modules\x86` or `modules\x64` folder, without a precompiled
header or a Qt input, as Windows GUI images entered through
`mainCRTStartup` with `/DEPENDENTLOADFLAG:0x800`, Dynamic base and NX,
and on x64 High Entropy VA but no CET-compatible flag; each of these
QuickJS-era images imported only ADVAPI32 and KERNEL32 and delay-loaded
`bcrypt.dll` and the Debug
`WS2_32.dll`, whose 19 functions only the probe's object referenced. A
product launch read every
creation policy back from the suspended child before its resume (x86 DEP
0x3, ASLR 0xB, dynamic code 0x1, strict handles 0x3, win32k 0x5,
extension points 0x1, signature 0x5, font disable 0x1, image load 0x4 and
child process 0x1; x64 the same with ASLR 0xF and shadow stack 0x105, no
DEP being expected), reached Ready in 3-6 ms with no expectation missing,
and held the handles and modules of **What the Windows lowering leaves**
there. A creation without one adopted flag, or for strict handles with
its always-off value, read that policy off on the suspended child and was
refused at its readback: `the mitigation policy 3 reads 0x0` (strict
handles), `8 reads 0x0` (signature), `9 reads 0x0` (font disable), `10
reads 0x0` (image load) and `4 reads 0x4` (win32k). Strict handles merely
left out still read 0x3, as the lowbox sets them, and reached Ready.
`Telegram.exe -widgethelper <handle> -many -workdir <empty folder>`, given
the same valid opening on an inherited pipe, started an ordinary client
that wrote its own data folder and sent no frame within 5 s, while the
helper answered `Ready` to that opening.

On x86 alone the same driver (Run 1) made a helper created outside the
job, without the lowbox, without the LPAC attribute, with the user as a
second restricting SID, without the dynamic-code policy or without the
win32k flag refuse before Ready with `the job's limits are 0x2000 for 0
processes`, `the token is not an AppContainer token`, `the token is not a
less privileged AppContainer token`, `the token's restricting SIDs are
not exactly S-1-0-0`, `the mitigation policy 2 reads 0x0` and `the
mitigation policy 4 reads 0x4`. The parent's own check abandoned the
three token defects before the resume (`the process token check failed:
...`), and a startup token without the lowbox or of another package as
well (`the attached token check failed: the token's impersonation level
is 1`). With no argument the helper wrote `Widget helper: unexpected
arguments`; an empty token, a leading zero, an overflow, `-1`, each
pseudo-handle value, trailing text and a third inherited file handle got
`Widget helper: the channel could not be adopted`, each exit 1 with no
frame. A token naming the helper's own inherited stderr copy sent no frame
either but ended `exit 0xC0000008` with no diagnostic: the rejected handle
is closed, and the refusal's write then meets it under strict handle
checks.

The x86 client `out\Debug\Telegram.exe` then ran with a Test account and
local widget messages (Runs 2-3b). With no switch one helper,
`out\Debug\modules\x86\TelegramWidgetHelper.exe` with argv `"<helper>"
<handle>`, served every widget of a launch: Low, restricted to `S-1-0-0`
with every group but the integrity label deny-only and only
`SeChangeNotifyPrivilege`, an LPAC token of the package
(`WIN://NOALLAPPPKG` one UINT64 value 1) with no capability, no thread
token, its job's only member (limits `0x2408`, UI `0xFF`), and the
outside mitigation reading `DEP=0x3 ASLR=0xB DynamicCode=0x1
StrictHandle=0x3 SystemCallDisable=0x5 ExtensionPoint=0x1 CFG=0x0
Signature=0x5 FontDisable=0x1 ImageLoad=0x4 ChildProcess=0x1
UserShadowStack=0x100`, through 10 s of work with four runtimes whose
`Date` and `Intl` readings equalled the `in-process` control's and whose
random bytes were nonzero; `in-process` started no helper. A killed
helper left both cards' last pictures unchanged outside the stop pill,
re-entry started one new confined helper, and a second kill reached the
two-crash limit with no third start. A quit ended a live helper 24 to
28 ms and an idle one 2 ms after the request and the client within
0.3 s; a suspended helper with a client write past both 64 KiB pipe
quotas ended `exit 1067, terminated after 999 ms` and the client 1.2 s
after the quit, and without the quit the 10 s write-stall monitor ended
it. With the helper missing from a copied layout three starts failed with
`CreateProcessAsUserW failed: The system cannot find the file
specified.`, nothing ran in process, the fourth widget died with `the
widget helper failed 3 times in a row`, and the same layout with the
helper served. The layouts and the execute denial of
[the Windows lowbox](#remote-engines) ran with that client too (Run 3),
with no grant. The attack audit over all 133 corpus files and the
conformance runner over the 133-run automatic matrix gave 0 leaks and the
same keyed rows, verdicts, report rows and stage judgements with no
switch and with `in-process`, the same 15 stops and baseline failures
included; the other differences were timing values and one crop's
anti-aliased edge, each shown in both versions by its paired rerun, and
every default launch ran in exactly one confined helper (Runs 3-3b). One
`in-process` control launch died once with an access violation in
`Test::WatchWidgetHost()`, called from `Tgw::Runtime::constructed()` on a
`WidgetHost` it could not read; its rerun passed, and that path is the
in-process one, which the move did not change.

On x64 alone the driver (Run 1) started the helper with the 16-byte
mitigation attribute and the LPAC attribute in one creation list, and it
reached Ready: the product checks read its actual primary as an LPAC
token and its attached startup token as a regular lowbox of the same
package at the impersonation level, both with no capability, and an
independent reader of the 64-bit security-attribute layout agreed
(`WIN://NOALLAPPPKG`, one UINT64 value 1, on the primary only). Without
the LPAC attribute the helper refused, and the parent's check abandoned
it before the resume. With `HIGH_ENTROPY_ASLR_ALWAYS_OFF` in place of
the flag the suspended child lacked high entropy and the helper refused
with `the mitigation policy 1 reads 0xb`; with the flag only left out the
image's own high-entropy ASLR still applied, and the helper reached
Ready. Without the CET flag, and with its always-off value, it lacked user
shadow stacks and refused with `the mitigation policy 15 reads 0x100`. The
x64 client then ran with a copy of the Test account (Runs 4-4b): its
compiled table read `creation=[0x1001101111110100 0x10000000] size=16`
with ten expectations and none for DEP, and with no switch one helper,
`modules\x64\TelegramWidgetHelper.exe` with argv `"<helper>" <handle>`,
served five runtimes for 10 s, among them an event widget through two
trusted click cycles, with the token and job above and the outside
mitigation reading `DEP=0x3 ASLR=0xF ... UserShadowStack=0x105`, and
ended `exit 0` at the quit, with no crash under user shadow stacks. The
x64 client's own checks of that helper's primary passed
`CheckLockdownToken` and `CheckAppContainerToken` for `LessPrivileged`
and failed it for `Regular` with `the token is a less privileged
AppContainer token`; `TokenIsLessPrivilegedAppContainer` failed with
error 87 there too. `in-process` ran the static and reading widgets and
the event widget's two click cycles with no helper.

Not run with `TelegramWidgetHelper.exe`: ARM64 (not built), a Release
runtime (the non-Debug gate was preprocessed and its link options read
only), the Store build, Windows before 10.0.26300 (the 1809 floor of
each new creation flag and readback rests on Microsoft's documentation
and SDK guards), the layouts, kill, quit, missing-image, attack and
conformance legs on x64 (x86 only), an updater-style copy, a 9P share
and a downloaded ZIP with its mark of the web, a DLL planted in
`modules\<arch>` (so the `SetDefaultDllDirectories` call in `Adopt()`
has no observed effect), either failed `bcrypt.dll` bind, a client killed
under its helper, and two clients at once.

On Linux the [remote engines](#remote-engines) first ran in the
same-binary helper, the client's own `/proc/self/exe -widgethelper 3`
that `TelegramWidgetHelper` later replaced, in the x86_64 Debug client
built in the Docker `centos_env` image, on a WSL2 6.6 kernel with
an Ubuntu 24.04 userland, Landlock ABI 3 and unprivileged user namespaces,
in the evidence campaign of
`2026/11/10/run-widget-engines-in-a-sandboxed-helper-process-on-linux`
(its `work/test.md`, Attempt 2). The client had no account (an empty golden
folder under `test-run --portable-root`), and a disposable overlay made
engines through `Tgw::PrepareRemoteEngines()` and
`twidget::SandboxEngine::make`; no card, chat or harness pack ran. With no
switch every engine ran in one `/proc/self/exe -widgethelper 3` child in
its own user, PID, network and mount namespaces (PID 1, only `lo`, an empty
root), with `Seccomp: 2`, `NoNewPrivs: 1`, no capabilities and only
descriptors 0 to 3; `in-process` started no helper. `probe` read the lines
recorded in [test_widget_helper_probe](test_widget_helper_probe.md#verification)
and its engines loaded. Under `bwrap --disable-userns` the client logged
`Tgw: The widget helper starts without namespaces: clone failed: No space
left on device.` and the helper kept the client's namespaces with seccomp,
NNP and Landlock applied. A SIGKILL ended the helper's engines and the next
engine started a new helper; a stopped cloned helper was gone 11 ms after
its client was killed. Inside Flatpak 1.14.6 (a privileged container,
`org.freedesktop.Platform//24.08`) the client spawned the helper through
the portal's `Spawn` (flags 28, descriptors 0 to 3, working directory `/`);
the helper, in the portal sandbox's namespaces (PID 2, only `lo`, its root
kept), had `Seccomp: 2`, `NoNewPrivs: 1`, no capabilities, Landlock and only
descriptors 0 to 3, and a stopped helper ended through `SpawnSignal` to its
process group. The framework harness's `--attacks`
loop over the 133 corpus files and the 68-widget conformance automatic
set, run with the client's executable as the engine process, gave 0 leaks
and the same rows and statuses as in process, apart from two throttle
readings: `cpu-kill-after.html`'s count, which varies in process too, and
`timer-flood.html`'s `cpu-throttled`, which the Debug client's `-O0`
QuickJS reaches near the 300 ms quota with or without the sandbox. Not run:
aarch64 (only `widget_lockdown_linux.cpp` compiled, its filter's decisions
emulated), a confined Snap, Ubuntu's AppArmor user-namespace restriction,
the client's death inside Flatpak (the portal's `WATCH_BUS`), a Release
runtime, and the tdesktop attack audit and conformance runner (no account
on that host).

`TelegramWidgetHelper` then ran on the same host, in the x86_64 Debug
client of the same image, in the evidence campaign of
`2026/11/14/move-the-linux-widget-helper-into-a-qt-free-executable` (its
`work/test.md`, Attempt 1, Runs 1-4), again without an account and with a
disposable overlay that made engines through `Tgw::PrepareRemoteEngines()`
and `twidget::SandboxEngine::make`, on a short-lived `std::thread` and on
`crl::async`; no card, chat or harness pack ran. The probe's runs are on
[test_widget_helper_probe](test_widget_helper_probe.md#verification).

- Build (Run 1): `out/Debug/TelegramHelpers/TelegramWidgetHelper` was
  built before Telegram from its own six objects and the two widget
  archives, without the precompiled header and with `-Werror`; a build of
  the helper alone linked no Telegram, and Telegram's link no longer took
  the lockdown. Its NEEDED were `libc.so.6`, `libm.so.6`,
  `libpthread.so.0`, `libdl.so.2` and the loader, with no RPATH and no Qt,
  GLib, crl or lib_base symbol. A tree configured without the image's
  `LDFLAGS` and with the allocation tracer got `-pthread`, `-ldl` and the
  tracer only from `common_options`, linked, and linked the C++ runtime
  dynamically.
- With no switch (Run 2) one helper served both engines:
  `/proc/<pid>/exe` the helper's file, not the client's, `cmdline`
  `<path>`, `3`; its own user, PID, network and mount namespaces (PID 1,
  only `lo`, an empty root); `Seccomp: 2`, `NoNewPrivs: 1`, one filter, no
  capabilities; descriptors 0 and 1 `/dev/null`, 2 the client's stderr and
  3 the socket, the client's inheritable test descriptor absent; only
  the helper's file and glibc's objects with libm mapped; two
  `widget.sandbox` threads in it and none in the client. `in-process`
  started no helper. A SIGKILL ended its engines (`stream ended: the
  engine process closed the stream.`, `ended, signal 9.`), and the next
  engine started a new helper from the same file.
- Under a wrapper whose inherited filter answered every seccomp install
  of the client and its children with `EPERM` (Run 3), three helpers
  refused with `prctl(PR_SET_SECCOMP) failed: EPERM`, each `exit 1`,
  before any engine loaded, and the next engine died with `the widget
  helper failed 3 times in a row`; an outer strace saw exactly three
  `execve` of the helper's file with argv `[<path>, 3]` and none carrying
  `-widgethelper`.
- A package-managed tree (the same image, no special target, the prefix
  `/usr`) compiled `../libexec/TelegramDesktop/TelegramWidgetHelper`.
  `cmake --install --prefix <prefix>` put Telegram into `<prefix>/bin` and
  the helper into `<prefix>/libexec/TelegramDesktop`, and that Telegram
  started the installed helper (Run 4). A `DESTDIR` install with the prefix
  `/usr` and the binary renamed `telegram-desktop`, the Snap's shape,
  started `usr/libexec/TelegramDesktop/TelegramWidgetHelper` the same way.
  With the installed helper removed, three starts failed with `execve
  failed: No such file or directory`, the fourth engine died with `the
  widget helper failed 3 times in a row`, and nothing ran in process. A
  configure with the absolute libexec directory `/usr/lib/x86_64-linux-gnu`
  (configured, not built) compiled
  `../lib/x86_64-linux-gnu/TelegramDesktop/TelegramWidgetHelper`, and its
  install script installs to that absolute directory. The official
  configuration compiles `TelegramHelpers/TelegramWidgetHelper`, and its
  install script installs to `bin/TelegramHelpers`.
- Inside Flatpak 1.14.6 (an `/app` install of that tree in a privileged
  container, `org.freedesktop.Platform//24.08`, Run 4) the client called
  the portal's `Spawn` once per start with the working directory `/`, the
  argv `/app/libexec/TelegramDesktop/TelegramWidgetHelper 3`, descriptors
  0 to 3, an empty `envs` and flags 28; the helper descended from the
  portal, not the client, with `Seccomp: 2`, `NoNewPrivs: 1`, no
  capabilities, user, PID, network and mount namespaces other than the
  client's and only descriptors 0 to 3, and served both engines; at quit
  one `SpawnExited` carried its `exit 0`.
- `Telegram -widgethelper 3`, run as the framework harness's engine
  process, started an ordinary Telegram and answered nothing (Run 1).
- The framework harness's `--attacks` loop over the 133 corpus files, on
  the host's glibc 2.39 and in the image's glibc 2.28, and the 68-widget
  conformance automatic set on 2.39, run through the helper (Run 1), gave
  0 leaks, attack rows equal to the same-binary helper's on 2.39 and to
  in process on both apart from `timer-flood.html`'s `cpu-throttled`
  (which on 2.28 an unsandboxed control of the same build reached too),
  and conformance statuses equal to in process; every helper exited 0,
  none with `SIGSYS`. The allowlist was measured on this helper under
  strace with glibc 2.28, 2.39 and 2.41 and tightened by 16 entries
  (**The Linux lockdown**). Emulated over every call number on x86_64 and
  aarch64, the moved but untightened filter decided every call as before
  the move, and the tightened one changed only those entries' decisions;
  on the tightened helper only `clone3` and the probe's own opens were
  refused (the tightening's strace legs, re-read in Run 1).
- The Release helper (Run 1) linked the same libraries, contained no
  probe, and exited 1 unless started with only the token `3` and a socket
  on descriptor 3.
- The Snap ran in the evidence campaign of
  `2026/11/16/keep-the-snaps-dav1d-link-flag-off-the-linux-widget-helper`
  (its `work/test.md`, Attempt 1, Runs 3-4), in an Ubuntu 24.04 VM
  (kernel 6.8): snapcraft 9.2.1 built the recipe with and without that
  task's change in destructive mode. With the change the `.snap`'s
  `usr/libexec/TelegramDesktop/TelegramWidgetHelper` had the NEEDED
  `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6` and
  `ld-linux-x86-64.so.2`; without it `libdav1d.so.7` came first, before
  the same five. `usr/bin/telegram-desktop` had the same 80 NEEDED
  entries in the same order in both, `libdav1d.so.7` first. A Release
  client of the changed recipe built with the test harness
  (`TDESKTOP_RELEASE_TEST_HARNESS`), whose NEEDED and link edge equalled
  the plain build's, ran account-free and unconfined under Xvfb, with the
  overlay above and an AVIF leg added, from a copy of the unpacked
  `.snap` tree in which only `usr/bin/telegram-desktop` was replaced, and
  with the `LD_LIBRARY_PATH` that the Snap's launch composes from snapd,
  `meta/snap.yaml`, `mesa-2404` and `gnome-46-2404`. It started that
  tree's `usr/libexec/TelegramDesktop/TelegramWidgetHelper`
  (`/proc/<pid>/exe` that file, the path its lookup computes from the
  tree's `usr/bin/`), whose maps held its own file, glibc's objects, libm
  and the C++ runtime and nothing of `libdav1d`, while the client's held
  `libdav1d.so.7.0.0`; both engines loaded in it. In another launch of
  the same tree that client decoded a 16x16 lossless AVIF to its source's
  exact pixel through `Images::Read`, read no image from a truncated copy
  and decoded a PNG control. This was the first run of
  `TelegramWidgetHelper` under Ubuntu's AppArmor user-namespace
  restriction (`kernel.apparmor_restrict_unprivileged_userns=1`): the
  helper got its own user, PID, network and mount namespaces without the
  fallback line, with `Seccomp: 2`, `NoNewPrivs: 1` and no capabilities,
  but its root still listed the system root, because its empty-root step
  was refused without a diagnostic (the silent case of **The Linux
  lockdown**).

The shipped helper ran in the evidence campaign of
`2026/11/14/ship-the-linux-widget-helper-in-the-tarball-and-its-updates`
(its `work/test.md`, Attempt 1, Runs 1-3), account-free, with engines
made by an overlay as above. `Telegram/build/build.sh` and
`Telegram/build/docker/build.sh` ran unmodified in an isolated
`tdesktop:centos_env` container on the outputs of an x86_64 Debug build,
with test update keys, which that build trusted, and stubs only for the
build, the symbol dump and the cloud signer. Their tarball held
`Telegram/TelegramHelpers/TelegramWidgetHelper` `-rwxr-xr-x`, their
update package exactly `Telegram`, `Updater` and the helper, all
executable, and every shipped copy of the helper had the pinned digest;
the base commit's scripts packed none. A fresh extract of that tarball
passed the identity check and loaded the engines in its helper, while
the base tarball's extract failed three starts with `open failed: No
such file or directory` and reached the failure limit. The client's own
update path (that package as `tupdates/ready-package` in its working
folder, then a plain launch), in installs their user can write, created
`TelegramHelpers/` 0755 with the helper where the folder was missing,
replaced an older helper and reached the helper of a binary renamed
`telegram-desktop`. `-installupdate` as root, in a container, did the
same for a root-owned install without the folder, the folder and the
helper root 0755. Run directly as root with the arguments that
`-installupdate` gives the Updater, on two identical staged updates
holding only the helper, the base Updater made the folder 0700, so the
user could not read the helper in it, and the changed one made it
0755. Each updated install loaded the engines in its new
helper. A local replay of the canary Linux job's "Pack v2 update." step
put the helper with the pinned digest into its update and its portable
archive. A Snap that snapcraft built from the base commit in an Ubuntu
24.04 VM like the one above (the Snap builds no Updater and reads none
of the other changed files) shipped
`usr/libexec/TelegramDesktop/TelegramWidgetHelper` `-rwxr-xr-x`,
byte-equal to its build tree's, with its symbol table and without the
`.gnu_debugdata` that `usr/bin/telegram-desktop` had. A Release client
with the test harness, unconfined in a copy of that unpacked tree with
only `usr/bin/telegram-desktop` replaced, logged `Tgw: The widget helper
is not pinned in a package-managed build.` before each of its two helper
starts and loaded the engines in that file; as in the run above, the
helper mapped the C++ runtime and, under the AppArmor user-namespace
restriction, its root still listed the system root.

A Snap installed with strict confinement ran the helper in the evidence
campaign of
`2026/11/16/prove-the-linux-widget-helper-in-the-strictly-confined-snap`
(its `work/test.md`, Attempt 1, Runs 0-4), account-free, in an Ubuntu
24.04 VM like the one above (kernel 6.8, snapd 2.78, AppArmor enforcing,
`kernel.apparmor_restrict_unprivileged_userns=1`). snapcraft 9.2.1 built
the recipe in destructive mode as it is, and again with the test harness
(`TDESKTOP_RELEASE_TEST_HARNESS`) and a disposable overlay, which let its
Release client read `TDESKTOP_WIDGET_ENGINES` and pass a probe mode and
made engines as above; the harness build's helper was byte-equal to the
plain recipe's. A third Snap was the harness build's tree with only the
helper replaced, repacked with `snap pack`: a Release helper rebuilt in
that build's directory after a second disposable overlay had opened its
three probe gates, with only those three objects recompiled. Each was
installed with `snap install --dangerous`: `snap debug confinement` read
`strict`, `snap info --verbose` read `confinement: strict`, `devmode:
false` and `jailmode: false`, the profile
`snap.telegram-desktop.telegram-desktop` was in enforce mode, and the
two test installs had the plain install's AppArmor profile and seccomp
source but for the revision. Started as the snap's app with `snap run
telegram-desktop` under Xvfb, the client ran from
`/snap/telegram-desktop/<rev>/usr/bin/telegram-desktop` with the label
`snap.telegram-desktop.telegram-desktop (enforce)` in snap-confine's
mount namespace, and before each start it logged `Tgw: The widget helper
is not pinned in a package-managed build.` and `Tgw: The widget helper
starts without namespaces: clone failed: Permission denied.`, after
AppArmor had denied its namespaced `clone` (`userns_create`). Read as
root, the helper with the plain recipe's bytes, which ran without the
probe, had `/proc/<pid>/exe`
`$SNAP/usr/libexec/TelegramDesktop/TelegramWidgetHelper` with that
file's device and inode, the client's label, the client as its parent
and the `cmdline` `<path>`, `3`. It shared the client's user, PID,
network and mount namespaces, its `uid_map` the initial namespace's, and
its root listed the client's entries; it had `Seccomp: 2`, three filters
on the client's two, `NoNewPrivs: 1` where the client had 0, no
capabilities, like the client, and only descriptors 0 and 1 `/dev/null`,
2 `app_stderr.txt` and 3 the socket whose other end only the client
held. Both engines loaded in it, two `widget.sandbox` threads there and
none in the client. The probe helper, read the same way, matched it in
label, filters, no-new-privileges, capabilities, namespaces, root and
descriptors, read Landlock ABI 4 enforced and had its copies' `execve`
and `socket` killed
([test_widget_helper_probe](test_widget_helper_probe.md#verification)).
With journald's audit socket on and no audit record lost, the kernel
logged for the helper: that `userns_create` denial, once per start; one
seccomp refusal of the empty root's `unshare` (`syscall=272`) per
lockdown call, from snapd's filter, which a control under the app's
label read as `EPERM`, so the root stayed; in both probe modes an
AppArmor denial of the probe's `openat` of `/proc/<pid>/uid_map`; and,
with `probe`, the `SIGSYS` kills of the `execve` and `socket` copies by
the helper's own filter. No record showed a call that the helper's
filter allows refused after its lockdown, and that filter's own `ENOSYS`
and `EPERM` answers are never logged. Not run there: a Telegram without
the test harness (the plain install was only read) and the terminal
relay of descriptor 2 (`test-run`'s stderr is a file).

The held and stripped helper ran in the evidence campaign of
`2026/11/17/hold-the-linux-widget-helper-for-telegrams-lifetime-and-strip-it`
(its `work/test.md`, Attempt 1, Runs 1-4) on a WSL2 6.18 kernel,
account-free, with engines made by an overlay as above. A Release tree
with the test harness (`TDESKTOP_RELEASE_TEST_HARNESS`), configured by
the official `configure.sh` with the special target `linux` in
`tdesktop:centos_env` and compiled with the official flags, the debug
information flags `-g -gdwarf64 -gz` among them, but without LTO, which
the harness refuses, and with `-Wno-error` appended, was built at the
base commit and again with the change and the overlay. The base build's
Release helper had compressed `.debug_info`, `.debug_abbrev`,
`.debug_line` and `.debug_str` sections and a symbol table: 12589504
bytes, 9726632 with `xz -9e --check=crc64`, the update packer's liblzma
settings. With the change each non-Debug configuration's helper link
step stripped the file in its POST_BUILD, before the identity step that
reads it; the Release build relinked and stripped the helper (GNU strip
2.44), then generated a digest equal to the stripped file's SHA-256.
That helper had no `.debug_*` section and no symbol table: 3354080
bytes, 1141720 compressed, byte-identical to the base helper stripped by
the same tool; `minidebug.sh` would have made the base helper 3430976
bytes, 1220296 compressed. The Docker tree's Debug helper kept its debug
sections, the Debug link steps of both trees ran no strip, and a
package-managed tree, configured but not built, had no strip, no
identity step and no pinned define. In installs of the changed build in
the `TelegramHelpers` layout (its Telegram, its Updater and its stripped
helper) the install's own Updater, run with `-justupdate` and only a
helper staged, replaced the helper during a launch or between launches:
it unlinked the file and copied the staged helper into a new one. A
running Telegram held one descriptor of its helper from launch, before
any start, at 10 or above with close-on-exec, and its `/proc/self/fd`
link read `<path> (deleted)` once the base helper had replaced the file.
That Telegram then started its helper again after a SIGKILL of the first
one and after the second one's idle exit, and in another install the
first start came after the replacement: each of these starts passed the
identity check with the build's own digest, ran the held file
(`/proc/<pid>/exe` read `<path> (deleted)` with the inode held at
launch) and loaded an engine, with no refusal and no failure limit. A
Telegram launched after the replacement held the base helper and
refused three starts with the build identity check's error, naming the
base helper's digest and its own, and its fourth engine died with `the
widget helper failed 3 times in a row`; once the Updater had put the
build's own helper back, a fresh launch held that file, started it from
`<path>` and loaded an engine. One byte appended in place to a held file
after its helper's idle exit made the next three starts refuse with the
changed digest, followed by the failure limit. With no
`TelegramHelpers/` at launch the main log had `Tgw: The widget helper is
not held, each start opens it: open failed: No such file or directory.`
and the first start failed with `open failed: No such file or
directory`; once the helper was written into place, the next start
opened the path, passed and loaded an engine, and nothing was held at
the end. An earlier launch of a binary whose stale `specific_linux.cpp`
object had no `Hold()` call held nothing, and after the replacement its
starts were refused with the base helper's digest until the failure
limit stopped them, which is what a per-start lookup gives. Not run:
aarch64, a second running Telegram installing a whole update package, a
shared root-owned install, a folder moved under a running Telegram, a
hold failing other than for a missing file, a failed duplicate, Flatpak
and a package-managed build at run time, built RelWithDebInfo and
MinSizeRel helpers (their graphs only), the official LTO configuration,
and the release scripts on the stripped helper.

Not run with `TelegramWidgetHelper`: aarch64 at run time (its sources
compiled for aarch64 with `-Werror`, its filter emulated), distribution
and Flathub toolchains, a client killed under a stopped helper (the
helper's own `PR_SET_PDEATHSIG` read only as the probe's `pdeathsig=9`),
the client's death inside Flatpak, a Release client without the test
harness, and the tdesktop attack audit and conformance runner (no
account in any of these campaigns).

The Windows build-pinned gate (**The Windows helper** above) was verified
on an x64 host (Windows 10.0.26300) in the evidence campaign of
`2026/11/14/verify-the-windows-widget-helper-through-the-handle-it-starts`
(its `work/test.md`). The build pins the Authenticode digest per
architecture (x86 and x64) and regenerates it when the helper is relinked;
the native `CryptCATAdminCalcHashFromFileHandle2` digest through the held
handle equals that pin, and a test-signed copy keeps the same digest. A
component probe over the shipped `HoldHelper()` and `CheckStartedImage()`
showed, on x86 and x64, the hold denying every write, rename, delete and
replacement and the folder renames, refusing a pre-existing writer, a
writable mapping and a name-surrogate reparse at `modules` or
`modules\<arch>`, and the `FILE_ID_INFO` comparator refusing a different
file under an equal name. Through the shipped `Launch()` the client started
its own helper and a test-signed copy and refused a modified helper and a
foreign executable with the digest diagnostic and no engine in process,
and a client built before a helper change refused the rebuilt helper while
the rebuilt client accepted it. ARM64, the Store build and a Release client
were not run; the cached-image-section technique is out of the task's
threat model (that task's known problems).

The shipped packages (**The Windows helper** above) ran on an x64 host
with Windows 11 build 26300 (10.0.26300), x86 under WOW64, in the
evidence campaign of
`2026/11/14/ship-the-windows-widget-helper-in-the-installer-portable-archive-and-updates`
(its `work/test.md`). `build.bat`'s release steps, with a stand-in
code-signing certificate and stand-in update keys in place of the owner's
signing, built Release x86 and x64 packages with the test harness on and
LTO off. Each build signed its helper once, and the setup, the portable
ZIP and the update package carried byte-identical signed copies and no
`.pdb`; `widget_helper_identity.py` over the signed helper reproduced the
build's pin, so signing kept it, and a Telegram of another build refused
that helper with `the widget helper digest does not match this build`.
The Release helper, built with the QuickJS engine, imported only ADVAPI32
and KERNEL32 and delay-loaded only `bcrypt.dll`. The x86 and x64 setups
installed it and their
uninstallers removed it, and `-cleanup` deleted every helper copy from
the `x86`, `x64` and `arm64` folders, idle and while its client served
live widgets. x86 copies were updated through a loopback update server,
installed and portable, from no helper (an install from a setup without
it, and a portable copy without `modules`, whose folders the Updater
created) and from an older helper, the installed one also through
`Core::Restart()` with live widgets, and each updated layout served its
widgets from the new helper. Installed, portable and updated, the helper
reached Ready as the LPAC process of the package (a lowbox token with
`WIN://NOALLAPPPKG`, no capability, Low, its job's only member), and no
folder scanned held a grant for the package SID; none of 23 Release
helper starts on x86 and x64 ended with `0xC0000135`, `0xC06D007E` or
`0xC06D007F`. Not run: ARM64 at run time (its setup was compiled and the
ARM lines of `build.bat` read), LTO, the owner's certificate and
KeyLocker signing, an x64 update or portable copy, the canary job on a
real runner (its sign and pack steps were simulated locally) and Windows
builds before 26300.

The Windows Store build (**The Windows Store build** above) ran on
2026-11-17 on Windows 10.0.26300.9711 x64, in the evidence campaign of
`2026/11/14/run-windows-store-widget-engines-in-the-restricted-helper`
(its `work/test.md`, Attempt 1, and `evidence/`; its
`work/decisions.md`, W1, is the confinement record): an x64 Store Debug
build (`uwp64`), laid out by a task script the way `build.bat`'s Store
lines lay out the AppX folder, packed, signed by the stand-in and
installed under `C:\Program Files\WindowsApps` as **Windows Store test
runs** describes, not as a Developer Mode registration. The installed
helper was byte-equal to the build's, and its Authenticode digest was
the build's pin. A build of the change before its commit gave the first
readings (`evidence/c2/readings.md`): through the alias, with a Test
account, the `probe` leg's client logged `Tgw: Widget engines run in the
widget helper (TDESKTOP_WIDGET_ENGINES=probe).`, started one helper from
the package with no hold, digest or start-image error and served three
live cards with it until the quit (helper and client `exit 0`), and a
network leg passed its 62 checks. An outside reading of the helper read
its token, job and mitigations as the x64 records of
`2026/11/15/move-the-windows-widget-helper-into-a-qt-free-executable-and-run-it-on-x64`
but for the differences that **The Windows Store build** lists. Each
client read the package job above, and an activation through
`IApplicationActivationManager` read the same identity and job. The
committed change then ran from packages installed the same way:

- Default path: with no switch the client logged `Tgw: Widget engines
  run in the widget helper.` and started exactly one helper,
  `"\\?\<package root>\modules\x64\TelegramWidgetHelper.exe" <handle>`
  with the client as its parent, which served the fixture's cards
  through 10 s of work, read from outside with the token, job and
  mitigations above, and ended at the quit; the `in-process` control
  showed the same cards with no helper. The probe and network legs,
  repeated, and a parent-package ACE leg are on
  [test_widget_helper_probe](test_widget_helper_probe.md#verification).
- Kill and restart: killing the helper stopped its cards with `the
  widget engine process ended`, their last picture unchanged outside the
  stop pill, with no restart while they stayed on screen; re-entry
  started one new helper with the same confinement, and a second kill
  reached the crash limit, after which nothing started. The client
  survived both kills.
- Fail-closed packages: a package without the helper stopped the first
  three widgets with `the widget helper could not start: could not hold
  \Device\HarddiskVolume<n>\Program Files\WindowsApps\<package full
  name>\modules\x64\TelegramWidgetHelper.exe: CreateFileW failed: The
  system cannot find the file specified.`; one whose helper differed in
  one byte with `the widget helper could not start: the widget helper
  digest does not match this build`, no helper process ever existing;
  and a disposable client variant that left its helper in the package
  job (no job list, no breakaway) got helpers that refused before Ready
  with `VerifyJob()`'s `the job's limits are 0x800 for 0 processes` and
  exited 1, each card stopping with `the widget engine process ended`.
  In each a fourth widget then met the latch (`the widget helper failed
  3 times in a row`), and no engine ran in process.
- Parity: the attack corpus and the automatic conformance set gave the
  same results in the helper and in process, 0 keyed differences and 0
  leaks over the attacks, 0 verdict or row differences and byte-identical
  crops over the conformance runs, with exactly one confined helper in
  every default launch that ran a widget and none in process.
- `build.bat`, run on the `uwp64` Debug tree through a disposable overlay
  with the stand-in signer in place of the owner's: it signed
  `Telegram.exe`, `StartupTask.exe` and
  `modules\x64\TelegramWidgetHelper.exe` and packed the signed helper
  into the AppX folder and `Telegram.x64.appx` at `modules\x64\`, its
  Authenticode digest the build's pin. Its AppX folder held the test
  layout's files, byte-equal but for the manifest, `resources.pri` and
  the three signed executables, and its manifest differed from the
  source only in `ProcessorArchitecture="x64"`.
- Non-Store controls: the x86 and x64 non-Store Debug builds of the same
  tree, probed unpackaged, read `package identity: none
  (APPMODEL_ERROR_NO_PACKAGE)`, and with outside readings of that tree's
  x64 helpers they attribute the probe's `CreateProcessW` row and the
  extra modules and handles to the package
  ([test_widget_helper_probe](test_widget_helper_probe.md#verification)).

No Store install or launch changed the owner's state in its bracket, and
the real `%APPDATA%\Telegram Desktop UWP` was never created. Not run: the
x86 and ARM64 Store builds (their `build.bat` lines were read), a Release
Store build, the Store's submission, certification and signing, Windows
builds other than 26300.9711, a package on a volume other than the
system's, the startup-task and protocol-activation starts, and, in the
Store build, `probe-unsandboxed`, `connect-control` and targets other
than the loopback ones.

The [Windows sandbox tiers](#remote-engines) (**The Windows sandbox
tiers** and **The Windows 7 tier** above) were added by
`2026/11/21/choose-the-windows-widget-helpers-sandbox-tier-by-windows-version`,
whose `work/plan.md` records the tier table, and whose `work/context.md`
(section 3) and `evidence/call-ledger.md` give the documented floor and
source of every policy, attribute and function a tier uses
(`evidence/record.md` joins them per tier). Its evidence campaign (its
`work/test.md`, Runs 1-9, and `evidence/`) forced each tier on Windows 11
build 26300 (10.0.26300.9711), on x86 and x64, through Telegram with a
disposable overlay or through that task's tier driver, which mirrors
Telegram's launch, overriding only the version reading of the launching
process and of the helper, and repeated the unforced full tier's
readings on the final images of its first attempt, whose lowbox and full
paths its second attempt left unchanged. Those are the first readings
of the tiers below the full one. They read:

- both processes chose the table's tier on both sides of every threshold
  and inner floor;
- every tier reached Ready under its token, job and policies, and the
  Windows 7 helper ran on its own Low-labelled desktop, read from
  outside;
- the Windows 7 helper's exposure matched **What it leaves** under **The
  Windows 7 tier** above: every socket, loopback target and AFD endpoint
  open on both families
  ([test_widget_helper_probe](test_widget_helper_probe.md#verification));
  its own desktop and `WinSta0` as its only USER handles, and `user32`,
  `win32u`, `GDI32`, `gdi32full`, `msvcp_win` and `IMM32` as the modules
  its USER connection added, with no `cryptbase.dll` (the identical
  connection in an ordinary process loaded seven modules: those six and
  `ucrtbase.dll`, already among the helper's core modules); and DEP
  (`0x3`, permanent) as the one mitigation policy the x86 helper reads
  back, while the x64 launch passes no creation word (the outside
  reading of its shadow stack, `0x105`, is the `/CETCOMPAT` image on
  that CET host, not a tier setting);
- every fail-closed case refused before Ready with its own text against
  a same-session positive, and a Windows 7 desktop left unlabelled made
  the helper refuse with `the widget helper is not sandboxed:
  LoadLibraryExW failed: error 1114`;
- a one-byte change of the helper was refused on the Windows 7 and full
  tiers;
- the 211 hosted cards on Nova equalled each architecture's reference at
  matched readings on all four tiers.

Every forced reading is a simulation on Windows 11, and a real Windows
7, 8, 8.1 or Windows 10 before 1809 is field coverage that no record
here claims. The paragraphs above that name Windows 10 1809 as the
helper's floor describe the
campaigns of their own tasks, run before the tiers; only the Store build
keeps that floor now (**The Windows Store build** above).

The settle in `RemoveFixtureMessage()` compiled in the macOS arm64 Debug
build of
`2026/11/16/decide-a-right-click-released-before-the-widgets-answer`; its
reading (no row and no height for the removed message, a kept neighbour as
the control, and a removal without the settle as the negative) is planned
as that task's campaign check H2, which records its result in that task's
`work/test.md`.

**Hosted widgets on macOS, Linux and Windows** above and the hosted
readings of the seams describe the arrangement of
`2026/11/14/run-widget-validators-and-renderers-in-the-macos-helper`,
compiled in the macOS Debug build; their first runs, with every pack in
the default arrangement against `in-process` and `helper-engines`, are
that task's evidence campaign, which records them. So far only these have
run, all outside the app: of its kept pictures the trim order and, since
the eleventh review round, when Telegram keeps only pixels, the render
work's request, paint and publication, the chat core's frame, drop and
apply and the runtime's unload, trim and restart, copied verbatim into a
scratch harness with the real `KeptBudget` (the card seen longest ago
dropped, pictures on screen kept, the placeholder at the last height, the
restart on screen, the placeholder at a new width or a kept document's
reflow, an in-process still repainting, one undelivered picture per work,
a picture painted after a menu answer shown after it, a later cut request
served, a null paint not asked again); the glue's cursor and canvas
font rules, in a scratch harness that links the helper's own glue
objects, also under ThreadSanitizer and AddressSanitizer, and since the
third review round its refused-font limits (one request per interval for
1,500 uses of a refused spelling, 64 requests per interval, recovery after
a refill with the metrics of a spelling never refused) and its `Clicked`
reports, against the glue before them; and the hold timing (its waiter
woken by an admission or renewal while it sleeps until its check too) and
the writer-admitted grants, in widget-sandbox's own tests. Since the ninth
review round, also outside the app: a paint whose emoji tile request
cannot prepare its eviction, batch slot or request entry, through the
Debug build's real painter and tile cache objects (refused, the cache as
if that paint never happened, another owner served); the work's real stop
path after the reason's conversion; and the acquisition order in the
`KeptBudget` harness. Since the tenth: the picture mailbox
(`PicturePost`, copied whole from the source) replacing, emptied at
teardown, with many cards and a stalled chat, resuming idle and ordered
behind earlier answers, also on two threads under ThreadSanitizer; and
each preparation of an emoji request (the owner's and the global eviction,
both, an isolated glyph, since the eleventh also an isolated glyph with its
large-tile fallback) failed with an older neighbouring glyph that still
paints and the cache unchanged. The kept readings above, the helper's
cursor and canvas font rules, the click reports and the report post have
no app reading yet.

On Linux the same arrangement was turned on by
`2026/11/14/run-widget-validators-and-renderers-in-the-linux-helper`,
compiled in the Linux Debug builds of both layouts; that task's records
hold its runs. Those builds predate the Nova engine, which was then
linked into the Linux helper alone; the
Linux source selects the same hosting, manager and answer classifier
(*A renderer that stops answering* above).

The end of a helper whose renderer stops answering (*A renderer that
stops answering* under **Hosted widgets on macOS, Linux and Windows**, with its
runtime log,
failure and quit readings) ran in the macOS arm64 direct-download Debug
client and its embedded Debug helper in the evidence campaign of
`2026/11/20/kill-the-widget-helper-when-a-renderer-leaves-a-request-unanswered`
(its `work/test.md`, Attempt 1, which holds the timelines), over three
runs, each a client rebuilt with that run's disposable evidence overlay.
Attempt 1 exercised that task's pre-rebase macOS build, from before the
Nova engine; no Linux build ran it.
The overlay's Debug helper tap made each stall real: a `SIGSTOP` of the
helper at the ingress of a card's frame request, a withheld frame or a
frame of another sequence. In Run 1, for a visible card whose helper
stopped at its frame request, the stream's end was decided 3.0 s after
the request's admission while the window scrolled and painted, the exact
child was killed after the 1 s exit wait and gone within 5 s of that
admission, and the card kept its last picture under the stop pill with
`Stopped (fatal): The widget stopped responding` and `Crashes in a row:
1 of 2.`; and a quit 0.044 s after such a request's admission ended the
stream with the owner's stop 0.063 s after that admission, then that
child, with no later start. In Run 3, with the frame of one of two live
cards withheld, that card read the same, the other the localized stop
label and `1 of 2` from 0, and the helper exited on its own. In Runs 1
and 3, a frame of another sequence that Telegram refused before its
deadline, and a counted stop of a fixture-owned hosted client, each ended
that widget alone, with its own reason, while the other card went on
answering on the same helper past that deadline. In Run 2, with the
refusal held on `widget.render` until the stream's end came first, the
refused card read the stopped-responding reason, and with that branch of
`RendererWork::refuseRoute()` reverted its own refusal instead. In each
of the three runs an idle chat with a running card, the cards placed off
screen with nothing timed admitted there and a static card with no frame
request admitted each kept the same helper for 10.5 s, and a matching
frame held 3.6 s behind render work on `widget.render` ended nothing; no
run's log had `failed 3 times` or `was not reaped`. In the Run 2 client
the production classifier's timed cases over the framework's scripted
engine end passed, among them a 3.6 s wait on the host's side for a
renderer request's answer that ended nothing and two overdue routes of
one stream both decided `Stalled`. Not run: the Mac App Store service's
path (read in `widget_helper_store_mac.mm`) and a stall by a widget's own
content. Those runs predate the per-run accounting of **Runtime log and
deaths**: the live sibling Run 3 read stopped at `1 of 2` is now decided
`Spared` and gets no charge from that end.

The per-run accounting of **Runtime log and deaths** (stalled, ordinary,
spared, refused and withdrawn runs, the spared restart, the held route and
the start gate) and the removal barrier through every hosted final came
with
`2026/11/20/count-a-widget-helpers-death-only-against-widgets-with-work-in-flight`.
Its evidence campaign (its `work/test.md`, Attempt 1, which holds the
measurements) ran them in the macOS arm64 direct-download Debug client and
its embedded Debug helper, each launch built with that campaign's
disposable evidence overlay: Debug helper tap verbs that withhold a frame
request or replace an answer, an external `SIGKILL` of the verified direct
child, and bounded gates that only delay a delivery, a construction or a
render creation. It observed:

- a stall charging only its own widget, while the spared sibling started
  again uncharged, with no paint, on a new helper verified as a sandboxed
  direct child; the stalled widget stopping at `2 of 2` after its second
  stall; and a third helper failure latching the manager;
- an external kill charging only the route with a request open, and
  both routes when neither had one;
- a credited answer held across a sibling's timeout settling `Spared`, the
  count unchanged, when the surviving core painted it, and charged
  `refused/spared` when that core refused it; one paint given while that
  run was undecided started its widget once after the `Spared` final, and
  none after the refusal's second crash; a removal before such a refusal
  finalized the run `Spared`, and the answer released afterwards charged
  nothing;
- an engine's own death charged once, whether its message was removed
  while its stop was held or not, and nothing when withdrawn before its
  admission;
- healthy, hidden, static and kept cards and a credited answer held behind
  render work keeping their helper and their count, and an ordinary quit
  leaving no helper;
- both packs' removal barriers closing with every hosted work final
  ([test_widget_attacks](test_widget_attacks.md#verification),
  [test_widget_conformance](test_widget_conformance.md#verification)).

A render creation skipped because its link was already gone
(`RendererCore`'s constructor tests `HostLink::gone()` before
`RendererWork::Create`) leaves no renderer platform to send that work a
timed request, and the open buffered for its route expects no answer, so
that route is never `Stalled` by its own request; a decision already
recorded for its run survives the skip. Not run: a Release build with the
harness, the Mac App Store service and the other platforms.

On Windows it was turned on by
`2026/11/14/run-widget-validators-and-renderers-in-the-windows-helper`,
whose first Windows builds and runs used the QuickJS engine, before the
Nova change, and which then linked Nova into the Windows helper alone;
that task's records hold its runs.

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
