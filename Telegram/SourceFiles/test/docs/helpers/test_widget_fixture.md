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
widget helper, packaged builds included, which on macOS outside the store
build is the separate `TelegramWidgetHelper`, on Linux x86_64 and aarch64
the client's own executable, and on Windows 10 1809 or later, outside the
Store build, a restricted helper that is also the client's own
executable.

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

## Remote engines

On macOS outside the store build, on Linux x86_64 and aarch64 and on
Windows 10 1809 or later outside the Store build, packaged builds included
on all three, every widget engine of the client runs in one shared widget
helper process that has locked itself down, instead of the client's own
threads, in Debug and Release builds alike and with no switch. On macOS
that helper is the separate Qt-free executable `TelegramWidgetHelper`; on
Linux it is the client's own executable, and on Windows a restricted
helper that is also the client's own executable. Everywhere else the
engines stay in process (**Store, packaged and other platforms** below).
The packs, the fixture and the seams they use need no change for it.

- **Platform default.** It is `Platform::WidgetHelper::Enabled()`: true on
  macOS, builds with packaged libraries (`DESKTOP_APP_USE_PACKAGED`)
  included, and on Windows, so a launch with no switch runs every engine
  in the sandboxed helper, and false in the macOS store build
  (`OS_MAC_STORE`), in the Windows Store build, on Windows before 10 1809
  and on the non-Linux all-other platforms, which keep their engines in
  process. On Windows it is true exactly when `UnsupportedReason()` in
  `widget_sandbox_win.cpp`, which also refuses every Windows launch,
  returns no reason, whatever the architecture.
  On Linux it is true on x86_64 and aarch64 in every build, Debug and
  Release, with or without the harness, and has no packaged exclusion:
  distribution builds against packaged libraries, Flatpak and Snap default
  to the sandboxed helper as well (`kLockdownAvailable` in
  `widget_lockdown_linux.h`). Other Linux architectures have no seccomp
  filter and keep their engines in process. No Linux build against
  packaged libraries has run the helper yet: a shared library that started
  a thread before `main` would make every helper refuse with `the widget
  helper has <N> threads before its lockdown` (**The Linux lockdown**
  below). The client decides once, on the main thread, when it constructs
  its first widget host (`Tgw::PrepareRemoteEngines()` in
  `Runtime::construct()`), keeps that choice for the whole process and
  names it in the main log: `Tgw: Widget engines run in the widget
  helper.` or `Tgw: Widget engines run in process.`. Once it chose the
  helper no engine of that process falls back to in process: a helper that
  cannot start or refuses its lockdown ends its widgets with a diagnostic,
  and the failure limit stops further starts (**Runtime log and deaths**
  and **Failure limit** below). Release builds,
  `TDESKTOP_RELEASE_TEST_HARNESS` ones included, read no switch and never
  probe. macOS Telegram has no `-widgethelper` entry: its helper is the
  separate `TelegramWidgetHelper` (**The shared helper** below), whose
  Release build contains no probe and accepts only its channel token. On
  Windows and Linux the helper is the client's own executable, whose
  `-widgethelper` entry a Release build opens only where this default is
  true (`HelperEntryAllowed()` in `tgw_remote.cpp` returns `Enabled()`
  there); elsewhere the entry exits with status 1 before it adopts a
  channel, as in the Windows Store build, on Windows before 10 1809 and in
  a Release build on another Linux architecture. On Windows 10 1809 or
  later, outside the Store build, the entry of a Release build therefore
  adopts its channel, and a helper that runs without the client's
  confinement refuses in its lowering (`LockDownHelper()`, **The Windows
  lowering** below) before Ready. Only Win32 (x86 on WOW64) Debug builds
  on Windows 11 build 26300 (10.0.26300) have run the Windows helper; x64,
  ARM64, a Release runtime, the Store build and Windows before that build
  have not, and measuring the x64 build's own creation and verification
  policies (high-entropy ASLR, CET user shadow stacks, no DEP entry) is
  the work of `2026/11/14/run-the-restricted-windows-widget-helper-on-x64`.
