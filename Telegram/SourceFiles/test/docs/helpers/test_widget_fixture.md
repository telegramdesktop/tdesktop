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
says where the engines of its widgets run: by default in a sandboxed
widget helper, packaged builds included, which on macOS, on Linux x86_64
and aarch64 and on Windows 10 1809 or later outside the Store build is the
separate Qt-free `TelegramWidgetHelper` (in the Mac App Store build, from
macOS 12, an App-Sandboxed XPC service), on Windows a restricted
`modules\<arch>\TelegramWidgetHelper.exe`. Where no such helper exists no
widget engine runs: every widget stops with `no
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
  refusal of the widget's frames, found in `RendererWork::raster()` on the
  core's work turn. The stop reaches the chat later, in the posted answer
  `RendererCore::apply()` takes: only then do the core's `stopped()`,
  `stopReason()` and `stopFatal()` read it and the runtime retire, so the
  reading stays empty until that answer arrives. A host's `frozen` is
  latched by `RendererCore::hostMessage()` at once, but its stop, too, is
  published only by the posted answer. Such a stop reaches no host death:
  the host watch's `dead` stays null, or reads `terminated` once the
  retired host is destroyed ([test_widget_host](test_widget_host.md)).
  The identity is the caller's: `core` is `WidgetHostSample::core` of the
  run's one watched attachment, compared by address with the list's own
  runtime's `Runtime::core()`, which is the retired core itself (the
  still) after the stop; never `interactive()`, null once stopped. That
  address holds unless the watched core stops before its first delivered
  picture while an older still is shown: `Runtime::retire()` then
  discards the stopped core, `Runtime::core()` stays the older still,
  another address, and the reading is empty, as for any other core.
  `fatalFrozen` is the run's tap count
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

## Remote engines

On macOS (in the Mac App Store build from macOS 12), on Linux x86_64 and
aarch64 and on Windows 10 1809 or later outside the Store build, packaged
builds included on all three, every widget engine of the client runs in
one shared widget helper process that has locked itself down, instead of
the client's own threads, in Debug and Release builds alike and with no
switch. That helper is the separate Qt-free executable
`TelegramWidgetHelper`, on Windows the restricted
`TelegramWidgetHelper.exe`; the Mac App Store build packages it as an
App-Sandboxed XPC service (**The Mac App Store service** below).
Everywhere else no widget engine runs: every widget stops with `no
sandboxed widget helper is available here: <reason>` (**Store, packaged
and other platforms** below), and only the Debug switch `in-process` runs
the engines in the client there. The
packs, the fixture and the seams they use need no change for it.

- **Platform default.** It is `Platform::WidgetHelper::Enabled()`: true on
  macOS, builds with packaged libraries (`DESKTOP_APP_USE_PACKAGED`)
  included and the Mac App Store build (`OS_MAC_STORE`) from macOS 12, on
  Windows 10 1809 or later outside the Store build and on Linux x86_64
  and aarch64, so a launch with no switch runs every engine in the
  sandboxed helper. Everywhere else it is false, and a launch with no
  switch stops every widget with `no sandboxed widget helper is available
  here: <reason>`, the reason of
  `Platform::WidgetHelper::UnsupportedReason()`, which is empty exactly
  when `Enabled()` is true and with which each platform's `Launch()`
  refuses before any spawn or connection: `the widget helper needs macOS
  12 or later in the Store build` in the Mac App Store build before macOS
  12 (`@available(macOS 12.0, *)` in `widget_helper_mac.mm`), `the widget
  helper is not available in the Store build` in the Windows Store build,
  `the widget helper needs Windows 10 1809 or later`
  on Windows before 10 1809, `the widget helper is not available on this
  architecture` on other Linux architectures and `the widget helper is
  not available on this platform` on the non-Linux all-other platforms.
  On Windows it is true exactly when `UnsupportedReason()`, defined in the
  client's `platform/win/widget_helper_win.cpp`, returns no reason,
  whatever the architecture.
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
  names it in the main log: `Tgw: Widget engines run in the widget
  helper.` or `Tgw: Widget engines do not run, no sandboxed widget helper
  is available here: <reason>.`. Once it chose the
  helper no engine of that process falls back to in process: a helper that
  cannot start or refuses its lockdown ends its widgets with a diagnostic,
  and the failure limit stops further starts (**Runtime log and deaths**
  and **Failure limit** below). Release builds,
  `TDESKTOP_RELEASE_TEST_HARNESS` ones included, read no switch, never
  probe and, like every build other than Debug, run no engine in process
  on any platform: the default never chooses in process and always
  installs a channel factory, the helper's or one that refuses every
  engine, with which `twidget::SandboxEngine::make()` builds no in-process
  engine. Telegram has no `-widgethelper` entry on any platform; its
  helper, where it has one, is the separate `TelegramWidgetHelper` (**The
  shared helper** and **The Windows helper** below), whose Release build
  contains no probe and accepts only its channel token. On Linux
  `Telegram -widgethelper 3` starts an ordinary Telegram, which takes both
  arguments as start URLs and answers no channel, and on Windows
  `Telegram.exe -widgethelper <handle>` started an ordinary client that
  answered no valid opening on the inherited pipe
  ([Verification](#verification)). A Windows helper that runs without the
  client's confinement refuses in its lowering (`LockDownHelper()`, **The
  Windows lowering** below) before Ready. The Windows helper has run only
  in Win32 (x86 on WOW64) and x64 Debug builds on Windows 11 build 26300
  (10.0.26300); ARM64, a Release runtime, the Store build and Windows
  before that build have not.
- **Debug switch.** Debug builds also read `TDESKTOP_WIDGET_ENGINES` at
  that point:
  - unset or any other value: the platform default, so a Debug build runs
    its engines in the helper wherever that default is true and stops
    every widget elsewhere;
  - `helper`: the sandboxed helper explicitly, the same as the default on
    macOS (the Mac App Store build from macOS 12), on Linux and on Windows
    10 1809 or later outside the Store build, so command lines that name
    it keep working; on Windows it is the restricted helper (**The Windows
    helper** below);
  - `in-process`: the engines run in the client, also where the default
    stops them; it is the only way to run widget engines in the client's
    own process, for tests and the harness, and the control of a run that
    compares the two modes;
  - `probe` and `probe-unsandboxed`: on macOS every helper, whose Debug
    build carries the probe, probes its lockdown, or runs the same probe
    without it as the control, and refuses the stream instead of serving a
    widget; its listing goes to
    `app_stderr.txt` and its summary to the main log's `stream ended` line
    ([test_widget_helper_probe](test_widget_helper_probe.md); the Mac App
    Store service gets the mode in its channel message,
    [Mac App Store](test_widget_helper_probe.md#mac-app-store)). Each
    refusal counts as a failure (see the failure limit below). On Windows
    `probe` runs the restricted helper with its native probe and serves
    widgets as `helper` does (**Windows probe** below), and
    `probe-unsandboxed` makes every helper refuse the stream with `the
    widget helper probe is not available on this platform`, each refusal
    a failure as well. On Linux both start the sandboxed helper, whose
    Debug build carries the probe too; it writes its probe lines to
    `app_stderr.txt` and then serves widgets (**Linux probe** below):
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
    --env TDESKTOP_WIDGET_ENGINES=in-process
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
  `modules/x64/` next to its own `Telegram.exe`. The main log names a
  recognized value: `Tgw: Widget engines run in the widget helper
  (TDESKTOP_WIDGET_ENGINES=helper).`, the same with `probe`
  or `probe-unsandboxed`, or `Tgw: Widget engines run in process
  (TDESKTOP_WIDGET_ENGINES=in-process).`.
- **Store, packaged and other platforms.** macOS, Windows and Linux have a
  helper. The non-Linux all-other platforms, FreeBSD among them, build
  none (no `TelegramWidgetHelper` target) and start none: their default
  stops every widget with `no sandboxed widget helper is available here:
  the widget helper is not available on this platform`. An explicit
  `helper` or probe value there fails closed as well: no engine runs in
  process, and each widget dies with `the widget helper could not start:
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
  Store build`, each a failure towards the failure limit, with no engine in
  process; that path has not run (no macOS before 27.0.1 was available).
  A macOS packaged build builds, stages and runs the same helper as every
  other non-store macOS build: the helper links no Qt, so the external Qt
  of the packaged workflow (Homebrew's) never reaches it. Every Linux build
  defines the helper. A package-managed build, one with packaged libraries
  (`DESKTOP_APP_USE_PACKAGED`) and no special target (distributions, Snap
  and Flathub), installs it into
  `${CMAKE_INSTALL_LIBEXECDIR}/telegram-desktop/` and builds it at the same
  place relative to its built binary (`../libexec/telegram-desktop/` with
  the default directories); every other build, the official tarball's
  (packaged too, with the special target `linux`) and the Docker
  development builds included, puts it into `TelegramHelpers/` next to the
  binary, and the install rule of a packaged one into
  `${CMAKE_INSTALL_BINDIR}/TelegramHelpers/` (**The shared helper**
  below). The release scripts move their files by hand and do not pack the
  helper yet, so a tarball they make stops its widgets with the missing
  helper's `open failed: No such file or directory` until
  `2026/11/14/ship-the-linux-widget-helper-in-the-tarball-and-its-updates`
  ships it. The Snap's `telegram` part builds with the prefix `/usr` and
  no special target, so it stages and primes
  `usr/libexec/telegram-desktop/TelegramWidgetHelper`, which the renamed
  `usr/bin/telegram-desktop` finds. The Snap's `avif` part writes the
  forced `dav1d` link,
  `-Wl,--push-state,--no-as-needed,-ldav1d,--pop-state`, into the `Libs:`
  line of the `libavif.pc` it builds, so the link reaches only the
  executables that link the avif decoder, Telegram and the
  `codegen_emoji` build tool, and the Snap's helper links only glibc,
  libm and the C++ runtime, like the other builds' helpers. A Snap built
  with snapcraft staged the helper, and a Release Telegram with the test
  harness, linked as that Snap's, started it from the Snap's unpacked
  tree, unconfined ([Verification](#verification)); no Snap has run
  confined with it.
  The Windows Store build and Windows before 10 1809 stop every widget
  with `no sandboxed widget helper is available here: <reason>`, because
  the default is false there, with the reason `the widget helper is not
  available in the Store build` or `the widget helper needs Windows 10
  1809 or later`; an explicit `helper` or probe value makes the seam fail
  every launch closed with the same reason, `the widget helper could not
  start: <reason>`, before it builds the helper's path, each a failure
  towards the failure limit. The Windows Store build also has no
  `TelegramWidgetHelper` target: `Telegram/CMakeLists.txt` includes
  `init_standalone_target.cmake` and `telegram_widget_helper.cmake` on
  Windows only without `build_winstore`. Neither has been run. The Windows
  default reads nothing else, so a Windows build with packaged libraries
  defaults to the helper as well; no such build has been run.
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
  certificate-signed client requires `identifier
  "com.tdesktop.Telegram.WidgetHelper" and certificate leaf = H"<SHA-1 of
  the client's own leaf certificate>"`, read at each start from the
  running client after it validated itself dynamically against its own
  designated requirement (`SecCodeCopyDesignatedRequirement`, then
  `SecCodeCheckValidity`), which binds the reading to the running image,
  so no build records a signer. An ad hoc client, and a genuinely unsigned
  one (no signature, and no valid status from the kernel), requires
  `cdhash H"<digest>"` instead: the CDHash its build recorded for that
  slice in the generated `widget_helper_identity.h`, which
  `Telegram/cmake/widget_helper_identity.py` writes from the helper
  target's final file. A mismatch, or a check that cannot complete, fails
  the start: the client kills the suspended child, or spawns none when its
  own identity is what failed (**Runtime log and deaths** below).
  The helper moves to the working directory `/` itself, in
  `WidgetHelper::Adopt()` before it adopts the channel. It links only
  libSystem and libc++, its Debug build too: no Qt, lib_base, crl or
  framework, built from widget-sandbox's engine end, QuickJS, the POSIX
  stream and its own `_other/widget_helper*` sources, without Telegram's
  precompiled header and before Telegram.
  Only a Debug probe value adds arguments after the `3`: the mode, the
  client's working directory and Application Support path, as
  [test_widget_helper_probe](test_widget_helper_probe.md) describes; the
  probe `dlopen`s its frameworks only after its listing. A Release
  `TelegramWidgetHelper` accepts only the token: with any other argument
  it prints `Widget helper: unexpected arguments` and exits 1, and with
  another token, a descriptor 3 that is no socket or a failed `chdir` it
  prints `Widget helper: the channel could not be adopted` and exits 1,
  both before it reads the channel. Its build string is the number in
  `Telegram/build/version`, which equals the client's `AppVersion`; a
  helper of another version refuses the opening with `build "<client's>",
  this end's is "<helper's>"`. The helper target strips its file outside
  Debug and then signs it ad hoc with the hardened runtime and the
  identifier `com.tdesktop.Telegram.WidgetHelper`, before its CDHash is
  recorded; Xcode never signs that target (`CODE_SIGNING_ALLOWED NO`),
  and no build gives the helper an entitlement. Ad hoc Xcode and Ninja
  builds, `build_mac.py`'s included, stage that file unchanged
  (`build_mac.py`'s universal app merges the two slices with `lipo`,
  which keeps each slice byte for byte), so each staged slice has the
  recorded CDHash, and `codesign --verify --deep --strict` of the Xcode
  app and of `build_mac.py`'s universal app passes. Release and
  certificate signing go through `Telegram/build/sign_mac.py`, which
  `build.sh`, the canary workflow, `build_mac.py`'s Debug app and a
  POST_BUILD step of Telegram's Xcode target call, the Mac App Store
  build's included: it signs the other nested code with the caller's
  identity, options and entitlements, except the Mac App Store service and
  any Mach-O inside a `Resources` folder, re-signs the helper only for a
  certificate identity, with that identity, the hardened runtime, its
  preserved identifier and no entitlement, and then signs the app without
  `--deep` (in an Xcode build Xcode signs the app itself). The release strip of
  `build.sh` and the canary leaves the already stripped and signed helper
  byte for byte unchanged (measured with CLT 26.6's `strip`, which still
  warns that its changes will invalidate the code signature). The pin
  refuses a helper whose CDHash differs from the recorded one, such as
  another build's (an identical ad hoc re-sign can keep the CDHash),
  while the signer rule accepts any helper with the helper identifier
  that the client's own certificate signed, a re-signed release's
  included, and refuses an ad hoc one, the build's own too. A copy signed
  with a certificate for a test needs `sign_mac.py` too: `codesign
  --force --deep` of the app gives the helper Telegram's entitlements and
  another identifier. No helper build carries `get-task-allow`, so a
  debugger cannot attach to the helper; observe it from the client and
  through its stderr. A running client refuses every later start once its
  own executable or its signed `Info.plist` changed on disk, as a
  rebuild, a re-sign or a reinstall under it can do: start a fresh client
  after each. A client whose code a debugger modified may lose its valid
  status and refuse the same way (not measured); `in-process` keeps a
  Debug client's engines out of the helper then. To test a refusal,
  replace only `Contents/Helpers/TelegramWidgetHelper` in a copy of the
  bundle: the client's own identity stays valid, and the check reads the
  replacement.
  On Linux it is `TelegramWidgetHelper` as well, built from the same
  engine end, QuickJS, POSIX stream and `_other/widget_helper.cpp`, with
  `_other/widget_helper_linux.cpp`, the Linux lockdown and the descriptor
  closing (`platform/linux/widget_{lockdown,descriptors}_linux.cpp`;
  Telegram no longer links the lockdown) in place of the macOS sources,
  without Telegram's precompiled header and before Telegram. It links
  only glibc's objects, libm and the C++ runtime, its Debug build too:
  the Docker build links the C++ runtime statically, so its NEEDED are
  `libc.so.6`, `libm.so.6`, `libpthread.so.0`, `libdl.so.2` and the
  loader, and a build that links the C++ runtime dynamically adds
  `libstdc++.so.6` and `libgcc_s.so.1`. Its file is
  `TelegramHelpers/TelegramWidgetHelper` next to the client's binary
  (`out/Debug/TelegramHelpers/TelegramWidgetHelper` in the Docker
  tree, the tarball layout) or, in a package-managed build,
  `<libexecdir>/telegram-desktop/TelegramWidgetHelper`
  (`/usr/libexec/telegram-desktop/` with the prefix `/usr`,
  `/app/libexec/telegram-desktop/` inside Flatpak,
  `$SNAP/usr/libexec/telegram-desktop/` in the Snap). The client finds it
  as `cExeDir()`, the canonical directory of its own binary that it
  resolved once at start from `/proc/<pid>/exe`, joined with the relative
  path CMake fixed at configure time (`TDESKTOP_WIDGET_HELPER_PATH`, a
  definition on `widget_helper_linux.cpp` only:
  `TelegramHelpers/TelegramWidgetHelper`, or the path from the bindir to
  the libexec folder, `../libexec/telegram-desktop/TelegramWidgetHelper`
  with the default directories) and cleaned lexically (`QDir::cleanPath`).
  So a binary started through a symlink or renamed, as the Snap's
  `telegram-desktop` is, finds it, while a tarball folder moved under a
  running Telegram is not followed. An absolute
  `CMAKE_INSTALL_LIBEXECDIR`, such as `/usr/lib/x86_64-linux-gnu`, still
  gives a path relative to the configured prefix
  (`../lib/x86_64-linux-gnu/telegram-desktop/TelegramWidgetHelper`), so
  `cmake --install --prefix <another prefix>` puts the helper into that
  absolute directory while Telegram moves, and that Telegram misses it;
  package tooling installs at the configured prefix, staged with
  `DESTDIR`, where the path holds. The client looks the file up at every
  start. In the `TelegramHelpers` layout, that of every build but a
  package-managed one, it then checks the file before each start: it
  opens the path (`O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK`, so a
  symlink there is followed), requires a regular file (`fstat`,
  `S_ISREG`) and hashes the opened descriptor with SHA-256,
  which must equal, in lowercase hex, the single digest of the build's
  generated `widget_helper_identity.h`.
  `Telegram/cmake/widget_helper_identity.py --kind sha256` writes that
  header per configuration from the helper target's final file
  (`out/Telegram/gen/widget_helper_identity/Debug/widget_helper_identity.h`
  in the Docker tree), and only these builds compile
  `widget_helper_linux.cpp` with `TDESKTOP_WIDGET_HELPER_PINNED`. The
  cloned child then executes that descriptor, never the path: it places
  it at descriptor 4 with close-on-exec (`dup3`), closes everything from
  5 and calls `fexecve(4, ...)`, which glibc issues as `execveat(4, "",
  ..., AT_EMPTY_PATH)`, so a file put at the path after the check never
  runs and the helper starts without descriptor 4. The main log has `Tgw:
  The widget helper passed the build identity check (SHA-256 <digest>).`
  before such a start. Any other bytes, another build's helper of the
  same version included, fail the start (**Runtime log and deaths**
  below), and so does anything that rewrites the file after its build,
  such as `cmake --install --strip` of such a build (a reading of the
  install script, not run). A package-managed build pins and checks
  nothing: the main log has `Tgw: The widget helper is not pinned in a
  package-managed build.` before each start, on both spawn paths, and
  its cloned child executes the file by path (`execve`), so the build
  string is its only check. After an upgrade replaced the file under a
  running Telegram, the next start fails the build identity check in the
  `TelegramHelpers` layout; in a package-managed build it runs the new
  file, whose build string differs, so the opening is refused until
  Telegram restarts. The client starts it as `[<path>, 3]`, without
  `-testagent`; a Debug probe value adds the same arguments after the `3`
  as on macOS, with the client's `QStandardPaths::GenericDataLocation` as
  the data path. When `cExeDir()` is empty both spawn paths fail with
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
  same argv, `/app/libexec/telegram-desktop/TelegramWidgetHelper 3` in an
  `/app` install, the working directory `/`, the same four
  descriptors and an empty environment table, so the helper gets the
  environment of the portal's sandbox, not the client's. That helper is
  not the client's child, and the pid in the main log is the portal's, not
  one in the client's PID namespace. Only a package-managed build, such as
  Flathub's, starts it there: the portal executes a path, so a build in
  the `TelegramHelpers` layout fails every start inside Flatpak with `the
  widget helper's build identity cannot be checked through the Flatpak
  portal`, before any portal call (a reading of the code, not run).
  On both Linux spawn paths, and for the direct-download macOS helper
  above, the helper's descriptor 2 follows the client's stderr at each
  start. When that is not a terminal (`isatty`), as under `test-run`,
  whose `app_stderr.txt` is a file, it is a duplicate of the client's
  stderr, and the helper's lines land there unchanged. When it is a
  terminal, descriptor 2 is the helper's end of a one-way `AF_UNIX`
  stream socket (on Linux `/proc/<pid>/fd/2` reads `socket:[<inode>]`, on
  macOS `lsof -p <pid>` shows `unix` at fd 2), never the terminal: a
  thread of the client reads the other end and writes each
  line to the client's stderr with one `write`, every byte outside
  printable ASCII (0x20 to 0x7e) escaped as `\xNN` in lowercase hex and
  `\` as `\\`. A line longer than 8192 bytes keeps its first 8192 and
  ends with ` [cut]`, a last line without a newline is relayed at the
  end, and a helper's bytes past its first 32768 are dropped, with one
  line `[widget helper output over 32768 bytes dropped]` in their place.
  The relay ends with its helper, before the main log's `ended` line
  below. On macOS, after a helper that was reaped, the client first waits
  up to 1000 ms for the relay to end on its own, at the end of the stream
  once every queued byte was relayed, and only then shuts the relay's end
  and joins its thread, still before the `ended` line: macOS discards the
  bytes still queued on a socket end shut for reading, which could lose the
  helper's last line, written right before it exits. A helper that was
  not reaped ends its relay at once, as on Linux. A terminal that stops
  accepting writes blocks only that thread, and with it the reap of its
  helper; on macOS the join after the wait blocks the same way, so
  1000 ms limits that wait, not the reap. The Mac App Store service still
  gets the client's stderr itself (**The Mac App Store service** below).
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
  reads `not reaped after N ms` before that addition. A stop by the client
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
  built) and its only entitlement the App Sandbox
  (`Telegram/Telegram/TelegramWidgetHelper.entitlements`), so it runs in
  its own empty container with no network, file, device or keychain
  entitlement. The target's POST_BUILD strips it outside Debug and signs
  the bundle ad hoc with the hardened runtime, that identifier and that
  file; the generated pin is that signature's CDHash
  (`widget_helper_identity.py --entitlements` requires the slice's
  entitlements to equal the file's exactly), and
  `TelegramWidgetHelper_bundle` copies the bundle's `Info.plist`,
  executable and `CodeResources` into the app. An edit of the plist
  template or the entitlements relinks, re-signs and re-pins it. It links
  only libSystem and libc++. `sign_mac.py` leaves it as built for an ad
  hoc identity, since its build signature is the pin, and re-signs it for
  a certificate identity with the hardened runtime and
  `--preserve-metadata=identifier,entitlements` (the identifier alone
  would drop the App Sandbox). It also skips every Mach-O inside a
  `Resources` folder, so Breakpad's `Resources/breakpadUtilities.dylib`
  keeps what each caller's own line gives it. `build.sh`'s macstore branch signs that
  dylib, then the app through `sign_mac.py` with the Mac App Store
  identity, `Telegram Lite.entitlements`, a timestamp and the hardened
  runtime; `build_mac.py`'s Store Debug signs the dylib ad hoc and calls
  `sign_mac.sign(bundle, '-')`; the Xcode Store target runs the same
  `sign_mac.py --xcode` POST_BUILD as other Xcode builds, without
  `--deep`. `build.sh`'s and `build_mac.py`'s lines end with
  `sign_mac.sign()`'s `codesign --verify --deep --strict`. The Xcode
  POST_BUILD signs only the nested code, and Xcode signs the app after it
  without a strict verify, so verify an Xcode product with `codesign
  --verify --deep --strict` yourself. `codesign --force --deep` keeps the
  service's identifier and gives it that command's entitlements: with
  `Telegram Lite.entitlements` (`build.sh`'s old line) Telegram Lite's
  nine, the network, camera, microphone, location and user file grants
  among them; with none (`build_mac.py`'s old ad hoc line) none, so the
  service loses the App Sandbox and its pinned CDHash.
  Telegram's `Launch()` (`platform/mac/widget_helper_store_mac.mm`) names
  the service after its own `[[NSBundle mainBundle] bundleIdentifier]`
  plus `.WidgetHelper`, so a copy re-identified for a test reaches its
  own service. It builds the requirement as on direct download,
  `identifier "<main bundle id>.WidgetHelper" and certificate leaf =
  H"<SHA-1 of the client's own leaf>"` for a certificate-signed client and
  `cdhash H"<pin>"` for an ad hoc one, and sets it with
  `xpc_connection_set_peer_code_signing_requirement`. XPC checks it only
  on replies and delivers an outgoing message before it checks the peer,
  so the first message, the hello, carries no descriptor. Only after XPC
  verified its empty reply does the client read the pid
  (`xpc_connection_get_pid`), watch it for `NOTE_EXIT` and send the
  channel message: `channel`, the child end of the socketpair, whose own
  copy it then closes, `stderr`, its descriptor 2, and, in a Debug probe
  launch, `arguments`. After that reply it also refuses when the watched
  instance ended or the connection's pid changed (`the widget helper
  ended before it took the channel`), then sends the keep request, which
  the service holds unanswered until it exits: the pending reply keeps the
  service at Telegram's priority (31 while serving; an XPC service with no
  request pending ran at 4) and out of the system's idle exit. Both
  replies share one 15 s deadline (`kStartWait`).
  The service runs `xpc_main()`: it accepts one peer connection and
  cancels any other, answers the hello, puts `stderr` on descriptor 2 and
  `channel` on 3, refuses a second `channel`, and starts one serve thread
  (8 MiB stack) that runs the direct helper's argument check,
  `WidgetHelper::Adopt()` (`chdir("/")`) and `serve` with the lockdown; it
  `_Exit`s on any error event of its peer connection. It is a child of
  launchd with its own container as `HOME`, launchd's `/dev/null` on 0
  and 1, Telegram's stderr on 2 and the channel on 3, and its responsible
  process is Telegram, so TCC counts its requests under Telegram's grants
  ([test_widget_helper_probe](test_widget_helper_probe.md#mac-app-store)
  lists what it can still reach). A start took about 20 ms, 90 ms when it
  created the service's container.
  A start failure reads `the widget helper could not start: <error>`, the
  errors among them `stderr is not open`, `the bundle identifier of this
  build is unknown`,
  the client's own identity errors of **Runtime log and deaths**,
  `xpc_connection_set_peer_code_signing_requirement failed: <error>`, `the
  widget helper failed the signer check: <XPC error>` or the same with
  `the build identity check` (`Peer Forbidden` on macOS 27.0.1), `the
  widget helper service <name> is unavailable (<XPC error>)` (a bundle
  without the service: `Connection invalid (Connection init failed at
  lookup with error 3 - No such process)`), `the widget helper ended
  before it took the channel`, `the widget helper did not answer within
  15000 ms` or `the widget helper refused the channel: <reason>`; once
  the client watches a pid, the cleanup follows in parentheses.
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
  identifier, the hardened runtime and `TelegramWidgetHelper.entitlements`,
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
  notification. A start after an instance that lived under 10 s, such as
  every relaunch after a probe or a refused service, meets launchd's 10 s
  deferral. At the end unregister the copies (`lsregister -u`) and remove
  `~/Library/Application Scripts/<id>`; the containers stay until removed
  in Finder.
- **The Windows helper.** It is `TelegramWidgetHelper.exe`, a separate
  executable in the `modules\<arch>\` folder next to the client's own,
  `<arch>` being `x86`, `x64` or `arm64` for the client's architecture
  (`build_winarm`, `build_win64` or neither in CMake, `_M_ARM64`,
  `_M_X64` or `_M_IX86` for the client's `kHelperArchitecture`). The
  client takes the directory of the path `GetModuleFileNameW(nullptr)`
  gives and appends that folder and file name. Before it spawns it checks
  the file through the handle it will start: `HoldHelper()`
  (`platform/win/widget_helper_image_win.cpp`) opens the helper with
  `GENERIC_READ` and `FILE_SHARE_READ` (no write or delete sharing) and the
  folders on its path (the executable folder, `modules`, `modules\<arch>`)
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
  initial token and resumes. A digest mismatch, a file it cannot open that
  way, a failed digest call, a redirected path component or a started image
  that is not the held file stops widgets with a diagnostic and starts no
  engine in process, and a path it cannot build still fails with
  `GetModuleFileNameW failed: <reason>`, `the executable path is too long`
  or `the executable path has no directory`. Telegram's build depends on
  the helper target, which puts the file there. The release scripts do not
  pack it yet (`setup.iss` and `build.bat` name no helper), so a package
  they make fails every start at the hold with `could not hold
  modules\<arch>\TelegramWidgetHelper.exe: CreateFileW failed: <reason>`
  until
  `2026/11/14/ship-the-windows-widget-helper-in-the-installer-portable-archive-and-updates`
  ships it. With `modules\x86` present and the file missing, x86 reads
  `could not hold ...: CreateFileW failed: The system cannot find the file
  specified.`; an ARM64 package ships no `modules` folder at all, and no
  package has been run. The
  client starts it as `"<path>" <handle>`, where `<handle>`
  is the decimal value of the pipe handle it inherits, without
  `-testagent`, with the client's environment and the system directory as
  its working directory (`C:\WINDOWS\system32` as `GetCurrentDirectoryW`
  reads it, which an x86 helper on 64-bit Windows holds open through
  WOW64 as `C:\Windows\SysWOW64`). Only a Debug probe value adds
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
  client opens the helper's end itself before the spawn. The client
  creates the helper suspended with `CreateProcessAsUserW` on a lockdown
  primary token derived from its own: Low integrity, no privilege but
  `SeChangeNotifyPrivilege`, the user and every group but the integrity
  label deny-only, and the NULL SID `S-1-0-0` as its only restricting SID.
  Before it resumes the helper it attaches an initial impersonation token
  to the helper's main thread and reads it back: Low as well and
  restricted to the client's user and enabled groups, so that the helper's
  startup can load its DLLs. Both are AppContainer tokens of the helper's
  package, and the creation makes the primary a less privileged one
  (**The Windows lowbox** below). The creation puts the helper in a new job
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
  helper runs outside its job or without these policies.
  The helper links no Qt, user32 or gdi32: it is built from
  widget-sandbox's engine end, QuickJS, the shared `main()` of
  `_other/widget_helper.cpp`, `_other/widget_helper_win.cpp` and the
  client's own Qt-free `platform/win/widget_channel_win.cpp` and
  `widget_sandbox_win.cpp`, which Telegram compiles too, without Telegram's
  precompiled header, as a Windows GUI image with `/ENTRY:mainCRTStartup`
  (so that no console is started) and the static CRT. Its static imports
  are ADVAPI32 and KERNEL32, both KnownDLLs, loaded from the system
  directory (`/DEPENDENTLOADFLAG:0x800`); `bcrypt.dll` is delay-loaded in
  every configuration and bound in its lowering (**The Windows lowering**
  below), and the Debug build's `WS2_32.dll`, delay-loaded too, has no
  caller but the probe. Its build string, argument gate and Release
  refusals are the shared `main()`'s, as on macOS and Linux (**The shared
  helper** above). At its entry, in `WidgetHelper::Adopt()`
  (`_other/widget_helper_win.cpp`), it first limits its later DLL loads to
  the system directory (`SetDefaultDllDirectories` with
  `LOAD_LIBRARY_SEARCH_SYSTEM32`), then takes the token only as a decimal
  handle value without a sign, a leading zero or trailing text that is no
  pseudo-handle, and adopts only the byte-mode client end of a pipe, whose
  inheritance it clears; anything else is `Widget helper: the channel
  could not be adopted`, exit 1, before it reads the channel.
- **The Windows lowbox.** `NtCreateLowBoxToken` boxes each of the two
  restricted tokens with one package SID, derived from the fixed name
  `telegram.desktop.widget-helper` by
  `DeriveAppContainerSidFromAppContainerName` (loaded from `USERENV.dll`),
  with no capability and no handle; no AppContainer profile, named-object
  directory, file grant or loopback exemption is created. The creation's
  attribute list adds `PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY`
  with `PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT`, which makes the
  helper's primary a less privileged AppContainer (LPAC) token, one that
  `ALL APPLICATION PACKAGES` grants do not reach. The startup token stays
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
  (`the attached token check failed: <error>`); either failure ends the
  suspended helper through its job and is a start failure (**Runtime log
  and deaths** below). Building the pair can fail with
  `NtCreateLowBoxToken is not available`, `NtCreateLowBoxToken failed:
  status 0x<status>`, `DeriveAppContainerSidFromAppContainerName is not
  available` or `DeriveAppContainerSidFromAppContainerName failed:
  0x<hresult>`. The AppContainer check's errors are `the token is not an
  AppContainer token`, `the token's AppContainer SID is not the widget
  helper's`, `the token has <n> capabilities`, `the token is not a less
  privileged AppContainer token` or `the token is a less privileged
  AppContainer token` (the level is read from the token's
  `WIN://NOALLAPPPKG` security attribute, because
  `TokenIsLessPrivilegedAppContainer` failed with error 87 on Windows
  10.0.26300), `the token's AppContainer information is truncated`, `the
  token's security attributes version is <n>`, `the token's security
  attributes are malformed`, `the token's WIN://NOALLAPPPKG type is <n>`,
  `the token's WIN://NOALLAPPPKG has <n> values`, `the token's
  WIN://NOALLAPPPKG value is <n>` and `GetTokenInformation failed: error
  <n>`. The design makes no grant: the client opens the image, the
  helper's static imports are KnownDLLs and every later DLL comes from the
  system directory. The evidence campaign of
  `2026/11/15/move-the-windows-widget-helper-into-a-qt-free-executable-and-run-it-on-x64`
  (its `work/test.md`, Attempt 1 Run 3, Win32 Debug only) started
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
  the refusal counts towards the failure limit below. No engine ever runs
  in process instead.
  The Mac App Store service runs the same lockdown with three
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
  `setrlimit failed: <error>` refuses. The App Sandbox still lets it reach
  more than the profile would: Telegram's process information, the allowed
  Mach services, the window server, IOKit properties and audio device
  counts ([test_widget_helper_probe](test_widget_helper_probe.md#mac-app-store)).
- **The Windows lowering.** The Windows helper drops its impersonation in
  the framework's lockdown callback, after it accepted the opening and
  holds the bundles and before it answers Ready, so before any runtime
  exists (`LockDownHelper()` in `widget_sandbox_win.cpp`, which
  `WidgetHelper::Lockdown()` calls). While still impersonating it checks
  that the mitigation readback is available (`the mitigation policy
  functions are not available` otherwise), binds the delay-loaded
  `bcrypt.dll` imports with `__HrLoadAllImportsForDll`, derives its
  package SID once (which loads `USERENV.dll`; the checkpoint of
  `2026/11/14/confine-the-windows-widget-helpers-network-with-lpac-and-turn-it-on`
  saw it map `profext.dll` as well, the host of the API set USERENV
  delay-imports for the derivation) and draws random bytes once, which
  starts the random provider that a widget's `crypto.getRandomValues` uses
  later. `bcrypt.dll` was the image's one static import that is no
  KnownDLL, and the system directory does not let the LPAC primary open
  it, so it is delay-loaded and the impersonating main thread maps it.
  While the helper still imported it statically, 3 of 5 confined launches
  on x86 and on x64 died before any frame with `0xC0000135`, and a load
  trace pointed to Windows' parallel loader opening it on a worker thread
  that runs as the primary
  (`2026/11/15/move-the-windows-widget-helper-into-a-qt-free-executable-and-run-it-on-x64`,
  its `work/decisions.md`, P6). Telegram, which compiles the same unit and
  delay-loads `bcrypt.dll` too, never runs this lowering. A failed bind
  stays closed before Ready either way: a missing delay descriptor refuses
  with `__HrLoadAllImportsForDll(bcrypt.dll) failed: 0x<hresult>`, and a
  module that cannot be loaded raises the delay-load exception, which
  nothing catches, so the helper dies before any frame with `exit
  0xC06D007E` or `exit 0xC06D007F`; no run has seen either. Then it
  reverts to its lockdown token, disables the predefined registry key
  cache and closes the cached roots, and verifies itself: no thread token,
  the lockdown token above, an LPAC token of exactly its package with no
  capability (the AppContainer check's errors above), its job's limits and
  UI restrictions, and every mitigation of its creation read back (**The
  Windows helper** above); it sets no mitigation itself. The first step
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
  processes` or `the mitigation policy <policy> reads 0x<flags>`.
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
  The startup token reaches a directory only where it grants the package
  or `ALL APPLICATION PACKAGES`: the checkpoint named in **What the Windows
  lowering leaves** read `error 5` for the client's working directory and
  `%APPDATA%` before the lowering, so their `after lowering:` denials have
  no control in the helper, and the same paths opened by the client are
  their positive control.
  The lines that start `after lowering:` try `CreateProcessW` of that
  `whoami.exe`, a TCP socket and a 1 s connect to the listener (`socket:
  not tried, the loopback listener did not start` when Winsock failed
  before or the listener's socket was not created), the same directories
  (two when the client's working directory was not given),
  `HKEY_CURRENT_USER\Software` and
  `HKEY_LOCAL_MACHINE\SOFTWARE`; read back the thread token, the process
  token and the job; read back the win32k disable of the helper's creation
  (`win32k system calls: DISABLED (ProcessSystemCallDisablePolicy
  0x<flags>)`, `ENABLED (...)` when `DisallowWin32kSystemCalls` is clear,
  or `not read (GetProcessMitigationPolicy(4) failed: error N)`, never
  `DISABLED` for a failed read); read back eleven mitigation policies, one
  `mitigation <name>: 0x<flags>` or `error N` row each for `DEP`, `ASLR`,
  `dynamic code`, `handles`, `win32k`, `extension points`, `signature`,
  `child process`, `shadow stack`, `font disable` and `image load`; and
  run a thread, an event, `OpenProcess` of its own pid, 32 random bytes
  and the time zone again. A refused process,
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
- **What the Windows lowering leaves.** Without the lowbox the Windows
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
  the helper and nothing from it. `TelegramWidgetHelper.exe` read the same
  socket and AFD denials on x86 and on x64, while listeners of the client
  on 127.0.0.1 and `::1` received every control of the client and of new
  sibling processes and nothing from it; the other targets and
  `connect-control` were not run with it ([Verification](#verification)).
  Native code in the lowered helper could therefore create no TCP or UDP
  socket over IPv4 or IPv6 and open no AFD endpoint for those kinds, and
  the ordinary helper holds no socket from before the lowering (its
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
  After Ready, on x86, the helper held as File handles only the WOW64 pair
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
  with `exit 0xC06D007E`. `TelegramWidgetHelper.exe` imports ADVAPI32
  statically and binds `bcrypt.dll` before the lowering (**The Windows
  lowering** above). At ordinary Ready and after widget work its modules
  were its own image and system DLLs only: on x86 ntdll, KERNEL32,
  KERNELBASE, ADVAPI32, msvcrt, sechost, RPCRT4, bcrypt, userenv,
  ucrtbase, profext and bcryptprimitives beside it, with WOW64's six x64
  modules, and 13 x64 modules on x64; none from outside the system
  directories, no user32, gdi32, win32u, Qt or `WS2_32.dll`, and
  `bcrypt.dll` from the system directory. On x86 the first start of a copy
  at a new path also mapped `apphelp.dll` from there. The warm-up covers
  the paths those runs found, not every load a later path can need.
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
  inside Flatpak and, by snapd's policy, under Snap), stays the root it was
  given. Seccomp still answers every path call with `ENOSYS` in both, and
  in a Debug build the probe's `layers:` line shows them (**Linux probe**
  below).
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
  state and descriptors and goes on serving widgets as with `helper`. Its
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
  per engine and no `widget.sandbox`; `widget.validator` and the host's
  `twidget-host` stay, and the channel adds `widget.reader`,
  `widget.writer` and `widget.monitor` per helper and a `widget.answer` per
  runtime that asked a synchronous question. On macOS (outside the Mac
  App Store build) and Linux a client whose stderr is a terminal also runs
  one relay thread per helper, with no name of its own (**The shared
  helper** above). The `widget.sandbox` engines run in the helper.
  Without a helper the client has no `widget.sandbox`
  and no `widget.reader`, `widget.writer` or `widget.monitor`; each host
  keeps its `widget.client`, `widget.validator` and `twidget-host` until
  its death retires it. Windows threads carry none of these names (the
  framework names its threads only on Apple platforms and Linux): there
  the `TelegramWidgetHelper.exe` child, the runtime log's `Started, its
  engine is remote.` note and the main log's `Widget helper <pid>` lines
  show where the engines run.
- **Seams.** [test_widget_ops](test_widget_ops.md),
  [test_widget_host](test_widget_host.md) and
  [test_widget_bot](test_widget_bot.md) act in the client and work
  unchanged: the tap wraps the renderer factory in `Runtime::construct()`,
  the options override edits the options before the `Job` (they reach the
  helper inside the engine's configuration), the host watch merges
  `WidgetHost::diagnostics()` on the client's `twidget-host` thread, and
  the played bot answers at `Runtime::botRequested()` through the responder
  the client engine handed the host. The client's Qt provider of
  `tgw_intl.cpp` answers the engines' `Intl` questions over the channel.
- **Runtime log and deaths.** The note of a host's start reads `Started,
  its engine is remote.` with the helper and `Started, its engine does
  not run.` where no helper is available, instead of the in-process
  `Started.`, chosen by the client's mode; the summary and its first
  `Died:` line, which the packs parse, are unchanged. Where no helper is
  available every widget dies at its start with `no sandboxed widget
  helper is available here: <reason>`, in the summary's `Died:` line and
  the host watch's `dead`: a fatal stop that counts towards the widget's
  crashes in a row like any other, with no `Tgw Error` line and no helper
  start. A helper that ends under live widgets (killed, crashed or cut
  off by a stream violation) kills each of them with `the widget engine
  process ended`, the host watch's `dead`; the helper's own reason is only
  in the main log's `stream ended` line, `the engine process closed the
  stream` for a kill. That death is a fatal stop like any other: it counts
  towards the widget's crashes in a row (two stop it), the card keeps its
  last picture when it had loaded, is released when it leaves the view and
  starts again, in a fresh helper, when it is painted visible. A widget
  whose helper cannot start dies with `the widget helper could not start:
  <error>`, the seam's error such as `posix_spawn failed: <reason>` on
  macOS (`posix_spawn failed: No such file or directory` when the bundle
  has no `Contents/Helpers/TelegramWidgetHelper`; the client never checks
  the file before the spawn) or a Windows gate error. On Windows the file
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
  checked file` (the suspended child is not the held file); the main log
  adds `Tgw Error: The widget helper could not start: <error>.`. The Mac
  App Store build's errors are listed under **The Mac App Store service**.
  On macOS outside the Mac App Store build a helper that fails the
  identity check of **The shared helper**
  is killed before it runs: the error is `the widget helper failed the
  signer check: <call> failed: <message> (<status>)`, or the same with
  `the build identity check`, followed by the cleanup of the suspended
  child in parentheses, `(signal 9, sent SIGKILL after N ms)`; a failed
  resume reads `kill failed: <reason>` with the same cleanup. A helper of
  another signer, an ad hoc one under a certificate-signed client, one
  with another identifier or another build's reads `SecCodeCheckValidity
  failed: code failed to satisfy specified code requirement(s) (-67050)`
  there, and a helper file without read permission
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
  **The shared helper** gives `open failed: <reason>` (`open failed: No
  such file or directory` when `TelegramHelpers/TelegramWidgetHelper` is
  missing) and `fstat failed: <reason>`, and the cloned child, before it
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
  that end's status in parentheses.
- **Failure limit.** A start failure and an unexpected end are helper
  failures; an end after at least 60 s of helper uptime starts a new run at
  one, and a finished idle stop resets the run. The third failure in a row
  stops launches for the rest of the process: later widgets die with `the
  widget helper failed 3 times in a row`. A `Launch()` that throws counts
  as one start failure, `the widget helper could not start: <what()>`. A
  helper that did not exit even
  when killed stops them the same way, with `the widget helper was not
  reaped`. Two kills already stop the widgets they killed, so a test of
  this limit needs fresh widgets. Once a quit has stopped the helper,
  widgets die with `the widget helper was stopped`. None of these falls
  back to an in-process engine. The stop of every widget where no helper
  is available is no helper failure: no start is attempted, so nothing
  counts towards this limit or stops launches.
- **Timing.** Queries and probes cross the channel.
  `SandboxStats::queries` and `queryMs` are counted by the engine in the
  helper and reach the client with its stats, so each uncached query's
  time includes both channel hops and the client's answer: compare query
  times only between runs of the same mode. The host's own bound on a
  query answer stays in the client, and a theme change's `probe()` is a
  channel probe bounded at 10 s. A widget that starts a helper spawns it
  synchronously in the channel factory, before the host's creation time
  that `loadedMs` counts from, so its `loadedMs` does not include the
  spawn itself; it can include the helper's own start-up and lockdown
  before its engine answers. On macOS the spawn includes both identity
  checks of **The shared helper**, Security calls without a time bound of
  their own, and on Linux in the `TelegramHelpers` layout the read and
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
  before that. Since the stop waits for build jobs, a build that is
  starting a Mac App Store service holds the quit for that start: at most
  16 s (`kStartWait` plus the 1 s cleanup) per start, with waiting builds
  starting one after another up to the failure limit, so about 48 s only
  for a service that neither answers nor ends and about 12 s for a
  launchd-deferred start and its stop. Measured Store quits: 0.1-0.2 s
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
  gives in-process loopback clients only when no factory is installed, so
  engines in the helper ignore it, the macOS, Linux and Windows defaults
  included, and so does a client without a helper, whose default installs
  a factory that refuses every engine; with `in-process` its engines keep
  the `Started.` note while running in the client. Leave it unset in both
  modes.

## Readings and refusals

| Operation | Reading or refusal |
| --- | --- |
| `InjectWidgetMessage` | Empty, `not injected: ...` (not registered, history gone or not at its bottom, list not shown, payload does not unpack) with nothing added, or `the injected message got id ..., not the armed ...`. |
| `CheckWidgetMessage` | Fills `shape`; empty, or `no HtmlWidget view: ...` (not a local outgoing client-id message, not a `Tgw::WidgetMedia` of the bytes, `its media's flags, state or widget id are not the injected ones (the media has ...)` comparing `Tgw::WidgetMedia::fields()` with the record, absent and present-empty bytes and an absent and a zero id told apart, `it comes via bot <id or none>, not the injected <id or none>` comparing `HistoryItem::viaBot()` with `fields.viaBot`, no laid-out card of the list's own). `shape` includes the record's `flags=<copy\|query\|background or none> chat=<bytes or none> public=<bytes or none> widgetId=<id or none>`: sizes, never the state's contents (`chat=0` is present and empty). |
| `RuntimeLogDecided` / `FinishRuntimeLog` | `log.source`: `requestLog`, `logText` (the runtime kept no callback), `logText-at-bound`, `unavailable`; `log.chars`, `log.pending`. |
| `log.summary` | The raw `Tgw::Runtime::_summary` (the `Summarize()` text) with `log.summaryState`. |
| `RevalidateWidgetCard` | Empty, or what changed: show refused, list gone, no hover, view gone or another, card not ready, core, card or view geometry. |
| `WidgetDesktopStop` | `RendererCore::stopReason()` verbatim, or empty: `core` 0, no own runtime or no current core, another core (the older still when the watched core stopped before its first delivered picture), a runtime not `Stopped` (a stop whose posted answer has not reached the chat yet; a stop off screen that `release()` already rearmed reads `Released`), a stop that is not fatal, or a fatal `frozen` counted on the tap. Empty is no desktop stop seen, never proof that the widget is alive. |
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
--strict`. Not run: macOS before 27.0.1, so the service on macOS 12-26
and the below-12 stop; Apple's Mac App Store re-signing and review, under
which the leaf is Apple's, shared by every Mac App Store app, so the
signer clause binds the identifier and that leaf rather than Telegram's
own; a trusted Apple leaf or a Team ID; a Release client; x86_64 at run
time; a Screen Recording grant; `build.sh`'s `productbuild`.

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
`work/test-overlay.patch` keeps the disposable scenario. It ran in the
Win32 Debug build (x86 on WOW64) and in an x64 Debug build of the same
commit, and no reading is carried from one architecture to the other.
The probe's lines are on
[test_widget_helper_probe](test_widget_helper_probe.md#verification).

On both architectures a native driver built the channel, tokens, job and
creation attributes with the product's functions and started the
committed images (Run 1). Both were built before Telegram, each into its
build's `modules\x86` or `modules\x64` folder, without a precompiled
header or a Qt input, as Windows GUI images entered through
`mainCRTStartup` with `/DEPENDENTLOADFLAG:0x800`, Dynamic base and NX,
and on x64 High Entropy VA but no CET-compatible flag; each imported
only ADVAPI32 and KERNEL32 and delay-loaded `bcrypt.dll` and the Debug
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
  `/usr`) compiled `../libexec/telegram-desktop/TelegramWidgetHelper`.
  `cmake --install --prefix <prefix>` put Telegram into `<prefix>/bin` and
  the helper into `<prefix>/libexec/telegram-desktop`, and that Telegram
  started the installed helper (Run 4). A `DESTDIR` install with the prefix
  `/usr` and the binary renamed `telegram-desktop`, the Snap's shape,
  started `usr/libexec/telegram-desktop/TelegramWidgetHelper` the same way.
  With the installed helper removed, three starts failed with `execve
  failed: No such file or directory`, the fourth engine died with `the
  widget helper failed 3 times in a row`, and nothing ran in process. A
  configure with the absolute libexec directory `/usr/lib/x86_64-linux-gnu`
  (configured, not built) compiled
  `../lib/x86_64-linux-gnu/telegram-desktop/TelegramWidgetHelper`, and its
  install script installs to that absolute directory. The official
  configuration compiles `TelegramHelpers/TelegramWidgetHelper`, and its
  install script installs to `bin/TelegramHelpers`.
- Inside Flatpak 1.14.6 (an `/app` install of that tree in a privileged
  container, `org.freedesktop.Platform//24.08`, Run 4) the client called
  the portal's `Spawn` once per start with the working directory `/`, the
  argv `/app/libexec/telegram-desktop/TelegramWidgetHelper 3`, descriptors
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
  `usr/libexec/telegram-desktop/TelegramWidgetHelper` had the NEEDED
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
  tree's `usr/libexec/telegram-desktop/TelegramWidgetHelper`
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

Not run with `TelegramWidgetHelper`: aarch64 at run time (its sources
compiled for aarch64 with `-Werror`, its filter emulated), a confined
Snap, distribution and Flathub toolchains, the no-namespace fallback
(unchanged code that execs the same file), a client killed under a
stopped helper (the helper's own `PR_SET_PDEATHSIG` read only as the
probe's `pdeathsig=9`), the client's death inside Flatpak, a Release
client without the test harness, a tarball from the release scripts
(they do not pack the helper yet), and the tdesktop attack audit and
conformance runner (no account in either campaign).

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
