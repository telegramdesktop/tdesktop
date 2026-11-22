# test_widget_helper_probe

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Internal dependency.
Source contract: [test_widget_helper_probe.h](../../test_widget_helper_probe.h)
(the client half in Telegram) and
[test_widget_helper_probe_lockdown.h](../../test_widget_helper_probe_lockdown.h)
(the helper half on macOS, Linux and Windows and the names both halves
share).

## Use when

A run must show what the macOS widget helper can still do once it locked
itself down, and list every descriptor and Mach right it keeps. The probe is
the lockdown hook of `TelegramWidgetHelper`'s Debug build, the Qt-free
executable at `Telegram.app/Contents/Helpers/`: it takes the listing after
the real `WidgetHelper::Lockdown()` (`_other/widget_helper_mac.cpp`), tries
what the sandbox must deny and refuses the stream with a summary, so no
widget is ever served, unless a fixture asks for its serving variant, which
takes the same listing and attempts while the helper renders widgets
([Serving while rendering](#serving-while-rendering)). In the Mac App Store build the same executable is
the App-Sandboxed XPC service `Contents/XPCServices/TelegramWidgetHelper.xpc`,
and the probe runs there with the differences of
[Mac App Store](#mac-app-store). On Linux the helper is `TelegramWidgetHelper` too,
in `TelegramHelpers/` next to the client's binary or in a package-managed
build's `libexec/TelegramDesktop/`, and its Debug build's lockdown hook
again runs a probe, that of
`test/test_widget_helper_probe_lockdown_linux.cpp`: it runs its own probe
in forked copies of the helper and around the real
`WidgetHelper::Lockdown()` (`_other/widget_helper_linux.cpp`), writes its
lines to stderr, and the helper goes on serving widgets, rendering them
([Linux](#linux)).
On Windows the helper is `TelegramWidgetHelper.exe`, in `modules\<arch>\`
next to `Telegram.exe`, in the Windows Store build's installed package
too ([Windows Store](#windows-store)), and its Debug build's lockdown
hook runs a different, native probe of
`test/test_widget_helper_probe_lockdown_win.cpp` and the harness's own
network attempts around the real
`WidgetHelper::Lockdown()` (`_other/widget_helper_win.cpp`), writes their
report to stderr, and the helper goes on serving widgets
([Windows](#windows)).

Callers: the probe has two halves. In Telegram the `_DEBUG` block of
`tgw/tgw_remote.cpp` is the only product caller of the client half.
`ChoosePlace()` accepts `probe` and `probe-unsandboxed` as values of
`TDESKTOP_WIDGET_ENGINES` (both select the remote engine), and
`HelperArguments()` passes `WidgetHelperProbeArguments()` to
`Platform::WidgetHelper::Launch()`. On macOS, Linux and Windows the Debug
`Run()` of `TelegramWidgetHelper` (`_other/widget_helper.cpp`), which
`main()` calls with argv and the Mac App Store service with its channel
message's arguments, gives its `serve` call
`Test::WidgetHelperProbeLockdown()` around the real lockdown. That hook,
in `test/test_widget_helper_probe_lockdown.cpp`, is the same on all
three: with nothing after the token it runs the lockdown alone, it
refuses any mode but `probe` and `probe-unsandboxed` with the platform's
`RefuseWidgetHelperMode()`, and otherwise runs the platform's
`RunWidgetHelperProbe()`; each platform's helper build compiles both from
its own `test/test_widget_helper_probe_lockdown_<platform>.cpp`. Release
builds of Telegram, harness ones included, return no arguments. A Release
`TelegramWidgetHelper` contains no probe (the target never defines
`TDESKTOP_RELEASE_TEST_HARNESS`), accepts only its token, refuses any other
argument with `Widget helper: unexpected arguments` before it adopts the
channel and always gives `serve` the real lockdown. A disposable overlay
supplies the positive-control fixtures itself (see
[Minimal use](#minimal-use)).

For the widget fixture's remote-engine switch, lifecycle and logs, read the
[Remote engines](test_widget_fixture.md#remote-engines) section instead.

## Preconditions and limits

- The listing, attempts and summary on this page are macOS only, and so is
  the keychain fixture. On Windows `probe` runs the native probe and the
  network attempts of [Windows](#windows), which read the fixture's
  loopback port, `connect-control` and network targets, and
  `probe-unsandboxed` refuses with `the widget helper probe is not
  available on this platform`. On Linux `probe` and `probe-unsandboxed`
  run the probe of [Linux](#linux), and both serve widgets. The non-Linux
  all-other platforms build no helper and have no probe hook: an explicit
  probe value there fails every start closed
  ([Remote engines](test_widget_fixture.md#remote-engines)).
- `probe` locks down, then probes. `probe-unsandboxed` runs the same probe
  without the lockdown: it is the control, always in a separate helper.
- On Windows every probe mode goes through the same gate as an ordinary
  launch (`HoldHelper()`, the build-pinned Authenticode digest and the
  started-image check, **The Windows helper** in
  [Remote engines](test_widget_fixture.md#remote-engines)), so the probe
  helper must be the one built with the running client: a relinked
  `modules\<arch>\TelegramWidgetHelper.exe` under an unchanged client is
  refused with `the widget helper digest does not match this build` until
  the client is rebuilt too.
- The real lockdown starts with its service check
  ([Remote engines](test_widget_fixture.md#remote-engines)). Its lookups
  run after the probe's reference snapshot and are released, so they show
  up only as a `changed between the reference snapshot and the listing`
  row if one leaves a right behind. The rows of that kind in a clean
  listing are the logging rights the lockdown itself opens
  ([firehose reading](#firehose-reading)). A helper that already holds a
  forbidden connection reports `lockdown: FAILED the widget helper holds a
  connection to <service> before lockdown` and is then listed and probed
  without the profile. Packaged macOS builds (`DESKTOP_APP_USE_PACKAGED`)
  build and run the same `TelegramWidgetHelper`: it links no Qt, so their
  external Qt's startup connections never reach it.
- `TelegramWidgetHelper` links only libSystem and libc++, and nothing in it
  looks up a service before `main`. In such a process the first
  `bootstrap_look_up`, even of a name nobody registered (`kr=1102`),
  lazily adds three entries: the unprivileged host port, a voucher and the
  lookup's own send-receive reply port. So right after its pre-lockdown
  snapshot the probe makes one warm-up lookup of the unregistered
  `org.telegram.desktop.widget-helper-probe.unregistered` and snapshots
  again; its reference lookups are compared with that warmed snapshot,
  while each `held-before` is still read against the untouched pre-lockdown
  one. The added entries stay and are classified by the listing like any
  other. The same-binary macOS helper it replaced never needed this: Qt
  and AppKit had made those first lookups before `main`.
- The argv after the helper's path is the token, then the tail `mode cwd
  application-support [keychain-account loopback-port [connect-control]
  [target...] [service:<name>...] [rendering:<ms>]]`, the last on macOS
  only ([Serving while rendering](#serving-while-rendering)). The token is `3` on macOS and Linux
  and the decimal value of the inherited pipe handle on Windows; the Mac
  App Store service gets the same vector from its channel message
  ([Mac App Store](#mac-app-store)). The targets come only from a
  fixture's `networkTargets` (Windows), the `service:` names only from its
  `serviceNames` (macOS). `cwd` is the client's `cWorkingDir()`; the
  helper's own working directory is `/`, which the macOS helper moves to
  itself before it adopts the channel and the Linux client gives its helper
  on both spawn paths, and on Windows the system directory the client
  creates it in. `application-support` is
  `QStandardPaths::GenericDataLocation`. A Debug helper with nothing after
  the token runs the lockdown alone and serves. Any other mode refuses with
  `unknown widget helper mode "<mode>"`. On macOS a malformed port, a
  seventh argument other than `connect-control`, any later argument, a
  target included, and an empty `service:` name are defects in the
  report, while a `service:<name>` at the seventh place or later is not:
  the Mac App Store probe lists it and the direct-download probe ignores
  it; neither is one `rendering:<ms>` there with `<ms>` from 1 to 60000,
  while a second one, another value or anything but decimal digits is; on
  Windows a malformed argument is an `argument: malformed` line, except
  that a target that does not parse, has no port or is past the limit gets
  one only with Winsock; without it such a target is only counted
  ([Windows arguments](#windows-arguments)); on Linux every argument after
  `application-support` is ignored ([Linux](#linux)).
- The Debug `TelegramWidgetHelper` links the same two libraries as the
  Release one. The attempts that call CoreFoundation, Security, CoreAudio,
  CoreMediaIO, IOKit or CoreGraphics `dlopen` that framework from
  `/System/Library/Frameworks/` (`RTLD_NOW | RTLD_LOCAL`) on its first use
  and resolve every function and exported constant with `dlsym`. The first
  such attempt runs after the descriptor and Mach listing, so no
  framework initializer changes what the listing reports; loading a
  framework is itself something the locked helper tries. A framework that
  does not load, or a symbol that does not resolve, makes that attempt
  `INCOMPLETE` with `<framework>: <dlerror() text>`, never a pass; the
  attempt's service lookups and `sandbox_check` policy rows are still
  made.
- On macOS an environment-selected probe has no fixture. Its keychain item
  read and its loopback connect report `INCOMPLETE`, which is never a pass.
  Every widget of such a launch dies with `the widget engine process
  ended`. Each refusal counts as a helper failure, so the third probe
  latches the helper for the process. On Windows such a probe has no
  network target, so the harness's attempt lines are only the socket and
  AFD ones (without Winsock its `not tried` line in place of the socket
  ones), after its `arguments:` and Winsock lines.
- A pristine listing holds two send rights no reference service matches:
  libtrace's firehose push ports to logd. The probe names them only through
  its own [firehose reading](#firehose-reading), made in the same process
  right after the snapshot it lists, which relies on a private libdispatch
  layout. When that reading fails, both stay `UNACCOUNTED` and the
  inventory is not clean.
- `task_for_pid` fails in the unsandboxed control too (no debugger
  entitlement). The probe also tries it on itself as the API control.
  `task_name_for_pid` is the parent query that only the profile denies.
- `com.apple.locationd.registration` is not registered on macOS 27 (1102 in
  the control). `com.apple.locationd.desktop.registration` is, and the probe
  looks up both.
- Never AVFoundation discovery: it crashes a locked process. Devices are
  counted with CoreMediaIO and CoreAudio property sizes only, with no capture,
  no consent prompt and no keychain UI.

## Serving while rendering

On macOS, where the helper runs each widget's engine, validator and
renderer ([Remote engines](test_widget_fixture.md#remote-engines)), the
ordinary probe never shows the helper while it renders: it refuses the
stream after its listing. Its serving variant takes the same listing and
attempts in a helper that serves widgets, for the direct-download helper and
the Mac App Store service alike. Linux and Windows, whose helpers render as
well, need no serving variant: their probes refuse no stream their lockdown
accepted, and right after their lines the same helper serves, hosting each
widget ([Linux](#linux), [Windows](#windows)).

- **Asking for it.** A fixture's `renderingDelayMs` appends
  `rendering:<ms>` after every other argument, the service names included,
  on macOS only. The helper accepts one, with `<ms>` from
  `kWidgetHelperRenderingMinMs` (1) to `kWidgetHelperRenderingMaxMs`
  (60000) in decimal; any other value, a second one, or any other
  malformed argument is a defect row and keeps the ordinary probe, which
  refuses. Windows and Linux never get it: their probe helpers go on
  serving widgets after the report anyway ([Windows](#windows),
  [Linux](#linux)).
- **Before Ready.** The helper runs every row of the ordinary probe up to
  and including `lockdown:` and `state:` on its serve thread, the real
  lockdown included, and only then decides: a lockdown that succeeded with
  `probe`, or `probe-unsandboxed`, which never locks down (the explicit
  Debug control, unconfined), writes `rendering: lockdown ok|skipped (...);
  the probe thread starts and takes the post-lockdown listing <ms> ms after
  this row while the helper serves widgets, uptime-us=<us>` and returns
  `""`, so the helper answers `Ready`. A lockdown that failed refuses the
  stream before `Ready` with the ordinary summary (`lockdown=FAILED`),
  exactly as without the argument; the lockdown result is in every branch's
  rows and summary.
- **The window.** A detached thread with an 8 MiB stack, named
  `widget.probe`, owns the probe from then on. It is started by
  `WidgetHelper::StartDetachedThread()`
  (`_other/widget_helper_thread_mac.cpp`), the starter and stack size the
  Mac App Store service's serving thread uses, so the probe runs under the
  same thread conditions as the service it measures. It sleeps `<ms>`, writes
  `rendering: listing begins <n> ms after the first rendering row,
  uptime-us=<us>`, takes the post-lockdown Mach snapshot, the firehose
  reading, the descriptor and Mach listing (and the Store's launchd
  reading), writes `rendering: listing ends, uptime-us=<us>; the attempts
  follow`, and only then makes the attempts, whose framework loads come
  after the listing as in the ordinary probe. It writes the summary as one
  uncut `summary: <summary>` row, its head with ` rendering=<ms>ms`, then
  the `end:` row, and releases the probe. Nothing calls the lockdown again,
  and the thread holds only the probe's own copies of its arguments. The
  `uptime-us` values are `CLOCK_UPTIME_RAW` in microseconds, the clock
  `mach_absolute_time()` counts, which an overlay in Telegram reads the same
  way to place the window among its frames of the same helper pid.
- **What it shows.** The listing is of a serving helper: it holds a port
  per serving thread (`thread (task_threads)`) and whatever rights the
  served host and its engine took, while the widget work loads no
  framework (the C++ helper linked none; the Rust one has not been listed
  on a Mac yet). Compare it by entry class and object with another
  serving listing taken the same way, not with the ordinary probe's
  pre-`Ready` counts. A widget that starts or
  ends threads during the listing can change the space between the
  firehose reading's two snapshots, which fails that reading (two
  `UNACCOUNTED` push ports), never silently; keep the widget count steady
  through the window. The attempts run while widgets render: the unsandboxed
  control's file, process and network attempts then succeed in a helper
  that is serving.
- **When the thread cannot start.** The helper writes `rendering: FAILED
  the probe thread did not start: <errno>; the listing follows here and the
  stream is refused`, counts it as a failure, runs the rest of the probe on
  its serve thread and refuses with that summary.
- **When the helper ends first.** The helper exits when its stream ends,
  Telegram's stop or its 30 s idle exit included, whatever the probe thread
  is doing: a listing without its `end:` row is incomplete. Choose `<ms>`
  so that the window falls while a widget is on screen, and keep it there
  until the `end:` row.

## Mac App Store

In the Mac App Store build (`OS_MAC_STORE`) `TelegramWidgetHelper` is the
XPC service `Telegram Lite.app/Contents/XPCServices/TelegramWidgetHelper.xpc`,
signed with the App Sandbox and the Enhanced Security keys of
`Telegram/Telegram/TelegramWidgetHelper.entitlements` as its only
entitlements, in Debug as in Release, and started by
launchd, not by Telegram: its parent process is launchd, its `HOME` its
own container `~/Library/Containers/<main bundle id>.WidgetHelper/Data`
([Remote engines](test_widget_fixture.md#remote-engines), **The Mac App
Store service**). The probe is the same hook; what differs:

- **Reaching the service.** A Debug `TDESKTOP_WIDGET_ENGINES=probe` or
  `probe-unsandboxed`, or an overlay's direct
  `Platform::WidgetHelper::Launch(WidgetHelperProbeArguments(mode,
  fixture))` as in [Minimal use](#minimal-use), goes through the Store
  `Launch()` in `platform/mac/widget_helper_store_mac.mm`. Once XPC has
  verified the service's reply to the hello, the channel message carries
  the channel, Telegram's descriptor 2 (or, when that is a terminal, the
  helper's end of Telegram's stderr relay) and the probe tail as
  `arguments`, an array of strings sent only when the tail is not empty.
  The service (`_other/widget_helper_service_mac.cpp`) puts the two
  descriptors on 2 and 3 and calls `Run()` on its serve thread with `3`
  and those arguments, so the hook reads the vector a direct-download
  helper reads from argv. The service's environment is launchd's:
  `test-run --env` values reach Telegram only, and only what Telegram
  sends selects the service's behaviour. Every row goes to the stderr
  Telegram passed, `app_stderr.txt` under `test-run`, or, when Telegram's
  stderr is a terminal, through the relay to that terminal, escaped and
  with the service's output past its first 32768 bytes dropped. A listing
  with the full service list (2138 names) passes that bound, so under a
  terminal its full rows come from a run whose stderr is a file, while the
  summary over the channel stays whole
  ([Readings and refusals](#readings-and-refusals)). A Release service
  refuses any argument with `Widget helper: unexpected arguments`; a
  Release Telegram sends none.
- **Parent and descriptors.** "The parent" of `begin`'s `ppid=`, of the
  parent attempts and of the channel check is the channel's peer,
  Telegram: a `parent: pid <pid>, the channel's peer (LOCAL_PEERPID of fd
  3); getppid() = 1` row follows `begin`. When the peer cannot be read the
  row is `parent: FAILED <reason>, getppid() = <pid> used instead`, a
  failure. Descriptors 0 and 1 are accounted as launchd's `/dev/null`
  (`stdin, launchd's /dev/null`, `stdout, launchd's /dev/null`) and 3 as
  the channel to the parent. Descriptor 2 is compared only with the
  probe's own reading before the lockdown, because the App Sandbox
  refuses Telegram's descriptors (`fd 2 before the lockdown: parent's
  unreadable: EPERM(1) Operation not permitted, ...`). It is accounted
  only with `own-terminal=no` and unchanged after the lockdown: as
  `stderr-relay` when it is the one-way stream of Telegram's relay (the
  direct-download test of [Readings and refusals](#readings-and-refusals)
  without the parent comparison), otherwise as `stderr, the stderr
  Telegram passed (...)`, since Telegram's descriptor cannot be read to
  tell it from anything else. A terminal at 2 (`fd 2 (<own>) is a
  terminal`) and a changed one are defects, as is a descriptor at any
  other number. `state:` adds `nproc=<soft>/<hard>`.
- **Mach listing.** libxpc connects before the serve thread exists, so
  the logd and notification-center references read `held-before=yes`,
  the warm-up lookup reads `kr=1100 (Permission denied)` (the App Sandbox
  denies the unregistered name) and adds one own send-receive right, and
  the [firehose reading](#firehose-reading) accepts the push ports in
  both modes. Right after the references, still inside the window that
  `reference lookups left the space as it was` checks, the probe records
  by object the launchd ports the service can get without a lookup and
  releases each right it got: `launchd before the lockdown: bootstrap
  <name> object=<object> special=same; registered <n> of <slots> slots
  (objects ...); parent kr=0 (Success) object=<object>`. `special=` reads
  `none`, `FAILED <kr>` or another name with its object when
  `task_get_special_port(TASK_BOOTSTRAP_PORT)` does not give the
  bootstrap name; the registered ports are the valid slots of
  `mach_ports_lookup()`, the parent what `bootstrap_parent()` of that
  special port returns. A port whose object could not be read adds
  `ERROR <reason>` and a failure. The Store lockdown's last step drops
  the bootstrap port ([Remote engines](test_widget_fixture.md#remote-engines),
  **Lockdown**), so with `probe` the listing reads `mach identities: ...
  bootstrap=0x0` and the row `changed between the reference snapshot and
  the listing: -<bootstrap name> S urefs=<n> object=<its object>`. A
  listed right whose object is one of the recorded ones is a launchd
  port: with `probe` the defect `a launchd port (<label>) after the
  lockdown`, with `probe-unsandboxed` `bootstrap (bootstrap_port)` for the
  bootstrap name and `launchd port (<label>)` for any other. The summary
  counts them as `launchd <n>` after `bootstrap <n>`. The listing (32
  entries on macOS 27.0.1 before the cut) also holds one send right that
  no reference matches, `UNACCOUNTED`, and one send-once right whose
  kernel-object query fails (`IDENTITY QUERY FAILED`, `kr=17`); the
  probe attributes neither. A Store inventory is therefore never clean:
  `inventory: NOT clean (... 1 enumeration/identity failures, 1
  unaccounted)`, and the failed query ends the summary with `this listing
  is INCOMPLETE`. Telegram's own receive rights, read while the service
  lived, hold both objects ([Verification](#verification)): the send
  right is the peer connection's port, the send-once the reply port of
  Telegram's keep request, which
  `xpc_connection_send_message_with_reply()` creates. The kernel reuses a
  freed port, so such objects compare only while both processes still
  hold their rights. The peer connection's port is immovable: in a
  scratch XPC service that mirrors this one, a request that carried it
  as a descriptor killed that service with `OS_REASON_GUARD`
  (`kGUARD_EXC_IMMOVABLE`) and no crash report. Never put that right, or
  any right the listing does not attribute, into a message: a lookup
  "through" it does exactly that.
- **More attempts.** Right after the listing four `launchd` attempts read
  the routes to a launchd right again, releasing what they get: `launchd
  special-port` (`none` or `HELD`), `launchd registered` (`none` or the
  count of valid slots), `launchd parent` (the `kr` code of
  `bootstrap_parent()` of that special port) and `launchd in-space` (the
  listed launchd ports). With `probe` a `HELD`, a registered port or an
  `ok` parent is a defect as well. A call that fails, or launchd ports
  that were not all read before the lockdown, make that attempt
  `INCOMPLETE`. `attemptFiles()` adds `own-container` (the service's
  `HOME`), the positive control that the App Sandbox allows, and
  `user-home`, `user-documents` and `user-downloads` from the part of
  `HOME` before `/Library/Containers/<id>/Data`: `getpwuid()` asks
  opendirectoryd, which the cut leaves unreachable (a scratch reading got
  NULL with `EMFILE`). A `HOME` of another shape makes those twelve
  attempts `INCOMPLETE` with `no user home: <reason>`. After the parent
  attempts `self responsible` reads
  `responsibility_get_pid_responsible_for_pid()` of the service:
  `parent`, `self` or `other`. After the window-server attempts
  `screen-capture preflight` reads `CGPreflightScreenCaptureAccess()`,
  `GRANTED` or `not-granted`, and never requests access or captures.
- **Service listing.** Each `WidgetHelperProbeFixture::serviceNames`
  entry is passed as `service:<name>` after the network targets and gets
  one `attempt service <name>: <policy>/<special>/<libxpc> <detail>` row
  after the service attempts. Every name is read through each lookup
  path, in both modes:
  - `<policy>`: `sandbox_check(pid, "mach-lookup", 2, name)` (2, the
    global name filter), `allowed` or `denied`. It reads the App Sandbox's
    policy, not reachability, so it stays `allowed` after the cut.
  - `<special>`: `bootstrap_look_up()` through what
    `task_get_special_port(TASK_BOOTSTRAP_PORT)` returns after the
    lockdown (read once, released after the listing), its right released
    at once: `ok`, `kr1102` for an unregistered name, `kr1100` for a
    denied one, `kr32` once the cut has broken libxpc's bootstrap pipe.
    After the cut the special port is `0x0`, so the call reads libxpc's
    routine with no launchd right.
  - `<libxpc>`: libxpc's own lookup, the one
    `xpc_connection_create_mach_service()` makes. The connections of each
    batch of 256 names are created on one private serial queue and
    resumed together with no message, so no daemon gets one. A connection
    with no error event within 500 ms reads `xpc-ok`, reachable; a failed
    one reads `xpc<N>` from the `error <N>` of
    `xpc_connection_copy_invalidation_reason()` (macOS 12 and later):
    `xpc159` for a sandbox restriction, `xpc3` for an unregistered name,
    `xpc32` for the broken pipe. Then all of them are canceled. Each one
    after the cut makes libxpc log `failed to do a bootstrap look-up:
    xpc_error=[32: Broken pipe]`.

  The third path, a send right to launchd left in the space, is read by
  object in the listing and by `launchd in-space`, never by a per-name
  lookup through each right: libxpc sends every lookup on its own pipe
  with the given right as the request's domain port, so such a lookup
  says what launchd thinks of the right (an own port nobody receives on
  read `kr112`), not whether the right reaches launchd, and through the
  peer connection's right it kills the service. The rows are not
  attempts of the summary, which gets `services: <n> listed, <a> allowed;
  reachable through the special port <s>, libxpc <x>`. No name, as in an
  environment-selected probe, a `sandbox_check` that answers neither
  way, a special port that cannot be read or a failed libxpc lookup with
  no `error <N>` in its reason (`xpc-error`, every failed one before
  macOS 12) makes a `service listing` attempt `INCOMPLETE`. The `lookup
  <name>` attempts of the service, keychain, camera, microphone and
  window-server groups still look up through the global `bootstrap_port`,
  which the cut clears, so after it they too read libxpc's routine; the
  per-name rows carry the path readings. The names come from outside the
  sandbox. The evidence of
  `2026/11/14/run-mac-app-store-widget-engines-in-an-xpc-service-helper`
  built 2138: the `(global-name "...")` literals of
  `/System/Library/Sandbox/Profiles/application.sb` (176) and
  `appsandbox-common.sb` (32, 31 of them new), then every endpoint of
  `launchctl print system` (705), `user/<uid>` (1428) and `gui/<uid>`
  (1428, externally hosted, none new), deduplicated. The profiles' prefix
  and regex rules are no literals, so a name they allow is listed only
  when it is registered.
- **Controls.** In the Store build `probe-unsandboxed` is still
  App-Sandboxed (`state: sandboxed=1`); it only skips the lockdown
  (`lockdown: skipped (unsandboxed control)`, `nofile` and `nproc` not
  lowered, the bootstrap port kept). Its denials are the App Sandbox's
  own, and what it allows where `probe` is refused (`socket`, `fork`,
  `posix_spawn`, `socket after close`, and `opendir` where the App Sandbox
  allows it) is what the Store lockdown's two limits remove. It reads the
  same lookup paths with the bootstrap port in place, so it is the
  control of the cut: on macOS 27.0.1 it is expected to read `launchd
  special-port: HELD`, `launchd registered: 1`, `launchd parent: ok` and
  `launchd in-space: 1` (the bootstrap entry), its allowed and registered
  names reachable through the special port and libxpc and its denied ones
  `kr1100`/`xpc159`. `own-container` is the positive control of the file
  rows in both modes.
- **Keychain.** No item fixture was made for the Store probe: its
  evidence read an absent run-only account. Before the cut the locked
  service got `status100024`, `kPOSIXErrorBase` plus `EMFILE`: the zero
  descriptor limit stopped its query. The control got `status-25300`
  (`errSecItemNotFound`): the App Sandbox alone lets the service query
  the keychain and get an answer. After the cut the `SecurityServer` and
  `securityd.xpc` lookups fail, so the query has no daemon to ask (a
  scratch service with the same lockdown read `-50`). Telegram's macOS
  sources add no keychain item (only the probe calls `SecItem*`; the
  wallet's Secure Enclave keys are not permanent), so the Store claim
  rests on that reading, those statuses and the per-name rows of the
  keychain services.

Expected readings of `probe` on macOS 27.0.1 arm64: `lockdown: ok`,
`state: sandboxed=1 nofile=0/0 nproc=0/0`, descriptors 0 to 3 (and the
`connect-control` fixture's 4) and nothing else, and one `sandbox
initialization failed: Operation not permitted` line per service on
stderr: libsandbox's report of the `sandbox_init` the Store lockdown
expects to fail with `EPERM`. Then `socket` and `socket after close`
`EMFILE`, `connect-control` `EPERM` with nothing at the listener, `fork`
and `posix_spawn` `EAGAIN`; `client-cwd`, `app-support` (Telegram's
container), `user-home`, `user-documents` and `user-downloads` `access`
`EPERM`, policy `denied`, `opendir` `EMFILE`, while `own-container` reads
`access: ok` and policy `allowed`; `parent kill-0` `EPERM` and `parent
task_for_pid` `kr5`; both device policies `denied`. After the cut:
`launchd special-port: none`, `launchd registered: none`, `launchd
parent: kr32` and `launchd in-space: 0`; every service row
`<policy>/kr32/xpc32`, `services: <n> listed, <a> allowed; reachable
through the special port 0, libxpc 0`; the `lookup` rows of tccd,
tccd.system, locationd, locationd.desktop, SecurityServer, securityd.xpc,
cmio, audiohald and windowserver `kr32`; `windowserver connection: none`,
`microphone devices: ok/0/0` and `screen-capture preflight: not-granted`.

The Store lockdown's last step cuts every new Mach lookup
([Remote engines](test_widget_fixture.md#remote-engines), **Lockdown**).
Before it the locked service could look up 259 of the 2138 names (218
registered, 41 unregistered), among them tccd, cfprefsd, the window
server, CARenderServer, the pasteboard, SecurityServer and securityd,
audiohald, runningboard, trustd, `lsd.mapdb` and the dock, open a window
server connection (`CONNECTED`) and count two audio devices through
audiohald. What it can still reach needs no lookup, and is App Sandbox
reach that `sandbox_init` cannot narrow there (it does not stack); the
direct-download profile denies all of it. These rows succeed, and they are
limits of the confinement, never passes:

- Telegram's process information: `parent task_name_for_pid`,
  `PROC_PIDTBSDINFO`, `proc_pidpath`, `KERN_PROC_PID` and `KERN_PROCARGS2`,
  Telegram's argv and environment. "Cannot inspect Telegram" holds only for
  signals, the task port and the descriptor table.
- The Mach rights it already holds: logd and the firehose push ports, so
  logging keeps working, the notification center and the peer connection
  to Telegram; its task-access special port and its corpse-notify
  exception port, which the kernel keeps and launchd does not serve, left
  in place so that crash reporting works. No probe row sends to them.
- `iokit open: ok` (`IOSurfaceRoot`) and `iokit property: ok`
  (`IOPlatformSerialNumber`): IOKit is a kernel path, not a lookup.
- `self responsible: parent`: TCC would count the service's requests
  under Telegram's grants, but tccd is unreachable, and with neither a
  window server connection nor tccd the screen-capture path is closed
  whatever Telegram holds. `not-granted` only says the copy had no grant.
- `stat` metadata of every path, the denied ones included (the `stat`
  rows), and system files such as `/etc/hosts`, which the App Sandbox
  lets it read (a scratch reading before the evidence; no probe row reads
  those).

Only macOS 27.0.1 arm64 was measured. On macOS 12 to 26 libxpc may hold
its references to the bootstrap port differently: a cut step that fails
there refuses the lockdown. One port has one name in a space, so a
launchd right left under another name is another port, and the `launchd`
rows recognise only the objects recorded before the lockdown. Such a
port would show as a further `UNACCOUNTED` right in the listing, and a
lookup libxpc still serves through it as reachable names in the
per-name `<libxpc>` column, while the lockdown would not refuse it: the
bootstrap name is gone, and a lookup through the cleared port fails with
or without a live pipe.

Each service start's failing `sandbox_init` used to leave libsandbox's
syslog socket, an `AF_UNIX` datagram socket connected to logd, open past
the zero descriptor limit (`fd 5 socket ... peer-pid=<logd> ...
class=DEFECT: unexpected fd 5`). The Store `Lockdown()` now closes every
descriptor that call added, right after it sets the limit, so the listing
reads `fds 5` with the `connect-control` fixture and `defects 0`; such a
socket in a Store probe is a regression of that repair.

The Debug service imports three private names for the probe:
`sandbox_check`, `voucher_activity_get_metadata_buffer` and, in the Store
build only, `responsibility_get_pid_responsible_for_pid`. The Release
service imports none.

## Windows

With `probe` the client appends the same argv tail after the handle, so
the helper runs as `"<helper path>" <handle> probe <cwd> <data> ...`, the
path being `modules\<arch>\TelegramWidgetHelper.exe` next to
`Telegram.exe` ([Remote engines](test_widget_fixture.md#remote-engines)
says how the client builds it). The hook,
`Test::WidgetHelperProbeLockdown()` in
`test/test_widget_helper_probe_lockdown.cpp`, compiled only into the
helper's Debug build, runs `LockDownWithProbe()` of
`test/test_widget_helper_probe_lockdown_win.cpp`, which goes in this
order, once per helper:

1. The native probe of the same file, with the client's working directory
   argument (native separators; empty or missing names no target):
   `PrepareProbeNetwork()` binds the delay-loaded `WS2_32.dll` imports and
   starts its own Winsock and 127.0.0.1 listener, and
   `PrepareProbeTargets()` takes its other `before lowering:` controls,
   while the helper still impersonates its startup token. On the Windows 7
   tier `PrepareProbeToken()` then opens the probe's own `TOKEN_QUERY`
   handle to the helper's process token, which the native probe holds
   until its `process token:` row ([Windows tiers](#windows-tiers)).
2. The harness's own preparation, under the same token: its own
   `WSAStartup`, then its `before lowering:` socket, target and AFD endpoint
   controls and, with `connect-control`, the preopened sockets; without
   Winsock only the AFD endpoint controls
   ([Windows harness lines](#windows-harness-lines)).
3. The real lockdown: the `lockdown` callable, which is
   `WidgetHelper::Lockdown()`, that is
   `Platform::WidgetHelper::LockDownHelper()` in `widget_sandbox_win.cpp`,
   the lowering of the helper's own sandbox tier (**The Windows sandbox
   tiers** in [Remote engines](test_widget_fixture.md#remote-engines)).
   A failure refuses the stream with the lockdown's own text, writes no
   probe line and closes what both probes hold, the Windows 7 tier's token
   handle included; `LockDownWithProbe()` returns before `FinishProbe()`,
   so no reading of a refused helper enters the report, and a diagnostic
   capture of one belongs to the run's own overlay.
4. The harness's `after lowering:` attempts, then the native probe's,
   after which the native probe closes its listener and ends its own
   `WSAStartup`.
5. Every line of the native probe's report, then every harness line, to
   stderr as `Widget helper probe: <line>` (`app_stderr.txt` in a test
   run); then the harness closes every socket it still holds and ends its
   own `WSAStartup`, and the hook returns "".

The helper then serves widgets as in `helper` mode, so a probe launch is no
helper failure. On the hosted opening it serves each whole widget, whose
served host, validator and renderer are built only after Ready and so after
the report ([Remote engines](test_widget_fixture.md#remote-engines)). After
the lockdown the hook calls only import entries bound before it and
functions looked up in modules mapped before it. The import entries are
the helper's static imports; the delay-loaded imports that the lockdown's
warm-up binds in this order, `ADVAPI32.dll` then `bcrypt.dll` on x86 and
x64, and `bcrypt.dll` then `bcryptprimitives.dll` on ARM64, whose
ADVAPI32 is a static import (on x86 and x64 the probe's
`PrepareProbeTargets()` resolves ADVAPI32's `RegOpenKeyExW` and
`RegCloseKey` earlier, on the same impersonating thread);
and the `WS2_32.dll` imports, which the native probe and then the
harness's own Winsock start each bind (the harness uses Winsock only when
its own bind and `WSAStartup` succeeded). The probe's own lookups are
`NtCreateFile` and `RtlInitUnicodeString`, which the harness resolves
from the already mapped ntdll before the lowering,
`GetCurrentPackageFullName`, which `FinishProbe()`'s `ProbePackage()`
resolves from the already mapped kernel32 after it, when it writes the
`package identity` row ([Windows Store](#windows-store)), from Windows 8
on, and on the Windows 7 tier alone `GetProcessWindowStation`,
`GetThreadDesktop` and `GetUserObjectInformationW`, which `ProbeDesktop()`
resolves after it from the `user32.dll` that tier's lowering mapped
([Windows tiers](#windows-tiers)). After the lowering on the other tiers
`FinishProbe()` also calls `ProbeWin32k()` and `ProbeMitigation()` for
its `win32k system calls:` and `mitigation <name>:` rows, and both read
through `Platform::WidgetHelper::ReadMitigation()`, which calls the
`GetProcessMitigationPolicy` pointer that `ResolvedPolicyFunctions()` in
`widget_sandbox_win.cpp` looked up in the mapped kernel32 and cached
before the reversion, at the latest in the warm-up, which refuses
without it there. The Windows 7 tier's warm-up does not look that
function up, and its probe calls neither it nor `ReadMitigation()`: x86
reads its DEP through `GetProcessDEPPolicy`, a static KERNEL32 import of
the x86 helper. Neither those reads nor the lookups bind an import
slot or complete a delay-load entry. The warm-up's descriptors follow each
architecture's Rust serving: x86 and x64 build it for
`i686-win7-windows-msvc` and `x86_64-win7-windows-msvc`, whose std draws
its random bytes through ADVAPI32's `SystemFunction036`, and the
helper's delay-load hook answers that import with the helper's own
adapter over `ProcessPrng`, or `BCryptGenRandom` where Windows has no
`ProcessPrng`; ARM64 builds it for the pc target
`aarch64-pc-windows-msvc`, whose std calls `bcryptprimitives.dll`'s
`ProcessPrng`. **The Windows helper** in
[Remote engines](test_widget_fixture.md#remote-engines) has each
architecture's targets and imports in full. `WS2_32.dll` is a delay
import of the Debug helper alone, for the probe: every `WS2_32.dll`
import of the image is the probe's, a Debug helper started without
`probe` leaves it unbound, and the Release helper has none, with the Nova
engine as with QuickJS before it. A probe helper
therefore maps `WS2_32.dll` and `mswsock.dll`, which the ordinary helpers
did not load in the module readings taken with the QuickJS engine. Neither
probe is an inventory of what the helper holds. The native probe's lines,
their controls and the
measured results are in
[Remote engines](test_widget_fixture.md#remote-engines) (**Windows probe**
and **What the Windows lowering leaves**), and its runs under
[Verification](#verification) and in that page's
[Verification](test_widget_fixture.md#verification). The Release
`TelegramWidgetHelper.exe` has no probe: it refuses any argument after its
token with `Widget helper: unexpected arguments`.

### Windows arguments

The hook reads the client's working directory (index 2 of the arguments,
the token at 0), the loopback port (index 5) and everything from index 6;
the application-support and keychain-account arguments are ignored.

- The arguments are the CRT's narrow argv, which it made from the command
  line in the ANSI code page, and the hook converts each one it reads back
  with `MultiByteToWideChar(CP_ACP)`; a character that code page lacks
  arrives as the CRT replaced it, which no run has tried. A failed
  conversion of the working directory reads `before lowering: client
  working directory: MultiByteToWideChar failed: error <n>` and names no
  target; one of the port or a later argument is an `argument: malformed
  argument <index>: ...` line, and one of the mode refuses with `unknown
  widget helper mode, MultiByteToWideChar failed: error <n>`. An unknown
  mode's text is cut at 64 characters.
- A loopback port of `0`, empty or missing means none. A port from 1 to
  65535 adds four targets: `tcp:127.0.0.1:<port>`, `udp:127.0.0.1:<port>`,
  `tcp:[::1]:<port>` and `udp:[::1]:<port>`.
- From index 6 each argument is `connect-control` or a target,
  `tcp:<address>` or `udp:<address>`. The address is numeric as
  `WSAStringToAddressW` reads it, port included: IPv4 first, then IPv6 in
  brackets with an optional scope, such as `udp:[fe80::1%4]:5000`. No name
  is resolved.
- At most 16 targets, the loopback port's four included, each at most 64
  characters.
- Anything else is one `argument: malformed ...` line and is skipped,
  never a refusal: with or without Winsock a port that is no number from 0
  to 65535 (`loopback port "<text>"`), an unknown argument (`"<text>"`) or
  a target that is too long, and only with Winsock a target that does not
  parse (`WSAStringToAddressW failed: error <n>`), has port 0 (`no port`)
  or is past the limit. Without Winsock those three are never checked: they
  write no line and only count in `<n> unresolved network targets`
  ([Windows harness lines](#windows-harness-lines)).

### Windows harness lines

After the native probe's lines, in this order:

- `arguments: loopback port <port|absent>, <n> network targets,
  connect-control yes|no`, `<n>` counting the targets that passed every
  check below, the ones the attempts use. Without Winsock no target is
  resolved, and it reads `<n> unresolved network targets`, counting every
  target that was not too long, the loopback port's four included.
- The `argument: malformed` lines written while the arguments are read,
  in argument order: a malformed loopback port, an unknown argument, a
  target that is too long.
- `before lowering: harness Winsock: WS2_32 bound, WSAStartup(2.2) ok`, or
  the failed bind or start.
- With Winsock, the `argument: malformed` lines written while the targets
  are resolved, in target order: a target that does not parse, has no
  port or is past the limit. Without Winsock none of these is written,
  and each phase's socket and target lines below are one `sockets and
  targets: not tried, Winsock is not available` line.
- For each phase, `before lowering:` then `after lowering:`, and in each:
  - `socket(<family>, <type>, <protocol>): <verdict>` for `AF_INET` and
    `AF_INET6` with `SOCK_STREAM, IPPROTO_TCP` and `SOCK_DGRAM,
    IPPROTO_UDP`, a fresh socket each, closed at once;
  - `target tcp:<address>: socket <verdict>, connect <verdict>`, with
    `, marker sent (<n> bytes)` after `connect CONNECTED`, and `target
    udp:<address>: socket <verdict>, sendto <verdict>`, a fresh socket per
    target and phase; a socket that cannot be created leaves `connect not
    tried, socket creation was denied` (or `failed`), and one that cannot
    be made nonblocking reads `socket ALLOWED, FIONBIO failed (error <n>),
    connect not tried` (or `sendto`);
  - `AFD endpoint <family>, <type>, <protocol>:
    NtCreateFile(\Device\Afd\Endpoint, EA AfdOpenPacketXX of 57 bytes,
    endpoint flags 0x<flags>): <status verdict>` for the same four kinds;
    the `before lowering:` line adds `EA bytes <hex>`, and the `after
    lowering:` call passes the same buffer. These lines need no Winsock.
    When ntdll lacks `NtCreateFile` or `RtlInitUnicodeString`, each phase
    has the one line `AFD endpoint: not tried, ntdll's NtCreateFile or
    RtlInitUnicodeString is missing` instead.
- With `connect-control` and Winsock, after the other `before lowering:`
  lines, one `connect-control: before lowering: target <target>: preopened
  socket <verdict>` per target: `ALLOWED, unconnected and nonblocking`, the
  creation's `denied (error 10013)` or `failed (error <n>)`, or `ALLOWED,
  FIONBIO failed (error <n>), closed`; only the first keeps a socket. After
  the other `after lowering:` lines, per target in the same order, a kept
  socket's `connect-control: after lowering: target <target>: <connect or
  sendto verdict>; closesocket ok` (or `closesocket failed (error <n>)`)
  and any other target's `connect-control: after lowering: target
  <target>: not tried, no socket was preopened`, then `connect-control: <n>
  preopened sockets used and closed before Ready; this run holds injected
  handles and is no ordinary helper`, `<n>` counting the kept sockets.
  With `connect-control` and no Winsock nothing is preopened, and the only
  line after the other `after lowering:` lines is `connect-control: not
  tried, Winsock is not available`, with no per-target line and no count.

Verdicts:

- A socket call reads `ALLOWED`, `CONNECTED` or `SENT (<n> bytes)` on
  success, `denied (error 10013)` only for `WSAEACCES`, and `failed (error
  <n>)` for any other error (a connect has the further forms below): an
  unavailable family, a bad argument, a refused or unreachable target or
  missing Winsock is never a denial.
- A TCP connect is nonblocking and waits at most 1 s in `select()`:
  `CONNECTED` when `connect()` succeeded at once or `select()` saw the
  socket writable with `SO_ERROR` 0; an immediate error
  other than `WSAEWOULDBLOCK`, or a nonzero `SO_ERROR`, reads as above. No
  completion reads `failed (no completion within 1000 ms)`, and the other
  failures `failed (select error <n>)`, `failed (SO_ERROR unreadable: error
  <n>)` or `failed (SO_ERROR 0 without a completed connect)`. On success
  the socket sends the marker `widget-helper-probe <pid> <phase> <mode>\n`,
  `<phase>` being `before-lowering`, `after-lowering` or `connect-control`;
  a failed or short send reads `connect CONNECTED, marker send failed
  (error <n>)` or `(<sent> of <size> bytes)` instead of `marker sent
  (<n> bytes)`. A UDP target gets the same marker in one `sendto` on
  a nonblocking socket too: `SENT` says the call returned the byte count,
  and only the receiving fixture shows delivery; a send that would block
  reads `failed (error 10035)` at once, never a denial.
- An AFD open reads `ALLOWED (status 0x<NTSTATUS>)` for a success status,
  such as `ALLOWED (status 0x0)`, `denied (status 0xC0000022)` only for
  `STATUS_ACCESS_DENIED`, and `failed (status 0x<NTSTATUS>)` for any other
  status. A created handle is closed at once.

Each TCP target can hold Ready back up to 1 s per attempt (before, after
and with `connect-control`), a UDP target does not wait; the 16-target
limit bounds that delay.

### AFD endpoint attribute

The AFD case opens `\Device\Afd\Endpoint` with `NtCreateFile` the way
Winsock's `socket()` creates an endpoint: one `FILE_FULL_EA_INFORMATION`
named `AfdOpenPacketXX`, 57 bytes in all as `mswsock.dll` sizes it, whose
30-byte value starts with six `ULONG`s: endpoint flags (0 for TCP; 0x11,
`AFD_ENDPOINT_CONNECTIONLESS | AFD_ENDPOINT_MESSAGE_ORIENTED`, for UDP),
group 0, family, type, protocol and a transport name length of 0; the rest
is zero. The layout is ReactOS's `AFD_CREATE_PACKET` with the family, type
and protocol that current `mswsock.dll` writes after the group. The call
asks for `GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE` with
`OBJ_CASE_INSENSITIVE`, share read and write, `FILE_OPEN_IF` and no create
options; Winsock's own call also asks for `WRITE_DAC` and an inheritable
handle, which the probe does not. Only an `after lowering:` denial whose
`before lowering:` line in the same helper read `ALLOWED` with the same
bytes is a denial of endpoint creation.

### What the Windows controls show

The `before lowering:` lines run under the helper's startup token, on the
lowbox tiers a regular AppContainer token of the helper's own package
([Remote engines](test_widget_fixture.md#remote-engines)) and on the
Windows 7 tier the restricted startup token at Low without a lowbox
([Windows tiers](#windows-tiers)), never under an unconfined one: they
are the in-helper control for the same calls, and an AppContainer token
may be kept from other processes' loopback listeners before the lowering
as well. Whether a target was reachable is therefore
decided by the fixture's own connections and datagrams to the same
listeners, from an ordinary process, before and after the helper; a
helper attempt that the fixture did not receive is no denial.
With `connect-control` the helper holds the preopened sockets across the
lowering and the run is an instrumented one, not the ordinary helper.

### Windows tiers

The Windows helper is confined by one of four sandbox tiers, full, LPAC
1703, AppContainer and Windows 7, which Telegram picks from the Windows
version and the helper picks again from its own reading of it (**The
Windows sandbox tiers** in
[Remote engines](test_widget_fixture.md#remote-engines) has the tiers,
their launches and lowerings). The probe reads the helper's own choice,
`CurrentSandbox()` of `widget_sandbox_win.cpp`: the version the helper
read itself (`RtlGetNtVersionNumbers`, or the version a disposable test
overlay simulates in its place) and the tier derived from it, both
cached once per process. Nothing the probe receives names a tier, and
the probe selects none: its rows are readings of the tier the lowering
already applied.

- **The tier row.** Right after `job:` the native probe writes
  `after lowering: tier: <tier>, version <major>.<minor>.<build>`, with
  `full`, `LPAC 1703`, `AppContainer` or `Windows 7` (`none` only names a
  helper without a tier, whose lowering refuses before any row). An
  unforced helper on the host of the records reads `tier: full, version
  10.0.26300`, and a forced one the version its overlay simulated. The
  unforced full-tier report on 10.0.26300 keeps every row it had before
  the tier row, in order and with the same text: its 53 rows with no
  fixture and this one, so a reader compares it with the older full-tier
  readings of that host by setting the tier row aside. That holds for the
  26300 control alone: a full-tier report whose version is below a
  query's floor (**Queries by version** below) reads that row `not
  read`, as a forced 10.0.17763 reads `mitigation shadow stack: not read
  (GetProcessMitigationPolicy(15) is documented from 10.0.19041)`.
- **Queries by version.** The `package identity:`, `win32k system
  calls:` and `mitigation <name>:` rows are read only when the helper's
  version reaches the first one whose documentation has that query:
  `GetCurrentPackageFullName`, and `GetProcessMitigationPolicy` with
  `ProcessDEPPolicy`, `ProcessASLRPolicy`,
  `ProcessStrictHandleCheckPolicy`, `ProcessSystemCallDisablePolicy` or
  `ProcessExtensionPointDisablePolicy`, from 6.2.0 (Windows 8);
  `ProcessDynamicCodePolicy` from 6.3.0 (8.1); `ProcessSignaturePolicy`,
  `ProcessFontDisablePolicy` and `ProcessImageLoadPolicy` from 10.0.10240;
  `ProcessChildProcessPolicy` from 10.0.16299 (1709); and
  `ProcessUserShadowStackPolicy` from 10.0.19041 (2004). Below that
  version the row reads `not read (<call> is documented from <version>)`
  and nothing is called (x86's DEP on Windows 7 aside, below), such as
  `mitigation child process: not read
  (GetProcessMitigationPolicy(13) is documented from 10.0.16299)` on the
  LPAC 1703 tier. A row that is read keeps its form. These are the floors
  of each query, not of the creation flag: where a tier documents the
  query but its creation did not set the policy (`signature` on the
  AppContainer tier below 10.0.10586), the row reads what the process
  has. The outside observer of a forced run reads every policy of the
  Windows 11 host, the rows the probe leaves out included.
- **Windows 7 rows.** The Windows 7 tier has neither the pseudo token
  handles nor `GetProcessMitigationPolicy`, both Windows 8, and no
  `GetCurrentPackageFullName`, so its probe calls none of them:
  - `process token:` reads the handle that `PrepareProbeToken()` opened
    with `TOKEN_QUERY` before the lowering, while the startup token still
    granted it, and the native probe closes that handle right after the
    row, or with the rest of its state when the lowering refuses. A
    failed open reads `process token: OpenProcessToken failed: error <n>
    before the lowering`.
  - Right after the tier row, `desktop: <station>\<desktop> (thread
    <tid>)` names the window station and the desktop of the serving
    thread after the lowering, through `GetProcessWindowStation`,
    `GetThreadDesktop` and `GetUserObjectInformationW(UOI_NAME)` looked up
    in the `user32.dll` that the Windows 7 lowering itself mapped, and
    read by the shared `ReadAssignedDesktop()` of `widget_sandbox_win.cpp`,
    the reading that the lowering compares with its startup request
    before the revert; the probe loads nothing (`desktop: not read
    (user32.dll is not mapped)` otherwise, or the failing call with its
    error). It is the helper's
    own reading under its lockdown token, and that USER calls still work
    there is itself part of what the tier leaves (**The Windows 7 tier**
    in [Remote engines](test_widget_fixture.md#remote-engines)). The
    evidence that the helper runs on its own desktop is the
    outside reading of the same thread's `GetThreadDesktop` from the
    client's window station, not this row. No lowbox tier writes it, and
    no lowbox helper maps `user32.dll`. Telegram labels each helper's
    desktop Low with no-write-up (`S:(ML;;NW;;;LW)`) before the helper
    starts, so that its Low startup token can connect to it (unlabelled,
    the desktop counts as Medium and `user32.dll` fails to initialize
    there with error 1114); a label it cannot set fails the start with
    `SetSecurityInfo failed: <reason>` (or `CreateWellKnownSid`,
    `InitializeAcl` or `AddMandatoryAce` before it), before any helper
    or row exists.
  - `package identity:` and `win32k system calls:` read `not read (... is
    documented from 6.2.0)`, and so does every `mitigation` row but x86's
    DEP: `mitigation DEP: 0x<flags>, permanent <0|1> (GetProcessDEPPolicy)`,
    with `GetProcessDEPPolicy`'s flags (1 DEP, 2 ATL thunk emulation off),
    where the x86 Windows 7 launch requires `0x3, permanent 1`. The x64
    helper's Windows 7 DEP row is one of the rows not read, `mitigation
    DEP: not read (GetProcessMitigationPolicy(0) is documented from
    6.2.0)`: 64-bit processes always run with DEP, and
    `GetProcessDEPPolicy` serves 32-bit processes only.
- **Controls under the startup token.** On the lowbox tiers the
  `before lowering:` directory controls of the client's working directory
  and `%APPDATA%` read `error 5`, as the LPAC checkpoint read them
  (**Windows probe** in
  [Remote engines](test_widget_fixture.md#remote-engines)), so their
  `after lowering:` denials have their control in the client
  ([What the Windows controls show](#what-the-windows-controls-show)).
  The Windows 7 startup token is the restricted token at Low, restricted
  to the user and the enabled groups, with no package, so those controls
  read `ok` where the user may list the folder, Low integrity not
  stopping a read: in the forced Windows 7 probes (simulations on
  Windows 11), the client's working directory and `%APPDATA%` read `ok`
  before the lowering and `denied (error 5)` after it
  ([Verification](#verification)), so on this tier the denials have
  their control in the helper itself.
- **Denials by design.** After the lowering every tier is expected to
  refuse `CreateProcessW` (the lockdown token, the job's one process and,
  from 10.0.10586, the child-process attribute) and every directory and
  registry key the probe opens (the lockdown token's deny-only groups and
  its only restricting SID `S-1-0-0`). The lowbox tiers are expected to
  refuse the fresh sockets and AFD endpoints of every kind the harness
  asks for (the package has no capability): a regular AppContainer
  lockdown token already refused `socket(AF_INET, SOCK_STREAM)` with
  10013 after the lowering in the experiment of
  `2026/11/14/confine-the-windows-widget-helpers-network-with-lpac-and-turn-it-on`
  (its `work/experiment.md`, Q4, the same-binary x86 helper on Windows
  10.0.26300), as the LPAC one did. The Windows 7 tier draws no network
  boundary: the same experiment's restricted Low primary without a
  lowbox read `ALLOWED ... CONNECTED`, so that tier's socket and AFD
  lines are recorded as they read, never judged as denials. Which
  denials each tier actually shows is left to the readings under
  [Verification](#verification).

### Windows Store

In the Windows Store build (`OS_WIN_STORE`: the `uwp`, `uwp64` and
`uwparm` targets) the helper is the same `TelegramWidgetHelper.exe`,
which the AppX package carries in `modules\<arch>\` next to
`Telegram.exe`. An installed package starts
`<package root>\modules\<arch>\TelegramWidgetHelper.exe`, `<package
root>` being its install folder, by default
`C:\Program Files\WindowsApps\<package full name>`, through the same
`Launch()`, hold, pin, tokens, job and creation policies as the other
Windows builds ([Remote engines](test_widget_fixture.md#remote-engines),
**The Windows Store build**). The probe is the same hook with the same
rows. What differs was measured on Windows 10.0.26300.9711 x64 in a
`uwp64` Debug build ([Verification](#verification)):

- **Reaching a packaged client.** The client gets the package's identity
  when it is started through its package. A test installs a stand-in
  identity package that declares an app execution alias and launches
  `workspace.py test-run --exe <alias> --portable-root <root>`
  (**Windows Store test runs** in
  [Remote engines](test_widget_fixture.md#remote-engines)). Through the
  alias the client gets `test-run`'s arguments and `--env` values
  (`TDESKTOP_WIDGET_ENGINES=probe` among them), its redirected stderr
  carries the rows into `app_stderr.txt`, its exit code comes back, and
  it runs with the package's identity. A package activation through
  `IApplicationActivationManager`, the way production starts Telegram,
  gave the client the same identity and the same package job, but it
  passes only arguments (the activated client read no test variable), so
  an environment-selected probe needs the alias route.
- **Command line and job.** The command line starts with the held file's
  DOS final path: `"\\?\C:\Program Files\WindowsApps\<package full
  name>\modules\x64\TelegramWidgetHelper.exe" <handle> probe <cwd>
  <data>`. Every Windows build creates its helper through that path, so a
  reader that finds the helper by its command line must accept the
  `\\?\` prefix. The packaged client ran in a package job whose only limit
  was `JOB_OBJECT_LIMIT_BREAKAWAY_OK` (`0x800`: no silent breakaway, no
  process limit, no UI restriction), so the client's `CreateJob()` adds
  `CREATE_BREAKAWAY_FROM_JOB` and the helper leaves the package job for
  its own: the `job:` row reads as on x64, `in a job 1, limits 0x2408,
  active processes 1, UI 0xff, check passed`. The row reads the immediate
  job only; the package job's members were not listed.
- **`package identity`.** After `job:` and the tier row the native probe
  writes `after lowering: package identity: <reading>` from
  `GetCurrentPackageFullName()`. Unlike the import entries the probe calls,
  all bound before the lowering ([Windows](#windows)), that function is
  looked up after it: `FinishProbe()`'s `ProbePackage()` resolves it from
  the already mapped kernel32 when it writes the row. That lookup binds no
  import slot, and the Debug helper does not import that Windows 8
  function. The row reads `none (APPMODEL_ERROR_NO_PACKAGE)` in a
  process without package identity, as the x86 and x64 non-Store helpers
  read it, the package full name in one with it,
  `GetCurrentPackageFullName failed: error <n>` for any other result, and
  `not available (GetCurrentPackageFullName is missing)` where kernel32
  lacks the function from Windows 8 on. The Windows 7 tier makes no lookup
  and reads `not read (GetCurrentPackageFullName is documented from
  6.2.0)` ([Windows tiers](#windows-tiers)).
  The Store helper read the package's full name: it inherits the identity
  of the package its client runs in. Nothing in the confinement relies on
  its absence; the hold, every token check and every denial held with it.

Measured differences from the x64 non-Store helper, the probe of
`2026/11/15/move-the-windows-widget-helper-into-a-qt-free-executable-and-run-it-on-x64`
(Run 4b) and its outside readings, and from the x64 non-Store helper of
the same tree on the same Windows build; every other row, the harness's
socket and AFD lines included, was equal after normalization:

1. **Package SID.** Called by a packaged process,
   `DeriveAppContainerSidFromAppContainerName` of
   `telegram.desktop.widget-helper` returns a child AppContainer SID of
   the caller's package: `S-1-15-2-`, the package family's seven
   sub-authorities, then the last four of the unpackaged SID
   (`...-153869053-3443934028-2520258059-2811718137`); an unpackaged
   control derived the x64 SID and the family's own. In that 26300.9711
   run the client and helper each called userenv and obtained the same
   SID. The helper's primary was an LPAC token of that SID with no
   capability (`WIN://NOALLAPPPKG` 1). For the current Store derivation
   see **The Windows lowbox** in
   [Remote engines](test_widget_fixture.md#remote-engines).
   On the 19045.6466 Hyper-V guest, five starts of the repaired x64 Store
   Debug package each rendered with one helper. Its observed Low LPAC
   primary had the same child SID as the packaged client's userenv reading
   and the 26300.9711 package, with zero capabilities and no thread token
   (`2026/11/22/prove-the-windows-store-widget-helper-on-windows-10`,
   `evidence/confinement.md` and `evidence/probe.md`).
   An ACE that names the parent package did not admit the helper: with
   the inheritable full-access ACEs that the package's
   `%LOCALAPPDATA%\Packages\<family>` folders carry for the family's
   capability SID (`S-1-15-3-...`), and the same for its package SID
   (`S-1-15-2-...`), set on the leg's root and inherited by the client
   working directory, the helper's `before lowering:` and `after
   lowering:` rows of that folder read `error 5` and `denied (error 5)`,
   while the client's own open of it succeeded: such an ACE admitted the
   helper at neither stage on 26300.9711.
2. **Child process.** `after lowering: CreateProcessW
   C:\WINDOWS\system32\whoami.exe: failed (error 31)` where x64 reads
   `denied (error 5)`. No child was created either way, the `child
   process` row (`0x1`) and the job's one-process limit read back
   unchanged, and the packaged client's own `CreateProcessW` of
   `whoami.exe` in the same run created the process. Error 31 is
   `ERROR_GEN_FAILURE`. The x64 and x86 non-Store helpers of the same
   tree, on the same host and Windows build, read `denied (error 5)`, so
   the error comes with the package; what inside the packaged creation
   path returns it was not traced. By the verdict rules the row is
   `failed`, never a denial: that no child can exist rests on the policy
   and job readings.
3. **Helper identity.** `package identity:` reads the package's full name
   where the non-Store helpers read `none (APPMODEL_ERROR_NO_PACKAGE)`.
4. **Inventory.** Against the same tree's x64 non-Store helpers, outside
   readings of the Store probe helper and of the ordinary Store helper
   both list exactly two more modules, the Microsoft System32 images
   `kernel.appcore.dll` and the KnownDll `combase.dll`, and, of handles,
   three read-only (`KEY_READ`) registry handles, `\REGISTRY\MACHINE`,
   `\REGISTRY\MACHINE\SYSTEM\ControlSet001\Control\Nls\Sorting\Ids` and
   `\REGISTRY\MACHINE\Software\Microsoft\Ole`, 7 unnamed events and
   `EtwRegistration` handles (4 in the probe helper, 10 in the ordinary
   one), and no other handle type: no file, section, port, token,
   process, directory or writable handle. They come with the package;
   the client loads both modules packaged or not. What opens the keys was
   not traced; after the lowering the probe's own
   `HKEY_LOCAL_MACHINE\SOFTWARE` open is denied. Against the move task's
   x64 probe leg the Store probe helper holds 10 more `EtwRegistration`
   handles, 6 of which the same tree's non-Store probe helper holds too,
   with an `apphelp.dll` that no Store helper maps: a change of the
   non-Store probe leg since that record (not traced), not a Store
   difference.

To read a Store probe against the x64 references, normalize paths, pids
and the package full name, set the `package identity` and `tier` rows
aside on both sides (a reference made before either row has none, which
shifts a line-for-line comparison by one per row; the Store readings
above predate the tier row) and compare with the x64 non-Store probe
of the same Windows build: every other row must be equal but for the
`CreateProcessW` row's `failed (error 31)`, which comes with the package
on 26300.9711, and an outside reading differs by the package SID and the
inventory above. Any other difference, an `ALLOWED` after the lowering
above all, is a finding, not a Store difference. The x86 and ARM64 Store
builds have no reference reading.

## Linux

With `probe` or `probe-unsandboxed` the client appends the same argv tail
on both of its spawn paths, so the helper runs as `<helper path> 3 <mode>
<cwd> <data>`. A direct clone executes the descriptor of
`TelegramHelpers/TelegramWidgetHelper` next to the client's binary that
the client opened and checked against its build's SHA-256, or the
package's `libexec/TelegramDesktop/TelegramWidgetHelper` by path in a
package-managed build; inside Flatpak the portal's argv starts
`/app/libexec/TelegramDesktop/TelegramWidgetHelper`
([Remote engines](test_widget_fixture.md#remote-engines) says how the
client finds and checks that path). The hook,
`Test::WidgetHelperProbeLockdown()` in
`test/test_widget_helper_probe_lockdown.cpp`, compiled only into the
helper's Debug build, runs `RunLinuxProbe()` of
`test/test_widget_helper_probe_lockdown_linux.cpp`. It takes the mode,
`cwd` and `application-support` (the data directory below) as the argv
bytes the client passed and ignores every argument after them, so it takes
no fixture. It calls nothing of the helper's Linux sources but the
`lockdown` callable, which is the real `WidgetHelper::Lockdown()`, that
is `Platform::WidgetHelper::LockDownHelper()` in
`widget_lockdown_linux.cpp`. It runs on serve's thread while the helper is
still single-threaded. The Release `TelegramWidgetHelper` has no probe: it
refuses any argument after its token with
`Widget helper: unexpected arguments`. Every line is one `write` to
descriptor 2, prefixed `Widget helper probe: `. On both spawn paths that
descriptor is a duplicate of the client's stderr when that is not a
terminal, so in a test run, whose stderr is a file, the lines land in
`app_stderr.txt` unchanged. When the client's stderr is a terminal it is
a one-way socket whose other end the client reads and relays to its
terminal: every byte outside printable ASCII escaped as `\xNN` in
lowercase hex and `\` as `\\`, a line over 8192 bytes cut there and
ended with ` [cut]`, and a helper's bytes past its first 32768 dropped,
with one line `[widget helper output over 32768 bytes dropped]` in their
place. A printable ASCII line reaches the terminal unchanged
([Remote engines](test_widget_fixture.md#remote-engines) describes the
relay). Unless the helper's own lockdown refuses, the hook returns "" and
the helper serves widgets as in `helper` mode, so a probe launch is no
helper failure. Both probe values choose the helper as `helper` does, so
the probed helper renders each widget it serves, and its `lockdown:`,
`after lockdown:` and `layers:` lines are those of a helper that then
renders.

With `probe` it goes through these steps:

1. Before any copy or lockdown, the helper reads:
   - `pid`: `getpid()`;
   - `uid_map`: the first line of `/proc/self/uid_map` as
     `inside:outside:count`, `init` for the initial namespace's
     `0 0 4294967295`, `none` when the map is empty or unreadable;
   - `interfaces`: the interface names in `/proc/self/net/dev`,
     comma-separated, `none` without any;
   - `pdeathsig`: `PR_GET_PDEATHSIG`;
   - the Landlock ABI:
     `landlock_create_ruleset(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION)`,
     or the error name of its failure;
   - `sandbox`: `flatpak` when `/.flatpak-info` exists, else `snap` when
     `SNAP` is set, else `none`.
2. It forks three copies of itself, one at a time. Each copy sets
   `PR_SET_PDEATHSIG(SIGKILL)` first, then makes itself non-dumpable with
   `PR_SET_DUMPABLE(0)`, so its deliberate death writes no core dump; it
   never touches the channel it inherits and ends with `_exit`. The helper
   polls for the copy's end every 10 ms for up to 2000 ms; a copy still
   running then is killed with `SIGKILL` and waited for. The copy's status
   text comes from its wait status: `killed by signal <N>`, `succeeded` for
   exit 0, `lockdown refused` for exit 255, `returned <E>` for any other
   exit, which carries the attempt's errno, or `status <N>`. A copy that
   outlived the 2000 ms reads `no end in 2000 ms, killed: <status text>`,
   with the status reaped after the kill, so a copy that was already dying
   keeps its own reading after the colon. `fork failed: <E>` and
   `waitpid failed: <E>` stand for a copy it could not run or wait for.
   - (a) "landlock alone". The copy sets `PR_SET_NO_NEW_PRIVS` and
     installs an outer filter that answers `seccomp(2)` and
     `prctl(PR_SET_SECCOMP, ...)` with `EPERM` and allows every other
     call, every call of another architecture included. Then it calls the
     real lockdown. Every step before the seccomp install runs as in the
     serving helper, the empty root, `capset`, no-new-privileges and
     Landlock included; the install fails both ways, so the lockdown
     returns `prctl(PR_SET_SECCOMP) failed: EPERM` and leaves Landlock in
     force with no seccomp filter of its own. The copy opens `.`, the
     client's `cwd` and the data directory read-only as directories,
     reads the root with `access("/proc", F_OK)` (`ENOENT` means `empty`,
     anything else `kept`) and writes `landlock alone: cwd=<r>
     client-cwd=<r> data=<r> root=<empty|kept> lockdown=<refusal>`, each
     `<r>` `opened` or the open's error name, `lockdown=ok` should the
     install not fail. Its `cwd` reading and root go back to the helper
     through a shared anonymous page. When it cannot install the outer
     filter, it writes `landlock alone: no outer filter: <error>` and
     calls no lockdown. When the copy ends any other way than exit 0, the
     helper writes `landlock alone: <status text>`, and it writes
     `landlock alone: mmap failed: <E>` when it cannot map the shared page,
     so a missing reading is never silent.
   - (b) The copy calls the real lockdown, then
     `syscall(__NR_execve, "", NULL, NULL)`; the helper writes `execve:
     <status text>`.
   - (c) The copy calls the real lockdown, then
     `syscall(__NR_socket, AF_UNIX, SOCK_STREAM, 0)` and closes a socket it
     got; the helper writes `socket: <status text>`.

   When the lockdown of copy (b) or (c) refuses, the copy writes `<name>:
   lockdown refused: <refusal>` itself and exits 255, before the helper's
   `<name>: lockdown refused`.
3. It opens `/proc/self/status` read-only, then calls the real lockdown
   once. A refusal writes `lockdown: FAILED <refusal>`, closes the
   descriptor and returns the refusal, so the stream is refused as without
   the probe and no line follows. Otherwise it writes `lockdown: ok`.
4. It reads the status descriptor and closes it, opens `.`, `cwd` and the
   data directory again, lists descriptors 0 to 4095 and writes `after
   lockdown: cwd=<r> client-cwd=<r> data=<r> descriptors=<list>`, the list
   comma-separated, `none` when empty.
5. It writes `layers: seccomp=<t> landlock=<t> nnp=<t> caps=<t>
   seccomp_filters=<t> pdeathsig=<n> pid=<n> uid_map=<t> interfaces=<t>
   root=<t> sandbox=<t>`:
   - `seccomp` is `on` when the status read `Seccomp:` 2, `nnp` is `on`
     when it read `NoNewPrivs:` 1, and each is `off` otherwise;
   - `caps` is `none` when `CapPrm` and `CapEff` are both zero, else
     `kept`;
   - `seccomp_filters` is the `Seccomp_filters:` value;
   - each of these four is `unknown` when its field is missing, as
     `Seccomp_filters` is before kernel 5.9, or the status could not be
     read;
   - `landlock` is `off(<E>)` when step 1's ABI query failed, and
     otherwise comes from copy (a): `abi<N>` when its `cwd` read `EACCES`,
     `abi<N>,not-enforced` when it read anything else, `unknown` when the
     copy gave no reading;
   - `root` comes from copy (a): `empty`, `kept`, or `unknown` without its
     reading;
   - `pdeathsig`, `pid`, `uid_map`, `interfaces` and `sandbox` come from
     step 1.
6. It returns "".

With `probe-unsandboxed` neither the copies nor the helper call `lockdown`
or install the outer filter: copy (a)'s line ends `lockdown=skipped`,
copies (b) and (c) make their attempts unconfined, and step 3 writes
`lockdown: skipped (unsandboxed control)`. That helper then serves its
widgets unconfined. It is the control of every reading: the same calls
reach the kernel in the same build, so a `probe` line that reads like its
control shows a layer that did not apply. Only a Debug client passes the
mode, and only a Debug helper runs it: the Release helper refuses it. For
the confined Snap's probe ([Verification](#verification)) disposable
overlays opened those gates in a Release build made for that campaign
only.

Where each reading is taken:

- `execve` and `socket` end the process under the production filter, so
  they are tried in copies. Landlock's enforcement shows only where no
  seccomp filter of the helper's own answers a path call first, so copy
  (a) stops the lockdown at the install. The root is read in that copy
  through `access(2)`, which Landlock does not restrict and the serving
  helper's filter answers with `ENOSYS`. The copies run the same
  `Lockdown()` in the same process image, just before the helper's own
  lockdown: these readings are the copies', not the serving helper's.
- The serving helper's seccomp, no-new-privileges and capability state
  after its lockdown come from the status descriptor it opened before it,
  read with `read`, which the filter allows. The descriptor is closed
  before the listing, so the listing stays exact.
- `pdeathsig` is read in the helper before the lockdown. Dropping
  capabilities is no credential change that clears it.
- The listing peeks at each descriptor with
  `recvfrom(fd, &byte, 0, MSG_DONTWAIT | MSG_PEEK, NULL, NULL)`: `EBADF`
  means closed, any other answer open. The production filter allows that
  call, and it consumes nothing. The filter lets `recvfrom` through only
  with these flags or 0 and no address; other flags or an address get
  `ENOSYS`, which would read every descriptor as open.
- After its lockdown the helper only reads, writes, peeks and allocates.
  Its opens are expected to fail with `ENOSYS`.

The readings below for a direct spawn with user namespaces, a helper the
Flatpak portal spawned and the control on a namespaced direct spawn were
confirmed on one host for `TelegramWidgetHelper` and, before it, for the
same-binary helper it replaced; the plain child's for the same-binary
helper and, with its control, for `TelegramWidgetHelper` inside a
strictly confined Snap ([Verification](#verification)). The others are
expectations. They assume a kernel with Landlock ABI
`<N>`, a client under no seccomp filter of its own, and the client's
working and data directories present in the client.

- A direct spawn with user namespaces:
  - `landlock alone: cwd=EACCES client-cwd=ENOENT data=ENOENT root=empty
    lockdown=prctl(PR_SET_SECCOMP) failed: EPERM`: the client's paths do
    not exist under the empty root;
  - `execve: killed by signal 31` and `socket: killed by signal 31`
    (`SIGSYS`);
  - `lockdown: ok`;
  - `after lockdown: cwd=ENOSYS client-cwd=ENOSYS data=ENOSYS
    descriptors=0,1,2,3`;
  - `layers: seccomp=on landlock=abi<N> nnp=on caps=none
    seccomp_filters=1 pdeathsig=9 pid=1 uid_map=0:<uid>:1 interfaces=lo
    root=empty sandbox=none`, `<uid>` being the client's effective user
    id.
- A plain child, after the main log's `Tgw: The widget helper starts
  without namespaces: clone failed: <reason>.`:
  - `landlock alone: cwd=EACCES client-cwd=EACCES data=EACCES root=kept
    lockdown=prctl(PR_SET_SECCOMP) failed: EPERM`;
  - the same `execve`, `socket`, `lockdown` and `after lockdown` lines;
  - the same `layers:` line but for `pid=<its pid>`, `uid_map=<the
    client's>` (`init` in the initial user namespace), `interfaces=<the
    client's>` and `root=kept`;
  - inside a strictly confined Snap, where AppArmor refuses the clone
    (`clone failed: Permission denied`), also `seccomp_filters=3`, one
    more than the client's two there, `sandbox=snap`, and `uid_map=none`
    instead of `init`: AppArmor denies the helper's read of
    `/proc/<pid>/uid_map` (the snap's profile has no rule for it),
    although the helper shares the client's initial user namespace.
- A helper the Flatpak portal spawned:
  - `landlock alone: cwd=EACCES client-cwd=<r> data=<r> root=kept
    lockdown=prctl(PR_SET_SECCOMP) failed: EPERM`: the portal's sandbox
    refuses the empty root's steps, and `client-cwd` and `data` read
    `ENOENT` where it lacks the client's paths, `EACCES` where it has
    them;
  - the same `execve`, `socket`, `lockdown` and `after lockdown` lines,
    the copies' wait statuses being the helper's own, not bubblewrap's;
  - `layers: seccomp=on landlock=abi<N> nnp=on caps=none
    seccomp_filters=<one more than the sandbox's own> pdeathsig=9
    pid=<n> uid_map=<the sandbox's> interfaces=lo root=kept
    sandbox=flatpak`.
- A system that lets the namespaced clone through but refuses part of
  what follows reads that part: `uid_map=none` for refused maps,
  `root=kept` for a refused mount step.
- A kernel without Landlock reads `landlock=off(ENOSYS)`, or
  `off(EOPNOTSUPP)` when Landlock is off at boot, and copy (a)'s opens
  succeed wherever the path exists.
- The control on a direct spawn: `landlock alone: cwd=opened
  client-cwd=opened data=opened root=kept lockdown=skipped`, `execve:
  returned ENOENT`, `socket: succeeded`, `lockdown: skipped (unsandboxed
  control)`, `after lockdown: cwd=opened client-cwd=opened data=opened
  descriptors=0,1,2,3` and `layers: seccomp=off landlock=abi<N>,not-enforced
  nnp=off ... seccomp_filters=0 ... root=kept`, with `caps=kept` in a
  namespaced helper, which is root of its user namespace, and `caps=none`
  in a plain one. Inside Flatpak, bubblewrap's own filter and
  no-new-privileges make the control read `seccomp=on` and `nnp=on`.
  Inside a strictly confined Snap, the client's two filters there make
  the plain child's control read `seccomp=on` and `seccomp_filters=2`,
  with `nnp=off`.

Limits:

- `client-cwd` and `data` show Landlock only where the path exists in the
  copy: a directory missing in the client, or any client path under an
  empty root, reads `ENOENT` whatever Landlock does, in the control as
  well. `cwd`, the helper's `/`, always exists, and it is the reading the
  `landlock=` token uses.
- Copy (a)'s readings assume its lockdown refused only at the install.
  Any other `lockdown=` text names a step that refused before it, so
  Landlock or the empty root may not have applied in that copy.
- The status reading covers only what `/proc/self/status` shows: Landlock
  leaves nothing there, so the serving helper's own Landlock domain is
  inferred from the copy's reading, not read.
- Every helper of a probe launch probes, since the client passes the mode
  to each one it starts.

## Minimal use

An overlay that wants controlled claims prepares the fixtures, then drives the
product launch and host end directly:

```cpp
auto fixture = Test::WidgetHelperProbeFixture{
	.keychainAccount = account, // a run-owned generic password
	.loopbackPort = port, // a live QTcpServer on 127.0.0.1
};
auto launched = Platform::WidgetHelper::Launch(
	Test::WidgetHelperProbeArguments(u"probe"_q, fixture));
// Tgw::PrepareResources() gave `resources`; the ended reason is the summary.
auto end = twidget::SandboxRemoteHostEnd::start(
	std::move(launched.stream),
	*resources,
	std::to_string(AppVersion),
	{ .ended = [=](const std::string &reason) { /* store it */ } },
	{});
// Once `ended` came: end->stop(); launched.complete(1000, 1000);
```

The overlay owns the keychain item of `kWidgetHelperProbeKeychainService`.
It reads first, adds the item only when that read reports
`errSecItemNotFound` and never replaces an item. It reads the item back
itself as the main-process control, deletes it in `Runner::onFinish` and
checks it is gone. A login-keychain item the client created is readable
without UI only by the client's own code identity, and
`TelegramWidgetHelper` is a separate one: even the unsandboxed control
answers `status-25293` (`errSecAuthFailed`) for the present item, also
when the item's access list names the helper. Its item claim is therefore
a contrast: the same pair once more with an account that has no item,
where the unsandboxed control answers `status-25300`
(`errSecItemNotFound`) while the locked helper answers `status-50` for
both accounts, its keychain lookups `kr1100`. The listener records each
line it receives. Launch the same legs with `probe-unsandboxed` as the
control. With `.connectControl = true`, the helper preopens one TCP socket
before the lockdown and connects it to the listener afterwards. That run
lists the injected descriptor, so it never counts as a clean inventory.
With `.renderingDelayMs = <ms>` the same launch is the serving variant: a
probe helper whose lockdown succeeded answers `Ready`, so the overlay drives
the product's own widget path rather than a bare host end, keeps the widget
rendering through the window and reads the `summary:` and `end:` rows from
stderr, since no refusal carries the summary then
([Serving while rendering](#serving-while-rendering)).

### Minimal use on Windows

A Windows overlay takes the same route with parent-owned listeners: TCP and
UDP listeners on 127.0.0.1 and [::1] at one port, and listeners of its own
on any further numeric target, such as an address of this host's own
interface. It never targets another device or a public service and changes
no firewall rule or loopback exemption.

```cpp
auto fixture = Test::WidgetHelperProbeFixture{
	.loopbackPort = port, // run-owned TCP and UDP on 127.0.0.1 and [::1]
	.networkTargets = {
		u"tcp:%1:%2"_q.arg(own, QString::number(tcpPort)), // run-owned
		u"udp:%1:%2"_q.arg(own, QString::number(udpPort)), // run-owned
	},
};
auto launched = Platform::WidgetHelper::Launch(
	Test::WidgetHelperProbeArguments(u"probe"_q, fixture));
// Start the host end as above: this helper answers Ready and serves.
// When the listeners have drained: end->stop(); launched.complete(1000, 1000);
```

Each listener drains every connection and datagram and records it with its
marker and time. Ordinary connections and datagrams from the client to the
same listeners right before the launch and after the helper ended are the
reachability control; a helper marker that arrives after the lowering
(`after-lowering` or `connect-control`) is a counterexample, not a pass.
The report is in the client's stderr (`app_stderr.txt` in a test run), so
the run reads it after the client exited. A second launch with
`.connectControl = true` is the injected-socket leg: it holds one preopened
socket per target whose preopen succeeded until the hook closes them
before Ready, so it is never the ordinary-helper inventory, which a launch
without the fixture gives.

## Readings and refusals

Every row goes to the helper's stderr: the client's own when that is not a
terminal (`app_stderr.txt` in a test run), and the client's relay to its
terminal otherwise (the paragraph on terminal runs below). Each row is one
`write`, cut past 2048 bytes with a marker, prefixed `widget-helper-probe
<pid> <mode>: `. The rows come in this order:

- `begin`, `paths` and `fd 2 before the lockdown`: the fd 2 identities of
  the parent and the helper, read before the lockdown, then
  `own-terminal=yes|no`, the helper's own `isatty(2)` at that time.
- `pre` rows of the first Mach snapshot; `warm-up lookup <name>: lookup
  kr=1102 (Unknown service name)` and one `first bootstrap lookup added:
  +<entry>` row per entry the warm-up added (or `first bootstrap lookup
  changed nothing: ...`); one `reference <service>` row per lookup with
  its object and `held-before=`; and whether the snapshot after the
  references equals the warmed one (names, rights, urefs, objects).
- `lockdown: ok|FAILED|skipped` and `state: sandboxed=<sandbox_check>
  nofile=<soft>/<hard>`.
- In the serving variant only: the first `rendering:` row, then, from the
  probe thread after the delay, `rendering: listing begins ...` before the
  listing below and `rendering: listing ends ...` after it
  ([Serving while rendering](#serving-while-rendering)).
- One `fd` row per entry of `PROC_PIDLISTFDS`. Then the `mach:` count, one
  `changed between the reference snapshot and the listing: <entry>` row per
  difference, the `mach identities:` row, the `firehose:` row of the
  [firehose reading](#firehose-reading), and one `mach` row per
  `mach_port_space_info` entry, tree entries included, each with its
  `class=`.
- One `attempt <name>: <code> <detail>` row per attempt.
- In the serving variant only: `summary: <summary>`, the whole summary in
  one row that is never cut.
- `end: rows=<n> fds=<n> mach=<n> attempts=<n> cut-rows=<n> stderr=ok|FAILED`.

The warm-up is a failure, which leaves the listing incomplete, only when
the unregistered name returned a port (`ERROR the unregistered name
returned a port`; the port is released), when the snapshot after it
failed, or when it removed or changed an existing entry (`first bootstrap
lookup changed: <entry>` rows). Added entries are expected. The summary's
`stable=yes` needs the pre-lockdown, warmed and post-reference snapshots
and no change by the reference lookups.

Each `fd` row gives the kind, access and close-on-exec flags. A vnode row
adds `F_GETPATH` and `fstat` (after the lockdown the sandbox refuses `fstat`
on write-only `/dev/null`: `fstat=EPERM`). A socket row adds its domain,
type, protocol, kind, `state=0x<hex>` (the socket's `soi_state` from
`PROC_PIDFDSOCKETINFO`) and, for `AF_UNIX`, the peer pid. Accounted
descriptors are 0 `/dev/null` read, 1 `/dev/null` write, 2 the client's
stderr or its relay, and 3 an `AF_UNIX` stream whose peer is the parent.
Anything else is a defect.

Descriptor 2 is accounted only when it was no terminal before the lockdown
(`own-terminal=no`) and is unchanged after it (the same path for a file,
the same object otherwise), and then only as one of two things:

- the same file as the parent's fd 2, `stderr` (the Mac App Store
  service, which cannot read the parent's, takes as `stderr` any such
  descriptor that is not the relay below);
- the one-way stream of the client's relay, `stderr-relay` (`stderr relay,
  AF_UNIX stream to the parent (receive side shut; same object after
  lockdown)`): an `AF_UNIX` stream socket whose peer is the parent and
  whose state has `SOI_S_CANTRCVMORE` (0x20), since the client shut its
  own sending side. That bit tells it from the channel at 3, which is
  duplex.

A terminal at fd 2 is a defect, `fd 2 (<identity>) is a terminal`, even
when it is the parent's fd 2, and so is a changed one (`fd 2 differs from
the one read before the lockdown`); in the direct-download helper so is
any other (`fd 2 (<own>) is not the parent's fd 2 (<parent>)`). The
probe identifies the stream, not where it leads: that the client relays it
to its terminal, and that the helper's fd 2 is not that terminal, is shown
from the client's side, by its own `isatty(2)`, `lsof -p` of both
processes (`unix` at the helper's fd 2, paired with a socket of the
client) and a capture of the terminal's bytes. The Mac App Store
service's parent, accounting, `state:` and extra rows differ
([Mac App Store](#mac-app-store)): its fd 2 accounting uses the same
`own-terminal=` reading and the same relay test, but it never compares
with the parent's fd 2, which it cannot read.

Each `mach` row gives every rights bit, urefs, the object hash and the kernel
object type. Receive rights also get their status, port sets their members.
The classes are matched against actual self references:

- task (`mach_task_self()`), threads (`task_threads()`), the unprivileged
  host (`mach_host_self()`) and the task name port;
- the kernel semaphore, clock and voucher objects;
- `bootstrap_port`, own receive and send-receive rights, port sets and dead
  names;
- `libSystem service com.apple.logd` and `com.apple.system.notification_center`;
- `libSystem firehose push port`, only for the two names that the firehose
  reading of the same helper accepted;
- in the Mac App Store build, a right to one of the launchd ports read
  before the lockdown, by object ([Mac App Store](#mac-app-store)): a
  defect with `probe`, `launchd port (<label>)` or the bootstrap class
  with `probe-unsandboxed`.

A right to any other reference service, any other kernel object, a thread or
task port that is not the helper's own, or a failed identity query is a
defect or a failure. Any other send right is `UNACCOUNTED: a send right to no
reference service`, with no guess at its owner.

Attempt codes are `ok`, an errno name, `kr<n>`, `status<n>`, `denied` or
`allowed` for `sandbox_check` readings, or `INCOMPLETE` with the missing
fixture or with the framework that did not load. With the zero descriptor
ceiling, `socket`, `opendir` and `open` report `EMFILE`. `stat`, `access`
and `file-read-data` show the profile's own denial.

The refusal is `the engine process refused the stream: widget helper probe
...`, kept under 3900 bytes. It holds the pid, mode, lockdown and state,
`inventory: clean` or `inventory: NOT clean (<reasons>)`, the fd and per-class
Mach counts (`firehose push ports` among them), every failure, defect, failed
firehose reading and unaccounted entry, and every attempt as `name=code`. It
ends by naming the stderr rows and their last row. `inventory: clean` needs a
successful lockdown in `probe`, no failure, defect, unaccounted entry, cut row
or write error, and no injected fixture. A clean inventory thus accounts for
libtrace's two push ports only through the reading made in the same helper;
any other unknown send right still makes it not clean. `unaccounted: none`
appears only on a complete listing. Otherwise it reads `unaccounted: unknown
(listing incomplete)`. When items do not fit, the summary says `SUMMARY
CUT ... INCOMPLETE` and the stderr listing stays authoritative, if it is
complete itself.

In a run whose client stderr is a terminal the rows reach that terminal
through the client's relay
([Remote engines](test_widget_fixture.md#remote-engines), **The shared
helper**): every byte outside printable ASCII escaped, a line over 8192
bytes cut, and the helper's output past its first 32768 bytes replaced by
the one line `[widget helper output over 32768 bytes dropped]`. A
direct-download probe's rows come close to that bound, so the end of the
listing, the attempt rows and the helper's `Widget helper:` line with the
refusal can be cut there; a cut transcript is not the complete listing.
A Mac App Store probe with the full service list, one row per name,
passes it: its `fd` rows, every other row before the listing and only
about its first 30 service rows come early enough to reach the terminal,
while the rest of the listing, the rows after it and its `Widget helper:`
line do not. One with no names stayed inside it, its `Widget helper:`
line included ([Verification](#verification)).
The channel still carries the refusal, untouched by the relay: the manager
puts it in the main log's `Tgw: Widget helper <pid> stream ended: ...`
line, while an overlay that launches the helper itself
([Minimal use](#minimal-use)) gets no manager line and must save the
reason its `ended` callback received. A summary that says `SUMMARY CUT`
next to a cut transcript leaves the inventory undecided: take it from a
run whose stderr is a file.

### Firehose reading

The probe reads libdispatch's firehose buffer header right after the snapshot
that the `mach` rows list, before the descriptor listing, any listing row and
every attempt:

1. logd's name is the listing's send right whose object is the found
   `com.apple.logd` reference's. The name the lookup returned is not used:
   it was released before the lockdown.
2. libdispatch's exported `voucher_activity_get_metadata_buffer()` returns
   the libtrace metadata that ends the header, which is the 4096-byte chunk
   ending there. The pointer, length and chunk end are checked first, and
   `mach_vm_region()` on the helper's own task must show the whole chunk
   mapped and readable before any word of it is read. An object-name right
   that call returns is released before the reading ends.
3. The words before the metadata are copied once and scanned for every place
   where logd's name is followed by two valid names. Exactly one is
   required. In the observed layout that place is the header's logd port, its
   two push ports, then the buffer's own port (the row's `next word`).
4. A second snapshot, taken right after the reading, must equal the listed
   one.
5. The two names must differ from each other and from logd, and each must be
   a plain send right in the listing that no earlier class takes: not
   `mach_task_self()` or `bootstrap_port`, no kernel object and no reference
   service.

Only then are both names classified; otherwise neither is. The accepted row
reads `firehose: libtrace push ports <a> <b> accepted, ...` with the header
range, logd's name, the match count, the offset and the word after the pair,
and the confirming snapshot's entry count. A failed reading writes `firehose:
FAILED <reason>; no right is classified by it`, with the reading when there
was one, and adds `firehose: FAILED <reason>` to the summary before the
unaccounted items. Its reasons are `no logd name: ...`, `no firehose header
(...)`, `firehose header ... not readable: ...`, `logd <name> found <n> times
in the firehose header`, a changed or unreadable space after the reading, and
a candidate that repeats the other name or logd's, is absent from the
listing, is not a plain send right or belongs to an earlier class. A failed
reading is not an enumeration failure: the listing stays complete, and its
`UNACCOUNTED` rows, not the reading, make the inventory not clean. When the
Mach listing itself failed, nothing is read and no `firehose:` row is
written.

The reader asks for no right and reads only the helper's own memory, but the
private export can lazily initialize libdispatch's buffer in a process whose
logging has not started yet (a logd lookup and the buffer's own ports). The
confirming snapshot catches a net change in the space, not a transient
allocation. In the same-binary macOS helper the traced runs under
[Verification](#verification) found the buffer initialized before `main`,
by Qt's `+load` logging. `TelegramWidgetHelper` logs nothing before its
lockdown: its pre-lockdown and reference snapshots hold no right to logd,
notifyd or a push port. In `probe` those four rights and one more own
send-receive right, the buffer's own port (the firehose row's `next
word`), first appear during the real lockdown, as five `changed between
the reference snapshot and the listing` rows, as expected from
`sandbox_init`, which acquires that logging set itself in a process that
had none (`2026/11/10/run-widget-engines-in-a-sandboxed-helper-process-on-macos`,
its `work/decisions.md`, D12). In `probe-unsandboxed` nothing has logged
by the listing, so the reading stops before it calls the export:
`firehose: FAILED no logd name: the listing holds no send right to
com.apple.logd's port (object <hash>)`. That control's listing stays
complete with no `UNACCOUNTED` row, and its verdict stays exactly
`inventory: NOT clean (unsandboxed control)`. Both are observations on
one macOS, not a guarantee for every future helper. The scan relies on a
private layout and cannot prove it: a changed layout can leave no single
match, which only fails the reading, or match unrelated words, so a reading
on another macOS is trusted only once its names were joined with that
helper's own rows.

## Lifetime and cleanup

The probe keeps no state. It releases every array, looked-up right, task,
thread and host right, IOKit object and CF object it takes, and any
object-name right `mach_vm_region()` returns. It reaps a successful `fork`
or `posix_spawn` child within 2 s (`SIGKILL` and another second otherwise).
Near its end it closes stdin and tries a socket again. The frameworks it
loaded stay loaded until the helper exits.
The helper exits after the refusal. In the serving variant the probe thread
owns the probe and releases it after its `end:` row, while the helper goes
on serving; a connect-control run keeps its preopened socket until then. The
overlay owns the keychain item, the listener and the restoration of
`SecKeychainSetUserInteractionAllowed`.

On Windows the hook closes each fresh socket and AFD handle right after its
attempt and each preopened socket right after its `after lowering:` use
(one that could not be made nonblocking at once, before the lowering);
before it returns, and on a lockdown failure, it closes whatever it still
holds and ends its own `WSAStartup`, so no harness socket survives into
Ready. The native probe closes its listener and ends its own start after
its `after lowering:` attempts, and on a lockdown failure too. On the
Windows 7 tier it closes its own process token handle right after its
`process token:` row, and on a lockdown failure with the rest of its
state, so that handle, too, never reaches Ready. The overlay owns its
listeners and closes them after the helper ended.

On Linux the probe keeps no state either. Each forked copy sets
`PR_SET_PDEATHSIG(SIGKILL)`, makes itself non-dumpable and ends only with
`_exit`, so the deliberate deaths of copies (b) and (c) write no core dump
to the host's crash handler (systemd-coredump, apport, or WSL's crash
folder); the helper reaps it within 2 s, polled every 10 ms, and otherwise
kills it with `SIGKILL` and reaps it with a blocking wait before the next
copy starts. The shared page
of copy (a) is unmapped once read, and the `/proc/self/status` descriptor
is closed before the listing. The helper then serves widgets and ends like
any other helper.

## Verification

The first two macOS campaigns probed the same-binary macOS helper, the
client's own `Contents/MacOS/Telegram -widgethelper 3` that
`TelegramWidgetHelper` later replaced, whose Qt and AppKit had started
before `main`. Of the three Windows campaigns, all run with the QuickJS
engine before the Nova change, the first probed the
same-binary Windows helper, the client's own `Telegram.exe -widgethelper
<handle>`, the second `TelegramWidgetHelper.exe`, which replaced it, and
the third that helper in the Windows Store build's package. The
two Linux campaigns are the last ones below: the first probed the
same-binary Linux helper, the client's own
`/proc/self/exe -widgethelper 3`, the second `TelegramWidgetHelper`, which
replaced it.

The serving variant ([Serving while rendering](#serving-while-rendering))
and the client's `renderingDelayMs` were added by
`2026/11/14/run-widget-validators-and-renderers-in-the-macos-helper` and
compiled in the macOS Debug build of both halves (arm64 and x86_64; the
Debug helper still loads only libSystem and libc++). They have not run
yet: that task's evidence campaign takes the direct and the Mac App Store
listings while a widget renders, against the engine-only control, and its
records are the first. The ordinary probe's rows are unchanged but for the
`begin:` row's `rendering=no`.

Executed on macOS 27.0.1 arm64 in a Debug build, in the evidence campaign
of `2026/11/10/run-widget-engines-in-a-sandboxed-helper-process-on-macos`;
its `work/test.md` is the record of commands, readings and verdicts. A
disposable overlay drove `Platform::WidgetHelper::Launch(arguments)` with a
`SandboxRemoteHostEnd` and the real bundles, a run-owned keychain item and
a live loopback listener: the canonical `probe`, the `probe-unsandboxed`
control, and both again with `connect-control`.

- Locked: `fork` and `posix_spawn` gave `EPERM`; `socket` gave `EMFILE`,
  also after `close(0)`; raising the limit and connecting the preopened
  socket gave `EPERM`; `stat` and `access` of the working directory, the
  client's working directory and Application Support gave `EPERM` with
  policy `denied` (`opendir` `EMFILE`). The WindowServer, SecurityServer,
  securityd.xpc, cmio, audiohald, tccd, tccd.system and locationd lookups
  gave `kr1100`, with no WindowServer connection. The keychain item read
  gave `status-50` with no data, 0 camera and 0 audio devices were counted
  and the device policies were denied. `kill(0)` of the parent gave
  `EPERM`, `task_name_for_pid` `kr5`, the parent's info, path, proc and
  args readings `EPERM`, and the IOKit open was refused.
- Unsandboxed: each of these succeeded, including the item read (its value
  not kept), the lookups (except the unregistered
  `com.apple.locationd.registration`), 2 camera and 2 audio devices
  (counted in Telegram's own executable, with Telegram's entitlements; see
  the limits below) and the parent readings. Raising the descriptor limit
  did not apply there: its hard limit is already unlimited, so the probe
  reports it `n/a`. `task_for_pid` of the parent failed in both modes.
- Controls: the listener accepted the main process before and after and
  the unsandboxed helpers' tokens, none while a helper was locked. The main
  process read the item before and after the helpers, deleted it and
  checked it was gone. A parent-side `sandbox_check` read the locked
  helpers as locked and the unsandboxed ones never.
- Listing of `probe`: complete (end marker, declared counts, no cut row),
  with stable reference lookups and no defect. 4 descriptors: 0 and 1
  `/dev/null`, 2 the parent's stderr, 3 the `AF_UNIX` stream to the
  client. 19 Mach entries: task, thread, unprivileged host, bootstrap,
  semaphore, clock, voucher, 5 own receive and 3 own send-receive rights,
  logd, notifyd, and two send rights that the probe then reported
  `UNACCOUNTED`.
- Same-process attribution: a rerun added a disposable overlay hunk to the
  probe that, right after the post-lockdown listing, read libdispatch's
  firehose buffer header through `voucher_activity_get_metadata_buffer()`.
  In that canonical helper, whose listing was again complete with the same
  counts, the header held the listing's logd name followed by exactly the
  two `UNACCOUNTED` names, and the buffer's receive right was one of the
  own send-receive rows. The `probe-unsandboxed` helper's reading matched
  its own rows the same way. At the time, the probe itself still reported
  the two as unaccounted.
- Creation trace: a tracer injected into a re-signed clone of the final
  bundle ran `probe` with a valid opening, and its complete listing had the
  same composition. Both names were created by libdispatch's
  `firehose_client_reconnect`, every other row had a traced creation or was
  present before interposition and identical in an empty program, and the
  header reading in that process found the same two names. They are
  libtrace's firehose push ports to logd.

The probe's own reading was then executed in the same-binary helper on
macOS 27.0.1 arm64 in a Debug build, in the evidence campaign of
`2026/11/14/let-the-widget-helper-probe-attribute-libtraces-firehose-push-ports`;
its `work/test.md` is the record, and its `work/test-overlay.patch` keeps
the disposable scenario and fault hunks. The canonical `probe` and the
`probe-unsandboxed` control each ran in its own launch through the widget
fixture with `TDESKTOP_WIDGET_ENGINES`: one local widget card, placed on
screen in a Test-session chat, demanded the helper, whose argv held no
probe fixture argument. The negative legs ran in one launch through the
direct `Platform::WidgetHelper::Launch` driver of
[Minimal use](#minimal-use), after a no-fixture control leg, with
disposable overlay faults selected by a sentinel in the keychain-account
argv slot. Every helper's listing was complete (end marker, declared
counts, no cut row), its summary stayed under the 3900-byte bound, and it
ended `exit 1` without a signal.

- Canonical `probe`: `lockdown: ok`; the `firehose:` row accepted a pair
  after finding the listing's logd name once, and the space after the
  reading equaled the listed snapshot. The 19 entries had the earlier
  listing's composition, with two `libSystem firehose push port` rows in
  place of its two `UNACCOUNTED` send rights: plain send rights whose
  names equal the reading's. With no `UNACCOUNTED` row, the summary read
  `inventory: clean`, `firehose push ports 2` and `unaccounted: none`.
  The direct driver's no-fixture control leg, its fault hunks inert, read
  the same.
- `probe-unsandboxed`: `lockdown: skipped (unsandboxed control)`; its own
  reading accepted its own pair, which equals its two
  `libSystem firehose push port` rows, and no row was `UNACCOUNTED`. The
  verdict was exactly `inventory: NOT clean (unsandboxed control)`, with
  `unaccounted: none`.
- Extra send right: before the pre-lockdown snapshot, a fault looked up
  `com.apple.SystemConfiguration.configd`, neither a probe reference nor a
  service the lockdown forbids, and held the new plain send right through
  the inventory. After `lockdown: ok` the reading still accepted the
  genuine pair among 20 entries, and that right was the only `UNACCOUNTED`
  row, with no annotation: `inventory: NOT clean (1 unaccounted)`.
- Failed readings: one fault replaced the export's answer with a null
  pointer and zero length, one made the probe's logd reference search find
  none, and one added a second copy of the matching tuple to the reader's
  own copied scan input, leaving libdispatch's memory untouched. A fourth
  replaced the second name of a genuine unique reading with
  `bootstrap_port` before the checks, which the first name then passed.
  Each wrote its cause in its `firehose: FAILED` row and summary item:
  `no firehose header (metadata 0x0, 0 bytes)`,
  `no logd name: the com.apple.logd reference found no port`,
  `logd <name> found 2 times in the firehose header` and
  `<name> is the bootstrap port`. None had a
  `libSystem firehose push port` row. Each helper's only two `UNACCOUNTED`
  rows were exactly the pair that an unperturbed reading in the same
  helper found once, with the space after it equal to the listed
  snapshot; that reading ran after the probe's decision and was never
  assigned to its state. Each read `inventory: NOT clean (2 unaccounted)`,
  and the bootstrap right kept its `bootstrap (bootstrap_port)` class.

`TelegramWidgetHelper` was then probed on macOS 27.0.1 (26A434) arm64 in a
Debug build, in the evidence campaign of
`2026/11/14/run-macos-widget-engines-in-a-qt-free-helper-executable`; its
`work/test.md` (Runs 4-6) is the record, and the scenario of its
`work/test-overlay-app1.patch` keeps the disposable direct driver, the
keychain and listener fixtures and the process watcher. The direct driver
of [Minimal use](#minimal-use) ran `probe`, `probe-unsandboxed` and both
again with `connect-control`, with a run-owned keychain item and a live
loopback listener (Run 4); the pair ran again with an item whose access
list named the client and the helper (Run 5) and with an account that has
no item (Run 6). `TDESKTOP_WIDGET_ENGINES=probe` and `probe-unsandboxed`
each ran in its own launch through the widget fixture (Run 4). Every
helper was `<bundle>/Contents/Helpers/TelegramWidgetHelper`, a child of
the client with argv `[<path>, 3, <tail>]` and no environment variable;
its listing was complete (end marker, declared counts, no cut row), its
summary stayed under the 3900-byte bound, and it ended `exit 1` without a
signal.

- Canonical `probe`: the pre-lockdown snapshot held 11 entries; the
  warm-up lookup answered `kr=1102` and added the host port, a voucher and
  an own send-receive right; the 26 reference lookups left those 14 as
  they were, none `held-before=yes`. Then `lockdown: ok`, `state:
  sandboxed=1 nofile=0/0`, the four accounted descriptors and 19 Mach
  entries: task, thread, unprivileged host, semaphore, clock, voucher,
  bootstrap, 5 own receive and 3 own send-receive rights, logd, notifyd
  and the two `libSystem firehose push port` rows of an accepted reading
  (logd found once). Five of them, logd, notifyd, both push ports and an
  own send-receive right, were `changed between the reference snapshot and
  the listing`. The summary read `inventory: clean`, `firehose push ports
  2` and `unaccounted: none`, and a parent-side `sandbox_check` read the
  helper locked. The fixture route and a fault build's control leg read
  the same composition.
- Denials: every attempt code, locked and unsandboxed, equaled the
  same-binary macOS helper's above, except the keychain item's unsandboxed
  control ([Minimal use](#minimal-use)): `status-25293` for the client's
  present item, with the default access list (Run 4) and with the one
  naming the helper (Run 5), and `status-25300` for the account with no
  item (Run 6). The locked helper answered `status-50` all three times,
  its two keychain lookups `kr1100` against `ok` unsandboxed. The main
  process read the present item before and after the helpers.
- `probe-unsandboxed`: `lockdown: skipped (unsandboxed control)`,
  `sandboxed=0`, none of the 26 references `held-before=yes`, and 14 Mach
  entries: the canonical composition without logd, notifyd, the push
  ports and one own send-receive right. Its firehose reading failed with
  `no logd name` ([firehose reading](#firehose-reading)), no row was
  `UNACCOUNTED`, and the verdict was exactly `inventory: NOT clean
  (unsandboxed control)` with `unaccounted: none`. The parent never read
  it locked.
- `connect-control`: both helpers preopened fd 4. The locked one's connect
  gave `EPERM` and sent no token; the unsandboxed one connected and sent
  its token; neither was a clean inventory (`connect-control fixture
  injected`). Besides the main process's controls the listener received
  only the unsandboxed helpers' tokens.
- Extra send right: of the earlier faults only this one was rerun, in a
  fault build of the probe. It looked up
  `com.apple.SystemConfiguration.configd` before the pre-lockdown snapshot
  and held the right; after `lockdown: ok` the reading accepted the
  genuine pair among 20 entries, and that right was the only `UNACCOUNTED`
  row: `inventory: NOT clean (1 unaccounted)`.
- Frameworks: the same fault build counted dyld images: no framework at
  `begin` or after the listing (81 and 83 images), and after the attempts
  111 frameworks and 183 private frameworks, the six probe frameworks
  among them. The staged Debug helper imports only
  `/usr/lib/libc++.1.dylib` and `/usr/lib/libSystem.B.dylib` and has no
  undefined framework symbol. No attempt was `INCOMPLETE` for a
  framework; the fixture-less route left the keychain item incomplete for
  want of its fixture.
- Fixture route: each switch value started one helper with argv `[<path>,
  3, <mode>, <cwd>, <application-support>]`, the main log's `stream ended`
  line carried the summary of its stderr, and the card died with `the
  widget engine process ended` without loading.

Limits: all three macOS campaigns ran on one macOS version (27.0.1 arm64)
with ad hoc signatures, so the private libdispatch layout that the header
reading relies on is proved only there: as an overlay technique in the
first, and through the probe's own [firehose reading](#firehose-reading)
in the second and third. `TelegramWidgetHelper` was probed only natively
on arm64. On another layout the reading can fail and leave both rights
`UNACCOUNTED`; as that section says, nothing promises that every
incompatible layout fails rather than matching unrelated words. All three
also predate
`2026/11/14/sign-and-verify-the-macos-widget-helper-executable`: the
unsandboxed control's 2 camera and 2 audio devices were counted in
Telegram's own executable, with Telegram's entitlements, in the first
campaign, and in the third in `TelegramWidgetHelper` with its plain ad hoc
signature, without the hardened runtime or an entitlement (Run 4), and
with the hardened runtime and Telegram's Debug entitlements that Xcode's
`--deep` signing then gave the staged copy (Runs 5-6). The helper now has
the hardened runtime and the Enhanced Security entitlements of
`Telegram/Telegram/TelegramWidgetHelperDirect.entitlements`, in Debug as
in Release, and no probe campaign recorded here has run on it since it
gained the hardened runtime, so what its unsandboxed control counts is
unmeasured.

The [Mac App Store](#mac-app-store) service was probed on macOS 27.0.1
(26A434) arm64 in a Debug Store build, in the evidence campaign of
`2026/11/14/run-mac-app-store-widget-engines-in-an-xpc-service-helper`;
its `work/test.md` (Runs 13, 14 and 31) is the record, and its
`work/test-overlay.patch` keeps the disposable driver. The client was an
App-Sandboxed test-identifier copy of the Store product, made and signed
through `sign_mac.py` by a self-signed stand-in as **Mac App Store test
runs** in [Remote engines](test_widget_fixture.md#remote-engines)
describes. The driver of [Minimal use](#minimal-use) launched `probe`, then
`probe-unsandboxed`, through the Store `Launch()` with an absent run-only
keychain account, a live 127.0.0.1 listener with `connect-control` and the
2138 service names; the second launch waited out launchd's restart
deferral (10043-10044 ms). Both services were children of launchd whose
`parent:` row named Telegram, each refused the stream with its summary and
ended `ended`, reaped, and the listener received only the client's two
controls. The first run (Run 13) listed libsandbox's logd socket as `fd 5
... DEFECT`; after the repair (Run 31) the locked service held only
descriptors 0 to 4, `defects 0`, every reading of the expected list above
up to its "After the cut" part held (the `cmio`, `locationd` and
`locationd.desktop` lookups then read `kr1100`; that run predates the
cut), and a script compared both runs row by row: the same code for each of
the 67 other attempts and each of the 2138 service rows in both modes, and
the two modes agreed on every service row (`services: 2138 listed, 259
allowed, 218 allowed and registered`). `TDESKTOP_WIDGET_ENGINES=probe`
through the widget fixture (Run 14) started three services, each ending
in a `stream ended: the engine process refused the stream: widget helper
probe ... mode=probe lockdown=ok sandboxed=1 nofile=0/0 nproc=0/0 ...`
line with an `INCOMPLETE` service listing, and the next widget died
latched. Limits:
one macOS, arm64 only; a stand-in leaf without a Team ID (the copy needed
`disable-library-validation`), never the Mac App Store's own signature or
Apple's re-signing; no Screen Recording grant; the two unattributed Mach
entries were not traced; and the allowed and registered counts depend on
the host's registered services.

The cut that now ends the Store lockdown
([Remote engines](test_widget_fixture.md#remote-engines), **Lockdown**)
was probed on macOS 27.0.1 arm64 in a Debug Store build, in the evidence
campaign of
`2026/11/16/cut-new-mach-lookups-from-the-mac-app-store-widget-service-after-its-lockdown`;
its `work/test.md` (Part A Run 1, Part B Runs 11 and 12) is the record,
and its `work/test-overlay.patch` keeps the disposable driver and a Debug
client hunk that logs the objects of Telegram's own Mach rights. A copy
remade from that build as above ran the same probe leg, `probe` and then
`probe-unsandboxed` after launchd's deferral (10042 ms), with the 2138
names (Run 11); its rerun for the attribution (Run 12) read the same
`services:` rows and `launchd` attempts.

- Launchd: both modes read `launchd before the lockdown: bootstrap 0x807
  object=0x81ee79a1 special=same; registered 1 of 3 slots (objects
  0x4cab68f9); parent kr=0 (Success) object=0x64e5a2a2`. With `probe` the
  only lockdown change was `-0x807 S urefs=4 object=0x81ee79a1`, the
  listing read `bootstrap=0x0`, the summary `bootstrap 0, launchd 0`, and
  the attempts `launchd special-port: none`, `launchd registered: none`
  (0 of 3 slots valid), `launchd parent: kr32` and `launchd in-space: 0`;
  `probe-unsandboxed` read `bootstrap=0x807`, `bootstrap 1, launchd 1`,
  `HELD`, `1`, `ok` and `1`.
- Names: `probe` read `services: 2138 listed, 259 allowed; reachable
  through the special port 0, libxpc 0`, every row `kr32` and `xpc32`
  (1879 `denied`, 259 `allowed`); `probe-unsandboxed` read `... special
  port 218, libxpc 218`: 218 `allowed/ok/xpc-ok`, 1879
  `denied/kr1100/xpc159`, 37 `allowed/kr1102/xpc3` and 4
  `allowed/kr1102/xpc145`, its two paths agreeing on every name. The
  policy column was equal in both modes, and 218 equals Run 31's allowed
  and registered count above. launchd logged the locked service's 9 denied
  lookups before its lockdown and none after it, against 3770 denied and
  94 failed lookups of the control; the locked service logged 2147
  `failed to do a bootstrap look-up: xpc_error=[32: Broken pipe]` lines
  after its lockdown (the probe's 2138 and 9 from frameworks), the
  control none.
- WindowServer and the other readings that moved with the cut: `probe`
  read `windowserver connection: none CGSMainConnectionID() = 0`, every
  `lookup` attempt `kr32` (`(os/kern) exception protected`), the
  per-name rows of the window server, tccd, tccd.system, the pasteboard,
  SecurityServer, securityd.xpc and audiohald `allowed/kr32/xpc32`,
  `microphone devices: ok/0/0` and the keychain item `status-50`
  (`status100024` before the cut); `probe-unsandboxed` read `CONNECTED`,
  the `lookup` rows of tccd, tccd.system, SecurityServer, securityd.xpc,
  audiohald and the window server `ok`, those seven names
  `allowed/ok/xpc-ok` and `microphone devices: ok/2/1`. The locked
  service's log showed its TCC request ending
  `XPC_ERROR_CONNECTION_INVALID` towards `com.apple.tccd.system` and
  SkyLight's `This user is not allowed access to the window system right
  now.`; `screen-capture preflight` read `not-granted`, with no grant.
- Inventory: `lockdown: ok`, `fds 5` (0 to 3 and the `connect-control`
  fixture's 4), `mach 31` with `defects 0`, one `UNACCOUNTED` send right
  and one `IDENTITY QUERY FAILED` send-once, as in Run 31 above but for
  the bootstrap right (`mach 32` there). The locked probe ended 98 ms
  after its launch; launchd logged no exit reason, signal or
  `OS_REASON_GUARD` for either service, and no crash report appeared.
- Attribution: Run 11's client hunk listed the objects of Telegram's
  receive and send rights after the channel reply (`after-channel`) and
  100 ms after the keep request (`after-keep`). For `probe` the second
  reading came 32 ms after the service had ended and held one of that
  service's own receive objects as a new Telegram right: the kernel
  reuses a freed port, so objects compare only while both holders live.
  Run 12 added readings right after
  `xpc_connection_send_message_with_reply()` of the keep request returned
  (`after-keep-send`) and 10 ms later (`after-keep-10ms`), and read only
  those taken before the service ended. In both modes each of them held
  the `UNACCOUNTED` object as a Telegram receive right, the peer
  connection's port; `after-keep-send` and `after-keep-10ms`, but not
  `after-channel`, held the send-once's object as one too: the reply port
  of the keep request, which that call creates (for `probe` Telegram's
  receive count grew by one between `after-channel` and
  `after-keep-send`, in the same millisecond). logd's and the
  notification center's objects were among Telegram's send rights in
  every reading, and none of a service's 15 own receive objects among
  Telegram's receive rights while it lived.
- Build (Part A Run 1): the Debug service imported exactly the probe's
  three private names and the Release service none, and the probe's unit
  compiled at a 10.13 target, arm64 and x86_64, with
  `-Werror=unguarded-availability-new` and no availability diagnostic.
  The serve, lifecycle and widget-pack reruns on the locked service are
  in [Remote engines verification](test_widget_fixture.md#verification).

Limits: one macOS, arm64 only, so the cut on macOS 12 to 26 is
unmeasured; the stand-in signature, never Apple's; no Screen Recording
grant, so the capture claim rests on the WindowServer and tccd readings;
the attribution comes from a disposable client hunk, not from the probe,
and holds only for readings taken while the service lives; and what the
service can do through the rights it already holds, IOKit and system
calls was not measured.

The [Mac App Store](#mac-app-store) service's descriptor 2 was probed
under a file and under a terminal on macOS 27.0.1 (26A434) arm64 in a
Debug Store build, in the evidence campaign of
`2026/11/16/give-the-mac-app-store-widget-service-a-descriptor-2-that-is-not-telegrams-terminal`;
its `work/test.md` (Runs 3, 4 and 6) is the record, and its
`work/test-overlay.patch` keeps the disposable driver, a main-log line
for each completion's relay ending and the Debug knob of Run 6. A copy
remade from that build as above ran one leg per run, each launch through
the Store `Launch()`: three services whose only argument was a payload of
control bytes, which the Debug service refused as an unknown mode in its
last line before `_Exit(1)`, then `probe` with no fixture, so with no
service names. Telegram's stderr was `app_stderr.txt` (Run 3) or the
slave of a raw pseudo-terminal whose master a driver outside the App
Sandbox read continuously into a capture (Runs 4 and 6); `lsof` ran in
that driver, never in Telegram. The App-Sandboxed Telegram Lite read its
own `isatty(2)` as 0 under the file and 1 under the pseudo-terminal.

- File (Run 3): `fd 2 before the lockdown: parent's unreadable: EPERM(1)
  Operation not permitted, own vnode dev=<dev> ino=<inode>
  path=<leg>/run/app_stderr.txt own-terminal=no`, the inode `lsof` read
  at Telegram's fd 2; after the lockdown the fd-2 row read that vnode and
  path with `class=stderr, the stderr Telegram passed (...)`, and the
  summary over the channel `fds 4: 0 stdin; 1 stdout; 2 stderr; 3
  channel;`. The payload lines landed in the file raw.
- Terminal (Run 4): `lsof` read Telegram's fd 2 as the slave. The probe
  read `own socket ... own-terminal=no` before the lockdown and after it
  `fd 2 socket ... state=0x22 peer-pid=<Telegram> ... class=stderr relay,
  AF_UNIX stream to the parent (receive side shut; same object after
  lockdown)` (bit 0x20, `SOI_S_CANTRCVMORE`, set), and the summary `fds 4:
  0 stdin; 1 stdout; 2 stderr-relay; 3 channel;`. All 180 rows and the
  probe's last `Widget helper:` line reached the terminal, with no drop
  line. Each payload's refusal arrived once, escaped, the first as
  `Widget helper: unknown widget helper mode
  "1\x1b[31mRED\x1b[0m\x1b]0;TASK-TITLE\x07\x08\x0d\x7f\\\xc2\x9b"`, and
  no line of the capture held a byte outside 0x20 to 0x7e (no raw ESC,
  BEL, DEL or `C2 9B`).
- Neither summary of Runs 3 and 4 named fd 2 among its defects or
  failures: each read `lockdown=ok` and `stable=yes`, its only failure the
  `kr=17` identity query and its only unaccounted item the send right of
  [Mac App Store](#mac-app-store).
- Terminal passed (Run 6): the knob sent the service Telegram's own
  descriptor 2, as before that task, and the service's fd 2 was the slave
  by its `rdev`, by its inode (the `lsof` node of Telegram's fd 2) and by
  its path. The App-Sandboxed service read its own `isatty(2)` as 1: `fd
  2 before the lockdown: ... own vnode dev=<dev> ino=<inode>
  path=/dev/ttys<n> own-terminal=yes`, the fd-2 row `class=DEFECT: fd 2
  (vnode dev=<dev> ino=<inode>) is a terminal`, and over the channel `fds
  4: 0 stdin; 1 stdout; 2 DEFECT; 3 channel;` with `inventory: NOT clean
  (1 enumeration/identity failures, 1 defects, 1 unaccounted)` and the
  item `defect: fd 2 (vnode dev=<dev> ino=<inode>) is a terminal [...]`.
  The payload lines reached the terminal raw.

Limits: one macOS, arm64 only, the stand-in signature; `isatty` was read
only on a pseudo-terminal's slave, never on a terminal emulator's; and no
probe with service names ran under a terminal, so the relay's cut of
that listing was not observed. The relay's lifetime readings are in
[Remote engines verification](test_widget_fixture.md#verification).

On Windows the harness lines of [Windows](#windows) first ran in the
same-binary helper, the client's own `Telegram.exe -widgethelper
<handle>`, with the Win32 (x86 on WOW64) Debug build on Windows 11 build
26300, in the evidence campaign of
`2026/11/14/confine-the-windows-widget-helpers-network-with-lpac-and-turn-it-on`
(its `work/test.md`, Attempt 1 Runs 1-2). Run 1 started the real
executable's `-widgethelper` entry with the probe's argv tail from a
scratch driver that built the launch with the product's functions and
owned TCP and UDP listeners on 127.0.0.1 and `::1`; a sibling process
listened on the same addresses at another port, given as targets. Run 2
took the route of [Minimal use on Windows](#minimal-use-on-windows) from
a disposable overlay, with the client's listeners on the loopback
addresses, the host's own LAN address and a scoped link-local IPv6
address and one in the running WSL distribution as targets, and also
ran `probe` without a fixture through `TDESKTOP_WIDGET_ENGINES`.

- Arguments: `loopback port <port>, 8 network targets` (Run 1) and `10
  network targets` (Run 2), the scoped IPv6 targets included, with no
  `argument: malformed` line; without a fixture `loopback port absent, 0
  network targets`.
- Sockets and targets: `WS2_32 bound, WSAStartup(2.2) ok`; in every
  probe helper the four kinds read `ALLOWED` before the lowering and
  `denied (error 10013)` after it, and every target after it `socket
  denied (error 10013), <connect|sendto> not tried, socket creation was
  denied`. Before the lowering, under the startup token, TCP targets read
  `connect failed (no completion within 1000 ms)`, except `connect denied
  (error 10013)` towards WSL, and UDP targets `sendto SENT (48 bytes)`
  that no listener received
  ([What the Windows controls show](#what-the-windows-controls-show)).
- AFD endpoints: Winsock's own call, captured in ordinary x86 and x64
  processes, passed a 57-byte attribute whose first 48 bytes equal the
  probe's; the probe's bytes opened there with status 0, and a family of
  `0x7FFF` failed with `0xC000000D`. In every probe helper each kind read
  `ALLOWED (status 0x0)` before the lowering and `denied (status
  0xC0000022)` after it, and the `EA bytes` the helpers of the network
  legs printed were the ones opened in the ordinary processes.
- `connect-control`: every preopened socket read `ALLOWED` before the
  lowering; after it TCP read `connect failed (no completion within
  1000 ms)` towards the loopback and same-host targets and `connect
  denied (error 10013)` towards WSL, UDP `sendto SENT (48 bytes)`, each
  with `closesocket ok`. Nothing reached a listener; no denial of a
  preopened socket is claimed.
- Controls: in Run 1 the driver, in Run 2 the client and a new sibling
  process sent ordinary connections and datagrams to every listener
  before the launch, after Ready and after the helper ended; each
  listener received all of them and no `widget-helper-probe` marker.
  After Ready the probe helpers of Run 2 held no `\Device\Afd` handle.

`TelegramWidgetHelper.exe`, built with the QuickJS engine, was then
probed on the same host, Windows 11
build 26300 (10.0.26300.9711, 26H2) on x64 with user-mode CET available
to x64 processes, in the Win32 (x86 on WOW64) Debug build and in an x64
Debug build of the same commit, in the evidence campaign of
`2026/11/15/move-the-windows-widget-helper-into-a-qt-free-executable-and-run-it-on-x64`;
its `work/test.md` (Attempt 1, Runs 1, 2b and 4b) is the record, and its
`work/test-overlay.patch` keeps the disposable scenario. In the app each
probe helper was started through the product `Launch()`, once with
`TDESKTOP_WIDGET_ENGINES=probe` and once through
[Minimal use on Windows](#minimal-use-on-windows) with a loopback port,
per architecture; it wrote no `Widget helper:` line and served until it
was stopped. No reading is carried from one architecture to the other.

- Argv: `"<client dir>\modules\x86\TelegramWidgetHelper.exe" <handle>
  probe <cwd> <data>` (x86, Run 2b) and the same from `modules\x64` (x64,
  Run 4b), `<cwd>` the client's `cWorkingDir()` with its forward slashes
  and `<data>` its `GenericDataLocation`.
- Native probe, x86 (Run 2b): after the lowering `CreateProcessW` of
  `whoami.exe`, the helper's and the client's working directories,
  `%APPDATA%` and both registry keys read `denied (error 5)` and the
  socket `denied (error 10013)`, while the client, in the same run and
  while the helper served, opened the same paths and keys, ran the same
  `whoami.exe` and opened the helper's process. `thread token: none
  (ERROR_NO_TOKEN)`, the token and job checks passed, `win32k system
  calls: DISABLED (ProcessSystemCallDisablePolicy 0x5)`, and the rows read
  `DEP 0x3`, `ASLR 0xb`, `dynamic code 0x1`, `handles 0x3`, `win32k 0x5`,
  `extension points 0x1`, `signature 0x5`, `child process 0x1`, `shadow
  stack 0x100`, `font disable 0x1` and `image load 0x4`, as an outside
  reading of the same helper did; the thread, the event, `BCryptGenRandom`
  and the time zone rows passed, and `OpenProcess` of its own pid read
  `error 5`. That helper mapped `WS2_32.dll` and `mswsock.dll`, which no
  ordinary helper did.
- Native probe, x64 (Run 4b): the same denials, checks and rows, but for
  `ASLR 0xf` and `shadow stack 0x105` (`EnableUserShadowStack` set); its
  `DEP 0x3` row is a reading only, since the x64 build expects no DEP
  policy ([Remote engines](test_widget_fixture.md#remote-engines)).
- Harness lines, x86 and x64 (Runs 2b and 4b): in every probe helper the
  four socket kinds read `ALLOWED` before the lowering and `denied (error
  10013)` after it, and each AFD kind `ALLOWED (status 0x0)` then `denied
  (status 0xC0000022)`. The loopback legs read `arguments: loopback port
  <port>, 4 network targets, connect-control no` with no `argument:
  malformed` line, `WS2_32 bound, WSAStartup(2.2) ok`, and every target
  after the lowering `socket denied (error 10013), <connect|sendto> not
  tried, socket creation was denied`; before it, under the startup token,
  the TCP targets timed out after 1000 ms and the UDP datagrams read
  `SENT` and were not received. The client's TCP and UDP listeners on
  127.0.0.1 and `::1` received every control the client and three new
  sibling processes sent before the launch, after Ready and after the
  helper ended, 24 per leg, and no `widget-helper-probe` marker.
- A driver launch with the probe's tail on the committed images of both
  architectures (Run 1) reached Ready with the `handles 0x3`, `signature
  0x5`, `font disable 0x1` and `image load 0x4` rows that the suspended
  child had read back before its resume, and its thread, event and
  `BCryptGenRandom` rows passed.
- The helper's units preprocessed without `_DEBUG` (Run 1) held no probe:
  `main()` accepted only the token and gave `serve` only
  `WidgetHelper::Lockdown()`, and the Windows probe unit was empty. No
  Release helper ran.

Not run on Windows with either helper: a malformed argument, the 16-target
limit, a probe helper whose lockdown fails, a helper without Winsock or
without ntdll's two functions, a failed preopen, nonblocking switch or
`closesocket`, a harness connect that completed or failed other than by a
timeout or `WSAEACCES`, and ARM64. The same-binary helper ran only on x86.
With `TelegramWidgetHelper.exe` also not run: `connect-control`,
`probe-unsandboxed`, a target other than the loopback port's four, a
`win32k system calls:` line other than `DISABLED`, and an argument outside
the ANSI code page. The other Windows runs of the native probe are in
[Remote engines verification](test_widget_fixture.md#verification).

The [Windows Store](#windows-store) helper was probed on 2026-11-17 on
Windows 10.0.26300.9711 x64, the build of the x64 records above, in an
x64 Store Debug build (`uwp64`) installed as a stand-in test package
under `C:\Program Files\WindowsApps`, in the evidence campaign of
`2026/11/14/run-windows-store-widget-engines-in-the-restricted-helper`,
first in a build of the change before its commit (its
`evidence/c2/readings.md`), then in the committed change (its
`work/test.md`, Attempt 1); its `work/decisions.md` (W1) is the
confinement record. The probe, network and ACE legs started their
client through the package's alias under `test-run`.

- Probe leg (`TDESKTOP_WIDGET_ENGINES=probe`, a Test account): one helper
  from the package reached Ready with no hold, digest or start-image
  error, wrote its report, served three live cards and ended `exit 0` at
  the quit. Its rows equal Run 4b's x64 probe above but for the
  differences under [Windows Store](#windows-store), and in the same run
  the client opened the same paths and keys, ran `whoami.exe` and opened
  the helper's process. In the first build six checks of the leg's
  disposable overlay failed because its helper match expected the
  command line without the `\\?\` prefix (the helper served throughout);
  with a match that accepts it every overlay check passed.
- Network leg ([Minimal use on Windows](#minimal-use-on-windows) with a
  loopback port): `arguments: loopback port <port>, 4 network targets,
  connect-control no`; the four socket kinds and AFD kinds read `ALLOWED`
  before the lowering and `denied (error 10013)` and `denied (status
  0xC0000022)` after it, and every target after it `socket creation was
  denied`. The client's listeners on 127.0.0.1 and `::1` received the 24
  controls of the client and new sibling processes and no marker; the
  leg's 62 checks passed in both builds.
- Parent-package ACE: a probe leg whose root carried the ACEs of item 1
  under [Windows Store](#windows-store) read `client working directory
  <root>\TelegramForcePortable: FILE_LIST_DIRECTORY error 5` before the
  lowering and `... denied (error 5)` after it, every other row as in the
  probe leg.
- Non-Store controls: probe legs of the x86 and x64 non-Store Debug
  builds of the same tree, started unpackaged on the same host, read
  `after lowering: package identity: none (APPMODEL_ERROR_NO_PACKAGE)`
  and `CreateProcessW` of `whoami.exe` `denied (error 5)`, every other
  row equal to Run 2b's and Run 4b's above; against the Store probe the
  x64 one differed only in the `CreateProcessW` row. Outside readings of
  that x64 probe helper and of an ordinary x64 non-Store helper of the
  same tree gave the attribution of item 4.
- Activation control: `ActivateApplication` of the package's application
  with `test-run`'s arguments started a client with the alias route's
  package identity and package job; with an account-free root it quit at
  the harness's no-test-account gate, before any widget.

That campaign's default path, kill and restart, fail-closed packages,
attack and conformance parity and `build.bat` run are in
[Remote engines verification](test_widget_fixture.md#verification).

Not run: the x86 (`uwp`) and ARM64 (`uwparm`) Store builds (their
`build.bat` lines were read), a Release Store build, the Store's
submission, certification and signing, Windows builds other than
26300.9711, a package on a volume other than the system's, the
startup-task and protocol-activation starts, and, in the Store build,
`probe-unsandboxed`, `connect-control` and a target other than the
loopback port's four.

The tier row, the version floors of the queries and the Windows 7 rows
([Windows tiers](#windows-tiers)) were added by
`2026/11/21/choose-the-windows-widget-helpers-sandbox-tier-by-windows-version`
and compiled in the Win32 and x64 Debug helpers. They first ran in that
task's evidence campaign (its `work/test.md`, Attempt 1 Runs 1, 2 and 4,
Attempt 2 Runs 5 and 7; its `evidence/probe.md`) on Windows 11 build
26300 (10.0.26300.9711), on x86 and x64, launched by Telegram with that
task's disposable overlay (Runs 4 and 5) or by its tier driver, which
mirrors Telegram's launch (Runs 1, 2 and 7). Each forced reading is a
simulation that overrides only the version reading of the launching
process and of the helper.

- The tier row read `tier: AppContainer, version 6.2.9200` (and
  `10.0.14393`), `tier: LPAC 1703, version 10.0.15063`, `tier: Windows
  7, version 6.1.7601` and, unforced, `tier: full, version 10.0.26300`;
  in Telegram it also read its tier and forced version at 6.3.9600,
  10.0.10240 and 10.0.10586 (AppContainer), 10.0.17134 (LPAC 1703) and
  10.0.17763 (full).
- On each architecture Run 1 probed, unforced, the Debug helper built
  before the tiers from the commit that task started on and the first
  attempt's final clean Debug helper. With the tier row set aside, their
  53 rows, the clean rows of the Nova helpers
  (`2026/11/21/build-the-x64-windows-widget-helper-image-for-windows-7`
  and `2026/11/21/build-the-x86-windows-widget-helper-image-for-windows-7`,
  their `evidence/confinement.md`, E7/G2), were equal in order after the
  loopback port, the own pid, the client directory, the user name and
  the time zone bias were normalized (`53 vs 53 comparable lines, 0
  differing`); unnormalized, the `connect to 127.0.0.1:<port>` and
  `OpenProcess(own pid <pid>)` rows differed by their port and pid. The
  second attempt's clean helpers are other images, relinked from the
  same helper sources, and were not probed: its Run 9 carried this
  reading forward on a comparison of those sources.
- The child-process row read `not read (GetProcessMitigationPolicy(13)
  is documented from 10.0.16299)` at 10.0.16298 and `0x1` at 10.0.16299,
  and the shadow-stack row `not read (GetProcessMitigationPolicy(15) is
  documented from 10.0.19041)` at 10.0.19040 and its value at 10.0.19041
  (`0x100` on x86, `0x105` on x64).
- After the lowering every tier denied `CreateProcessW` of `whoami.exe`
  and `FILE_LIST_DIRECTORY` of the client's working directory and
  `%APPDATA%` (and of Telegram's `tdata`, a row that task's overlay
  adds), each `denied (error 5)` against a working control: the same
  calls succeeded around each helper in an ordinary process of the same
  architecture or, in Telegram, in the client.
- The lowbox tiers also denied every fresh socket (`denied (error
  10013)`) and AFD endpoint (`denied (status 0xC0000022)`) of both
  families, and every loopback target read `socket creation was denied`.
- On the Windows 7 tier the `before lowering:` directory controls read
  `ok`, and the `desktop:` row named the launch's own desktop. x86 read
  `mitigation DEP: 0x3, permanent 1 (GetProcessDEPPolicy)`, and x64
  `mitigation DEP: not read (GetProcessMitigationPolicy(0) is documented
  from 6.2.0)`.
- The Windows 7 network rows, recorded as read and never judged (**The
  Windows 7 tier** in
  [Remote engines](test_widget_fixture.md#remote-engines)), read
  `socket(AF_INET, SOCK_STREAM): ALLOWED, connect to 127.0.0.1:<port>:
  CONNECTED`, the four fresh sockets `ALLOWED` and the four AFD
  endpoints `ALLOWED (status 0x0)`. With a loopback port (Run 7) the TCP
  targets read `connect CONNECTED, marker sent (47 bytes)` and the UDP
  ones `sendto SENT (47 bytes)`, and the tier driver's listeners
  received the markers over TCP and UDP on 127.0.0.1 and `::1`.

Every forced reading, of any tier, is a simulation on Windows 11, and
what a real Windows 7, 8, 8.1 or Windows 10 before 1809 shows is field
coverage that no record here claims.

On Linux the probe first ran in the same-binary helper, the client's own
`/proc/self/exe -widgethelper 3`, in the x86_64 Debug client (the Docker
`centos_env` build) on a WSL2 6.6 kernel with Landlock ABI 3, in the
evidence campaign of
`2026/11/10/run-widget-engines-in-a-sandboxed-helper-process-on-linux`
(its `work/test.md`, Attempt 2, Runs 2-5): with user namespaces, without
them (`bwrap --disable-userns`) and inside Flatpak 1.14.6, against
`probe-unsandboxed` with namespaces. The lines matched the
readings under [Linux](#linux) for each shape: `landlock alone` read
`EACCES` for the working directory (`root=empty` with namespaces,
`root=kept` without them and inside Flatpak); `execve` and `socket` were
`killed by signal 31`, with no core dump; `after lockdown` read `ENOSYS`
and `descriptors=0,1,2,3`; and `layers:` named seccomp, Landlock ABI 3,
NNP, no capabilities, PDEATHSIG 9 and the namespaces, or
`sandbox=flatpak`. The control read `opened`, `returned ENOENT`,
`succeeded` and `seccomp=off`. The probed helpers went on serving widgets.

`TelegramWidgetHelper` was then probed on the same host, in the x86_64
Debug build of the same image, in the evidence campaign of
`2026/11/14/move-the-linux-widget-helper-into-a-qt-free-executable`; its
`work/test.md` (Attempt 1, Runs 1, 2 and 4) is the record, and its
`work/test-overlay.patch` keeps the disposable account-free scenario. In
every leg the probed helper went on serving widgets.

- Against the same-binary helper: the framework harness ran the helper's
  Debug build as its engine process, `unshare -Urnpm --fork --kill-child
  -- <helper> 3 <mode> / /`, and the Debug client built before the move as
  `... <Telegram> -widgethelper 3 <mode> / /` in the same session (Run 1).
  For each mode the six lines were byte-identical to the same-binary
  helper's and to that helper's lines taken earlier the same day:
  `landlock alone: cwd=EACCES client-cwd=EACCES data=EACCES root=empty
  lockdown=prctl(PR_SET_SECCOMP) failed: EPERM`, `execve: killed by
  signal 31`, `socket: killed by signal 31`, `lockdown: ok`, `after
  lockdown: cwd=ENOSYS client-cwd=ENOSYS data=ENOSYS descriptors=0,1,2,3`
  and `layers: seccomp=on landlock=abi3 nnp=on caps=none
  seccomp_filters=1 pdeathsig=9 pid=1 uid_map=0:1000:1 interfaces=lo
  root=empty sandbox=none`; the control read as described under
  [Linux](#linux). With descriptor 9 open in the process that exec'd the
  helper, its listing still read `descriptors=0,1,2,3`.
- Through Telegram's direct spawn (Run 2): `TDESKTOP_WIDGET_ENGINES=probe`
  and `probe-unsandboxed` each started one helper whose `/proc/<pid>/exe`
  was `out/Debug/TelegramHelpers/TelegramWidgetHelper` and whose
  `cmdline` was `<path>`, `3`, the mode, the client's working directory
  and its data directory; its six lines in `app_stderr.txt` were
  byte-equal to the same-binary helper's app lines of the first Linux
  campaign (`client-cwd=ENOENT data=ENOENT` under the empty root), and no
  core dump was written. The client's clone sets no parent-death signal,
  so `pdeathsig=9` there reads the helper's own `PR_SET_PDEATHSIG` in its
  `WidgetHelper::Adopt()`.
- Inside Flatpak 1.14.6, an `/app` install of a package-managed build
  (Run 4): the portal's `Spawn` got the argv
  `/app/libexec/TelegramDesktop/TelegramWidgetHelper 3 probe <cwd>
  <data>`, and the six lines were byte-equal to the first Linux
  campaign's Flatpak lines: `landlock alone: cwd=EACCES client-cwd=ENOENT
  data=ENOENT root=kept lockdown=prctl(PR_SET_SECCOMP) failed: EPERM`, the
  same `execve`, `socket`, `lockdown` and `after lockdown` lines, and
  `layers: seccomp=on landlock=abi3 nnp=on caps=none seccomp_filters=3
  pdeathsig=9 pid=2 uid_map=1000:0:1 interfaces=lo root=kept
  sandbox=flatpak`, the filter count one more than the client's. The
  kernel's audit lines for the two `SIGSYS` copies named the helper's
  path.
- Under strace, on the helper with the tightened filter, with glibc 2.39
  and through the loaders of glibc 2.28 and 2.41, the probe wrote the same
  lines, and after its lockdown the filter refused only `clone3` (not
  called on 2.28) and the probe's own three `openat` calls (the
  tightening's legs, re-tallied in Run 1).
- The Release helper defined neither `WidgetHelperProbeLockdown` nor
  `RunLinuxProbe`, and `3 probe / /` exited 1 with
  `Widget helper: unexpected arguments`, also with a connected socket on
  descriptor 3 (Run 1).

`TelegramWidgetHelper` was probed as a plain child inside the Snap
installed with strict confinement, in the evidence campaign of
`2026/11/16/prove-the-linux-widget-helper-in-the-strictly-confined-snap`
(its `work/test.md`, Attempt 1, Run 4), in an Ubuntu 24.04 VM with
kernel 6.8, snapd 2.78 and AppArmor enforcing. The client was a Release
Telegram with the test harness and the helper a Release helper, and
disposable overlays opened the probe's gates in both; the run of the
helper bytes the plain recipe ships had no probe
([Remote engines verification](test_widget_fixture.md#verification)).
Each client logged `Tgw: The widget helper starts without namespaces:
clone failed: Permission denied.`, and its helper ran under the app's
label, `snap.telegram-desktop.telegram-desktop (enforce)`, as
`$SNAP/usr/libexec/TelegramDesktop/TelegramWidgetHelper 3 <mode> <cwd>
<data>`, `<cwd>` the client's working directory and `<data>`
`$SNAP_USER_DATA/.local/share`, and went on serving widgets. With
`probe` its six lines in `app_stderr.txt` were `landlock alone:
cwd=EACCES client-cwd=EACCES data=EACCES root=kept
lockdown=prctl(PR_SET_SECCOMP) failed: EPERM`, `execve: killed by signal
31`, `socket: killed by signal 31`, `lockdown: ok`, `after lockdown:
cwd=ENOSYS client-cwd=ENOSYS data=ENOSYS descriptors=0,1,2,3` and
`layers: seccomp=on landlock=abi4 nnp=on caps=none seccomp_filters=3
pdeathsig=9 pid=<n> uid_map=none interfaces=lo,enp0s2 root=kept
sandbox=snap`; the kernel logged the two copies' `SIGSYS` kills
(`syscall=59` and `syscall=41`) and, from snapd's filter, a refused
`unshare` (`syscall=272`) for each of the three copies and the helper,
the empty root's first step, which kept the root. With
`probe-unsandboxed` they were `landlock alone: cwd=opened
client-cwd=opened data=opened root=kept lockdown=skipped`, `execve:
returned ENOENT`, `socket: succeeded`, `lockdown: skipped (unsandboxed
control)`, `after lockdown: cwd=opened client-cwd=opened data=opened
descriptors=0,1,2,3` and `layers: seccomp=on landlock=abi4,not-enforced
nnp=off caps=none seccomp_filters=2 pdeathsig=9 pid=<n> uid_map=none
interfaces=lo,enp0s2 root=kept sandbox=snap`, with no kill logged. In
both, `pid=` was the pid of the main log's `started` line and
`interfaces=` the client's, and `uid_map=none` came from AppArmor's
denial of the probe's `openat` of `/proc/<pid>/uid_map`, while the
helper's map, read as root, was the initial namespace's
`0 0 4294967295`. Read as root, the `probe` helper matched the helper
with the plain recipe's bytes in its filters over the client's two,
no-new-privileges, capabilities, the namespaces it shared with the
client, descriptors 0 to 3 and root listing.

Limits: x86_64 only, on one WSL2 6.6 host and, for the confined Snap,
one Ubuntu 24.04 VM with kernel 6.8. `TelegramWidgetHelper` was probed
as a plain child only inside that Snap, as a Release helper whose probe
gates an overlay opened; outside it only the same-binary helper's plain
child was probed (`bwrap --disable-userns`). aarch64 compiled only.
Every Linux helper probed above served engines alone; the Linux helper
renders since
`2026/11/14/run-widget-validators-and-renderers-in-the-linux-helper`,
whose records hold the probe of a helper that renders.

## Related guidance

- [test_widget_fixture: Remote engines](test_widget_fixture.md#remote-engines)
- [Account fixtures](../account-fixtures.md) for the secrecy rules the
  keychain fixture follows.