- **Debug switch.** Debug builds also read `TDESKTOP_WIDGET_ENGINES` at
  that point:
  - unset or any other value: the platform default, so a Debug build runs
    its engines in the helper wherever that default is true;
  - `helper`: the sandboxed helper explicitly, the same as the default on
    macOS, on Linux and on Windows 10 1809 or later outside the Store
    build, so command lines that name it keep working; on Windows it is the
    restricted helper (**The Windows helper** below);
  - `in-process`: the engines stay in the client, the control of a run
    that compares the two modes;
  - `probe` and `probe-unsandboxed`: on macOS every helper, whose Debug
    build carries the probe, probes its lockdown, or runs the same probe
    without it as the control, and refuses the stream instead of serving a
    widget; its listing goes to
    `app_stderr.txt` and its summary to the main log's `stream ended` line
    ([test_widget_helper_probe](test_widget_helper_probe.md)). Each
    refusal counts as a failure (see the failure limit below). On Windows
    `probe` runs the restricted helper with its native probe and serves
    widgets as `helper` does (**Windows probe** below), and
    `probe-unsandboxed` makes every helper refuse the stream with `the
    widget helper probe is not available on this platform`, each refusal
    a failure as well. On Linux both start the sandboxed helper, which
    writes its probe lines to `app_stderr.txt` and then serves widgets
    (**Linux probe** below): `probe` under the real lockdown, which still
    refuses the stream when it fails, and `probe-unsandboxed` with no
    lockdown at all, so that helper runs its widgets unconfined.

  The helper never reads the variable: the client passes a probe value to
  it as the mode argument after its channel (**The shared helper** and
  **The Windows helper** below). The launch passes the variable to the
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

  On Linux the first command takes `--exe out/Debug/Telegram`. The main
  log names a recognized value: `Tgw: Widget engines run in the
  widget helper (TDESKTOP_WIDGET_ENGINES=helper).`, the same with `probe`
  or `probe-unsandboxed`, or `Tgw: Widget engines run in process
  (TDESKTOP_WIDGET_ENGINES=in-process).`.
- **Store, packaged and other platforms.** macOS, Windows and Linux have a
  helper. An explicit `helper` or probe value on the non-Linux all-other
  platforms fails closed instead: no engine runs in process, and each
  widget dies with `the widget helper could not start: the widget helper
  is not available on this platform` until the failure limit below stops
  the attempts. On a Linux architecture other than x86_64 and aarch64 it
  fails closed the same way, with `the widget helper could not start: the
  widget helper is not available on this architecture`. The macOS store
  build keeps its engines in process because `sandbox_init` cannot narrow
  a process the App Sandbox already confines, and it builds no helper: it
  has no `TelegramWidgetHelper` target, and its bundle no
  `Contents/Helpers` folder. An explicit Debug `helper` there finds no
  file to start, so its starts fail and count towards the failure limit,
  and no engine runs in process; no store build has been launched with it.
  A macOS packaged build builds, stages and runs the same helper as every
  other non-store macOS build: the helper links no Qt, so the external Qt
  of the packaged workflow (Homebrew's), which lacks the patches that keep
  Qt's startup from connecting to the preferences daemon and other
  services before `main`, never reaches it. The Windows Store build and
  Windows before 10 1809 keep their engines in process, because the
  default is false there; an explicit `helper` or probe value makes the
  seam fail every launch closed the same way, with `the widget helper is
  not available in the Store build` or `the widget helper needs Windows 10
  1809 or later`. Neither has been run. The Windows default reads nothing
  else, so a Windows build with packaged libraries defaults to the helper
  as well; no such build has been run.
- **The shared helper.** One helper serves every widget of the client, of
  every account. The first host construction that needs one starts it from
  its `Build()` worker, and later widgets attach to the same process. On
  macOS it is `TelegramWidgetHelper`, a separate executable in the
  client's bundle at `Contents/Helpers/TelegramWidgetHelper`, next to
  `crashpad_handler` (the client takes `[[NSBundle mainBundle]
  bundlePath]` plus that path); the `TelegramWidgetHelper_bundle` target
  stages it there on every Telegram build outside the store build. The
  client starts it with `posix_spawn`, without LaunchServices, as its
  child with argv `[<path>, 3]` and an empty environment: the channel on
  descriptor 3, the client's stderr, `/dev/null` for stdin and stdout and
  no other descriptor (`POSIX_SPAWN_CLOEXEC_DEFAULT`), default signal
  actions, an empty signal mask and no exception ports. The helper moves to
  the working directory `/` itself, in `WidgetHelper::Adopt()` before it
  adopts the channel. It links only libSystem and libc++, its Debug build
  too: no Qt, lib_base, crl or framework, built from widget-sandbox's
  engine end, QuickJS, the POSIX stream and its own `_other/widget_helper*`
  sources, without Telegram's precompiled header and before Telegram.
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
  this end's is "<helper's>"`. In an Xcode build, whenever Telegram
  relinks, its `--deep` code signing re-signs the staged copy with the
  hardened runtime and Telegram's entitlements, as it does
  `crashpad_handler`, so the bundled file's hash then differs from the
  build folder's `TelegramWidgetHelper` while its LC_UUID and code are
  equal; when Telegram does not relink, the staging copies the build
  folder's file back and `codesign --verify --deep` of the app reports the
  nested code modified, as it does for `crashpad_handler` and `Updater`.
  On Linux it is the client's own executable, started as
  `/proc/self/exe -widgethelper 3`, so it runs the client's executable
  even after an update replaced its file, with `argv[0]` the executable's
  path as `/proc/self/exe` reads, and without `-testagent`; a Debug probe
  value adds the same arguments after the `3` as on macOS, with the
  client's `QStandardPaths::GenericDataLocation` as the data path. The
  client clones it from a thread of its own that lives as long as the
  process, with only descriptors 0 to 3 open (0 and 1 `/dev/null`, 2 the
  client's stderr, 3 the channel), in the working directory `/`, with every
  signal at its default action, an empty signal mask and the client's
  environment. The same clone gives it its own user, PID, network and
  mount namespaces: inside them it is PID 1 and root of a user namespace
  that maps it to the client's user, with only a loopback interface, while
  the main log and `ps` show its outer pid. Where the system refuses that
  clone, the main log has `Tgw: The widget helper starts without
  namespaces: clone failed: <reason>.` and the client clones it again as a
  plain child. Inside Flatpak the Flatpak portal starts it instead, through
  `Spawn` with the flags 28 (`SANDBOX`, `NO_NETWORK` and `WATCH_BUS`): the
  executable's real path, the same arguments, the working directory `/`,
  the same four descriptors and an empty environment table, so the helper
  gets the environment of the portal's sandbox, not the client's; when
  `/proc/self/exe` cannot be read that start fails with `the executable
  path is unknown`. That helper is not the client's child, and the pid in
  the main log is the portal's, not one in the client's PID namespace. The
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
- **The Windows helper.** It is the client's own executable, the path
  `GetModuleFileNameW(nullptr)` gives, started as `"<path>" -widgethelper
  <handle>`, where `<handle>` is the decimal value of the pipe handle it
  inherits, without `-testagent`, with the client's environment and the
  system directory as its working directory (`C:\WINDOWS\system32` as
  `GetCurrentDirectoryW` reads it, which an x86 helper on 64-bit Windows
  holds open through WOW64 as `C:\Windows\SysWOW64`). Only a Debug probe
  value adds arguments after the handle, the same ones as on macOS: the
  mode, the client's working directory and its
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
  and the eight UI restrictions) and sets its creation mitigations: DEP on
  32-bit, bottom-up and forced-relocation ASLR (high entropy on 64-bit),
  extension points disabled, dynamic code prohibited, CET user shadow
  stacks in x64 builds where the system offers them, and no child
  processes. A creation the system refuses, the job included, is a start
  failure: no helper runs outside its job or without these policies.
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
  <n>`. The design makes no grant: the client opens the image, its static
  imports are kernel32 and ntdll and every later DLL comes from the system
  directory. That task's checkpoint (its `work/checkpoint-2.md`) reached
  Ready that way from the checkout's `out\Debug\Telegram.exe`, and its
  evidence campaign (its `work/test.md`, Attempt 1 Run 4) from copied
  clients with fresh ordinary permissions: `setup.iss`'s files in a
  per-user folder under `%APPDATA%`, also after an updater-style
  `CopyFile` over the executable, a fresh extraction of `build.bat`'s
  portable ZIP, that folder moved whole to another path, and a copy on
  the WSL distribution's 9P share, which has no NTFS permissions. No ACE,
  package profile or AppContainer state appeared. With execute access to
  the client's own executable denied to its user, the start failed closed
  with `CreateProcessAsUserW failed: Access is denied.` and the engine
  that asked for it died with that error, none in process. All of it ran
  in Win32 Debug builds only.
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
- **The Windows lowering.** The Windows helper drops its impersonation in
  the framework's lockdown callback, after it accepted the opening and
  holds the bundles and before it answers Ready, so before any runtime
  exists. While still impersonating it binds the delay-loaded advapi32
  imports, derives its package SID once (which loads `USERENV.dll`; the
  checkpoint of
  `2026/11/14/confine-the-windows-widget-helpers-network-with-lpac-and-turn-it-on`
  saw it map `profext.dll` as well, the host of the API set USERENV
  delay-imports for the derivation) and draws random bytes once, which
  starts the random provider that a widget's `crypto.getRandomValues` uses
  later. Then it reverts to its lockdown token, disables the predefined
  registry key cache and closes the cached roots, lets only
  Microsoft-signed images load, turns strict handle checks on and verifies
  itself: no thread token, the lockdown token above, an LPAC token of
  exactly its package with no capability (the AppContainer check's errors
  above), its job's limits and UI restrictions, and every expected
  mitigation read back. The first step that fails refuses the stream: the
  helper writes `Widget helper: the widget helper is not sandboxed:
  <error>` to stderr and exits 1, the main log reads `Tgw: Widget helper
  <pid> stream ended: the engine process refused the stream: the widget
  helper is not sandboxed: <error>.`, and the widget dies. The errors
  after the revert carry Windows error numbers, such as
  `SetProcessMitigationPolicy(<policy>) failed: error N`, or name the
  reading that failed, such as `the token's integrity 0x2000 is not Low`,
  `the token's user is not deny-only`, `the token's restricting SIDs are
  not exactly S-1-0-0`, `the job's limits are 0x<flags> for <n>
  processes` or `the mitigation policy <policy> reads 0x<flags>`.
- **Windows probe.** With `probe` the client passes the mode to the
  Windows helper, and the helper's lockdown hook
  ([test_widget_helper_probe](test_widget_helper_probe.md#windows)) adds a
  native probe to the lowering, once per helper: it takes the probe's
  positive controls while the helper still impersonates, lowers the helper
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
  passing control reads `ok`, or `present` for the `whoami.exe` lookup.
  The startup token reaches a directory only where it grants the package
  or `ALL APPLICATION PACKAGES`: the checkpoint named in **What the Windows
  lowering leaves** read `error 5` for the client's working directory and
  `%APPDATA%` before the lowering, so their `after lowering:` denials have
  no control in the helper, and the same paths opened by the client are
  their positive control.
  The lines that start `after lowering:` try `CreateProcessW` of that
  `whoami.exe`, a TCP socket and a 1 s connect to the listener, the three
  directories, `HKEY_CURRENT_USER\Software` and
  `HKEY_LOCAL_MACHINE\SOFTWARE`; read back the thread token, the process
  token and the job; try to disable win32k system calls
  (`DisallowWin32kSystemCalls: APPLIED` or `not applied
  (SetProcessMitigationPolicy(4) failed: error N)`); read back nine
  mitigation policies; and run a thread, an event, `OpenProcess` of its
  own pid, 32 random bytes and the time zone again. A refused process,
  socket, connect, directory or key reads `denied (error N)` only for
  access denied (5, or `WSAEACCES` for the socket and the connect) and
  `failed (error N)` for any other error, so a connect that timed out is
  no denial; a success reads `ALLOWED`, a completed connect `CONNECTED`.
  `OpenProcess` of its own pid reads `opened` or `error N`. The product
  probe reads no argument but the client's working directory. After its
  lines the harness writes its own: fresh TCP and UDP sockets of both
  families, the fixture's loopback port and numeric network targets,
  native `\Device\Afd\Endpoint` opens and, with `connect-control`,
  preopened sockets used after the lowering, each before and after the
  lowering with the same verdict rules (`denied` only for `WSAEACCES` or
  `STATUS_ACCESS_DENIED`); their arguments, lines and controls are in
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
  measured the boundary: after the lowering every fresh socket, `AF_INET`
  and `AF_INET6` with TCP and UDP, was refused at creation with
  `WSAEACCES`, and the native `\Device\Afd\Endpoint` open with Winsock's
  own endpoint attribute with `STATUS_ACCESS_DENIED`, each after the same
  call succeeded in the helper before the lowering (the same AFD bytes
  also opened in an ordinary process). Listeners of the helper's parent on
  127.0.0.1, `::1` and the host's own LAN and link-local addresses, of a
  sibling process on 127.0.0.1 and `::1`, and one in the running WSL
  distribution received every ordinary control before, during and after
  the helper and nothing from it. Native code in the lowered helper can
  therefore neither create a socket nor open an AFD endpoint, and the
  ordinary helper holds no socket from before the lowering (its handles
  are below). Sockets that the probe's `connect-control` opens before the
  lowering delivered nothing after it: TCP connects to the host's own
  addresses timed out and one to WSL was refused with `WSAEACCES`, and UDP
  sends reached no listener; the timeouts and unreceived sends are no
  denial. Not established: which layer refuses (AFD's check of the token
  or the filtering platform), the behaviour with the firewall service
  stopped, name resolution, ICMP, other protocols, and routing off the
  machine (the WSL distribution is a separate network stack behind the
  same machine's virtual switch).
  win32k system calls stay enabled: in both runs without the lowbox the
  runtime disable failed with error 19 (`ERROR_WRITE_PROTECT`; the probe's
  `DisallowWin32kSystemCalls: not applied (SetProcessMitigationPolicy(4)
  failed: error 19)`, then `mitigation win32k: 0x0`), because every
  Telegram process loads user32 during its static initialisation, and the
  job's UI restrictions limit some window-system operations without
  blocking those calls. Under the lowbox the same attempt read error 5,
  and `mitigation win32k: 0x4` is the LPAC creation's
  `DisallowFsctlSystemCalls` bit, not a win32k disable (the same
  checkpoint).
  Registry key handles that system DLLs opened during static
  initialisation, before the lowering, stayed open with the access they
  had been granted (nine without the lowbox, eight in
  `HKEY_LOCAL_MACHINE` and one in the user's hive; seven in the lowbox
  helper, all in `HKEY_LOCAL_MACHINE` with read access only), and an open
  relative to one is checked against its target key; the probe's fresh
  opens were denied. The lowbox helper's other handles after Ready, in
  Run 2 of the campaign above, were the pipe, the stderr copy, the WOW64
  pair of its working directory with traverse access only, the crypto
  devices of the random warm-up, the `\KnownDlls` and `\KnownDlls32`
  directories, user32's window station and desktop, its own threads and
  unnamed thread-pool, synchronisation, timer, ETW and ALPC objects: no
  socket, AFD endpoint or section. A thread on the lockdown token, in
  a scratch process that
  `2026/11/10/run-widget-engines-in-a-sandboxed-helper-process-on-windows`
  launched with the helper's tokens, job and policies (its
  `work/logs/phase-4-impl-4.notes.md`), could not load the DLLs it tried
  that the process had not mapped (`bcrypt.dll`, `propsys.dll` and the
  KnownDll `ws2_32.dll`), so a path that needs a delay-loaded DLL the
  helper had not mapped before the lowering can end it: before the random
  warm-up existed, a widget's `crypto.getRandomValues` ended the helper
  with `exit 0xC06D007E`. Whether an unbound import of a DLL the helper
  had already mapped still resolves after the lowering was not measured,
  so the advapi32 bind is a precaution. The warm-up covers the paths those
  runs found, not every load a later path can need.
- **The Linux lockdown.** At its entry, before it adopts its channel, the
  Linux helper asks the kernel for `SIGKILL` when its parent ends
  (`PR_SET_PDEATHSIG`) and closes every descriptor above 3. It locks
  itself down where the macOS helper does, once it has accepted the
  client's opening and holds the bundles, before any runtime or engine
  thread exists and before it answers `Ready` (`LockDownHelper()` in
  `widget_lockdown_linux.cpp`). In this order it:
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
  `mprotect` gets `EPERM`), futexes without priority inheritance,
  glibc's own thread creation and naming, clocks, sleeps, random
  bytes and signals to the helper itself. It ends the helper with
  signal 31 (`SIGSYS`) for `execve`, `execveat`, `socket`, `connect`,
  `ptrace`, `process_vm_readv` and `process_vm_writev`, on x86_64 also
  `fork` and `vfork`, for a `clone` that does not create a thread, and for
  any call of another architecture or of the x32 ABI. It answers every
  other call, opening a file included, with `ENOSYS`; `clone3` gets
  `ENOSYS` as well, so glibc falls back to `clone`. A step that fails
  refuses the stream as on macOS: the helper writes `Widget helper:
  <refusal>` to its stderr and exits 1, the main log reads `Tgw: Widget
  helper <pid> stream ended: the engine process refused the stream:
  <refusal>.`, its widgets die with `the widget engine process ended`, and
  the refusal counts towards the failure limit below. The refusals are
  `the widget helper cannot count its threads: <E>`, `the widget helper
  has <N> threads before its lockdown`, `chdir failed: <E>` when the old
  root was already detached, `capset failed: <E>`,
  `prctl(PR_SET_NO_NEW_PRIVS) failed: <E>`, `landlock_create_ruleset
  failed: <E>`, `landlock_restrict_self failed: <E>` and
  `prctl(PR_SET_SECCOMP) failed: <E>`, where `<E>` is an error name such as
  `EPERM`, or `errno<N>`; a Debug helper started by hand on another
  architecture refuses with `the widget helper has no seccomp filter for
  this architecture`. The main log never lists the layers that applied:
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
  through the portal's argv, and the helper's lockdown hook
  ([test_widget_helper_probe](test_widget_helper_probe.md#linux)) probes
  once per helper, on serve's thread before any engine thread exists. With
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
  runtime that asked a synchronous question. The `widget.sandbox` engines
  run in the helper. Windows threads carry none of these names (the
  framework names its threads only on Apple platforms and Linux): there
  the `Telegram.exe -widgethelper` child, the runtime log's `Started, its
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
  its engine is remote.` instead of `Started.`, chosen by the client's
  mode; the summary and its first `Died:` line, which the packs parse, are
  unchanged. A helper that ends under live widgets (killed, crashed or cut
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
  the file before the spawn) or `CreateProcessAsUserW failed: <reason>` on
  Windows, and the main log adds `Tgw Error: The widget helper could not
  start: <error>.`.
  On Linux the error is `clone failed: <reason>` when the clone without
  namespaces fails as well, `<call> failed: <reason>` for another failed
  call of the spawn, such as `execve failed: <reason>` from the cloned
  child before it became the helper, `stderr is not open` or `the widget
  helper is not available on this architecture`; inside Flatpak it can
  also be `the Flatpak portal is unavailable: <message>`, `Spawn failed:
  <message>` or `the executable path is unknown`, with the D-Bus error's
  message. The Linux `<reason>` is the system's message without a period,
  such as `No space left on device`.
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
  widget helper failed 3 times in a row`. A helper that did not exit even
  when killed stops them the same way, with `the widget helper was not
  reaped`. Two kills already stop the widgets they killed, so a test of
  this limit needs fresh widgets. Once a quit has stopped the helper,
  widgets die with `the widget helper was stopped`. None of these falls
  back to an in-process engine.
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
  before its engine answers. The spawn runs while the client's helper
  manager holds its mutex. Inside Flatpak it is a synchronous portal call,
  bounded by the portal proxy's 10 s timeout and preceded, until the
  client has its proxy, by that proxy's creation, whose D-Bus calls have
  GDBus's default bound of 25 s each; until it returns, a manager call
  from the main thread waits for it.
- **Quit.** A quit stops the helper, live or idle, once no widget host,
  build, sample or theme job is left, as one more tgw job that holds the
  quit (`Tgw prevents quit, stopping widget sandboxes...`); `Tgw doesn't
  prevent quit any more.` can appear twice, before and after that job. The
  stop waits up to 1 s for the helper to exit, then kills it (on Windows
  by terminating its job, inside Flatpak through the portal) and waits up
  to 1 s more; an outstanding theme probe can hold the quit for its 10 s
  before that. When the client dies without a quit, the helper reads the
  end of its channel and exits by itself. On macOS the helper's path is not
  the client's, and `test-run`'s path-scoped kill matches only the client's
  executable: it never kills a helper or lists one in
  `stragglers_killed_after`, and after it kills a quiet client the helper
  ends on the end of its channel. A run that must show no helper outlived
  it looks for `Contents/Helpers/TelegramWidgetHelper` itself. On Windows
  and Linux a helper left behind has the client's executable path, so that
  kill ends it too and lists it in `stragglers_killed_after`. On Windows
  the client's death also closes the helper's job, whose only handle the
  client holds, and the job's kill on close ends the helper even when it
  is not reading. On Linux the kernel kills a helper the client cloned
  when the client dies, also when it is not reading: its
  `PR_SET_PDEATHSIG` follows the client's spawning thread, which lives as
  long as the client. Inside Flatpak the portal kills the helper's sandbox
  when the client leaves the session bus (`WATCH_BUS`).
- **Loopback.** The framework's own Debug switch `TW_ENGINE_LOOPBACK=1`
  gives in-process loopback clients only when no factory is installed, so
  engines in the helper ignore it, the macOS, Linux and Windows defaults
  included, and with `in-process`, or where the default keeps engines in
  process, its engines keep the `Started.` note while running in the
  client. Leave it unset in both modes.

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
Windows, Linux, the store build or a packaged build: the store build keeps
its engines in process, and packaged builds were then excluded from the
macOS helper default.

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
build compiled and linked with no helper target, file or `Helpers`
folder; and the helper's CMake stage built in packaged mode
(`DESKTOP_APP_USE_PACKAGED`) beside a placeholder Telegram target, no
packaged Qt being installed. Not run: a packaged or store app, a Release
client, widgets on x86_64, Linux and any macOS but 27.0.1.

On Windows the [remote engines](#remote-engines) ran in the Win32 Debug
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
Windows default ran on the same machine, Win32 Debug (x86 on WOW64) on
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
product probe's self-listener read `ok`. TCP and UDP listeners of the
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

The Windows default is therefore on for every architecture on Windows 10
1809 or later outside the Store build, but it is measured only in Win32
(x86 on WOW64) Debug builds on Windows 11 build 26300. Not run: x64,
ARM64, a Release runtime (the non-Debug selection was only
preprocessed), the Store build, Windows before build 26300 (the 1809
floor was read and preprocessed, never run), a target on another
machine, name resolution, ICMP and other protocols, a stopped or
disabled firewall service, which layer refuses the sockets and AFD
endpoints (AFD's check of the token or the filtering platform), a
downloaded portable ZIP with its mark of the web, and macOS or Linux
compilation of the changed `test_widget_helper_probe` sources. win32k
system calls stay enabled (the probe's disable read error 5 under the
lowbox), and `TokenIsLessPrivilegedAppContainer` is unreadable on this
build (error 87), so the less privileged level is read from
`WIN://NOALLAPPPKG`.

On Linux the [remote engines](#remote-engines) ran in the x86_64 Debug
client built in the Docker `centos_env` image, on a WSL2 6.6 kernel with
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
