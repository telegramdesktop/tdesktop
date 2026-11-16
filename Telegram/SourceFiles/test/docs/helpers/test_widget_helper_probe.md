# test_widget_helper_probe

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Internal dependency.
Source contract: [test_widget_helper_probe.h](../../test_widget_helper_probe.h)
(the client half in Telegram) and
[test_widget_helper_probe_lockdown.h](../../test_widget_helper_probe_lockdown.h)
(the helper half on macOS and Linux and the names both halves share).

## Use when

A run must show what the macOS widget helper can still do once it locked
itself down, and list every descriptor and Mach right it keeps. The probe is
the lockdown hook of `TelegramWidgetHelper`'s Debug build, the Qt-free
executable at `Telegram.app/Contents/Helpers/`: it takes the listing after
the real `WidgetHelper::Lockdown()` (`_other/widget_helper_mac.cpp`), tries
what the sandbox must deny and refuses the stream with a summary, so no
widget is ever served. On Linux the helper is `TelegramWidgetHelper` too,
in `TelegramHelpers/` next to the client's binary or in a package-managed
build's `libexec/telegram-desktop/`, and the probe is again the lockdown
hook of its Debug build
(`test/test_widget_helper_probe_lockdown_linux.cpp`): it runs its own
probe in forked copies of the helper and around the real
`WidgetHelper::Lockdown()` (`_other/widget_helper_linux.cpp`), writes its
lines to stderr, and the helper goes on serving widgets ([Linux](#linux)).
On Windows the helper is the client's own executable, started with
`-widgethelper`, and the probe is that same-binary helper's lockdown hook
in a Debug build, which runs a different, native probe and the harness's
own network attempts; the helper then goes on serving widgets
([Windows](#windows)).

Callers: the probe has two halves. In Telegram the `_DEBUG` block of
`tgw/tgw_remote.cpp` is the only product caller of the client half.
`ChoosePlace()` accepts `probe` and `probe-unsandboxed` as values of
`TDESKTOP_WIDGET_ENGINES` (both select the remote engine), and
`HelperArguments()` passes `WidgetHelperProbeArguments()` to
`Platform::WidgetHelper::Launch()`. On macOS and Linux the Debug `main()`
of `TelegramWidgetHelper` (`_other/widget_helper.cpp`) gives its `serve`
call `Test::WidgetHelperProbeLockdown()` around the real lockdown; each
platform's helper build compiles its own definition of it. On Windows
`HelperLockdown()` in `tgw_remote.cpp` gives the same-binary helper's
`serve` call `WidgetHelperLockdown()`. Release builds of Telegram, harness
ones included, return no arguments. A Release `TelegramWidgetHelper`
contains no probe (the target never defines
`TDESKTOP_RELEASE_TEST_HARNESS`), accepts only its token, refuses any other
argument with `Widget helper: unexpected arguments` before it adopts the
channel and always gives `serve` the real lockdown; on Windows the Release
hook ignores argv and calls the real lockdown. A disposable overlay
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
- On macOS and Linux the argv after the helper's path is the token `3`,
  then the tail `mode cwd application-support [keychain-account
  loopback-port [connect-control] [target...]]`; on Windows the same tail
  follows `-widgethelper <token>`. The targets come only from a fixture's
  `networkTargets` (Windows). `cwd` is the client's `cWorkingDir()`; the
  helper's own working directory is `/`, which the macOS helper moves to
  itself before it adopts the channel and the Linux client gives its helper
  on both spawn paths. `application-support` is
  `QStandardPaths::GenericDataLocation`. A Debug helper with nothing after
  the token runs the lockdown alone and serves. Any other mode refuses with
  `unknown widget helper mode "<mode>"`. On macOS a malformed port or
  seventh argument, a target included, is a defect in the report; on
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

## Windows

With `probe` the client appends the same argv tail to the same-binary
`-widgethelper` helper, and its hook, `WidgetHelperLockdown()` in Telegram,
runs `LockDownWithProbe()` in `test_widget_helper_probe.cpp`, which goes in
this order, once per helper:

1. `Platform::WidgetHelper::PrepareProbe()` with the client's working
   directory argument (native separators; empty or missing names no
   target): the product's native probe in
   `platform/win/widget_sandbox_win.cpp` binds the delay-loaded `WS2_32.dll`
   imports and takes its `before lowering:` controls while the helper still
   impersonates its startup token.
2. The harness's own preparation, under the same token: its own
   `WSAStartup`, then its `before lowering:` socket, target and AFD endpoint
   controls and, with `connect-control`, the preopened sockets; without
   Winsock only the AFD endpoint controls
   ([Windows harness lines](#windows-harness-lines)).
3. The real lockdown. A failure refuses the stream with the lockdown's own
   text, writes no probe line and closes what the harness holds.
4. The harness's `after lowering:` attempts, then the product probe's.
5. Every line of the product probe's report, then every harness line, to
   stderr as `Widget helper probe: <line>` (`app_stderr.txt` in a test
   run); then the harness closes every socket it still holds and ends its
   own `WSAStartup`, and the hook returns "".

The helper then serves widgets as in `helper` mode, so a probe launch is no
helper failure. After the lockdown the hook calls only what was bound
before it: the `WS2_32.dll` imports, which the product probe and then the
harness's own Winsock start each bind (the harness uses Winsock only when
its own bind and `WSAStartup` succeeded), and `NtCreateFile` and
`RtlInitUnicodeString`, resolved from the already mapped ntdll. Neither
probe is an inventory of what the helper holds. The product probe's lines,
their controls and the measured results are in
[Remote engines](test_widget_fixture.md#remote-engines) (**Windows probe**
and **What the Windows lowering leaves**), and its runs in that page's
[Verification](test_widget_fixture.md#verification).

### Windows arguments

The hook reads the client's working directory (index 2 of the arguments,
the token at 0), the loopback port (index 5) and everything from index 6;
the application-support and keychain-account arguments are ignored.

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

After the product probe's lines, in this order:

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

The `before lowering:` lines run under the helper's startup token, a
regular AppContainer token of the helper's own package
([Remote engines](test_widget_fixture.md#remote-engines)), not under an
unconfined one: they are the in-helper control for the same calls, and an
AppContainer token may be kept from other processes' loopback listeners
before the lowering as well. Whether a target was reachable is therefore
decided by the fixture's own connections and datagrams to the same
listeners, from an ordinary process, before and after the helper; a
helper attempt that the fixture did not receive is no denial.
With `connect-control` the helper holds the preopened sockets across the
lowering and the run is an instrumented one, not the ordinary helper.

## Linux

With `probe` or `probe-unsandboxed` the client appends the same argv tail
on both of its spawn paths, so the helper runs as `<helper path> 3 <mode>
<cwd> <data>`. A direct clone execs `TelegramHelpers/TelegramWidgetHelper`
next to the client's binary, or the package's
`libexec/telegram-desktop/TelegramWidgetHelper` in a package-managed
build; inside Flatpak the portal's argv starts
`/app/libexec/telegram-desktop/TelegramWidgetHelper`
([Remote engines](test_widget_fixture.md#remote-engines) says how the
client finds that path). The hook, `Test::WidgetHelperProbeLockdown()` in
`test/test_widget_helper_probe_lockdown_linux.cpp`, compiled only into the
helper's Debug build, runs `RunLinuxProbe()` there. It takes the mode,
`cwd` and `application-support` (the data directory below) as the argv
bytes the client passed and ignores every argument after them, so it takes
no fixture. It calls nothing of the helper's Linux sources but the
`lockdown` callable, which is the real `WidgetHelper::Lockdown()`, that
is `Platform::WidgetHelper::LockDownHelper()` in
`widget_lockdown_linux.cpp`. It runs on serve's thread while the helper is
still single-threaded. The Release `TelegramWidgetHelper` has no probe: it
refuses any argument after its token with
`Widget helper: unexpected arguments`. Every line is one `write` to
descriptor 2, prefixed `Widget helper probe: `. That is the client's
stderr on both spawn paths, since the portal is handed the client's
descriptor 2 too, so the lines land in `app_stderr.txt` in a test run.
Unless the helper's own lockdown refuses, the hook returns "" and the
helper serves widgets as in `helper` mode, so a probe launch is no helper
failure.

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
mode, and only a Debug helper runs it: the Release helper refuses it.

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
same-binary helper it replaced; the plain child's only for the
same-binary helper ([Verification](#verification)). The others are
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
    client's>` and `root=kept`.
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

Every row goes to the helper's stderr, which is the client's
(`app_stderr.txt` in a test run). Each row is one `write`, cut past 2048
bytes with a marker, prefixed `widget-helper-probe <pid> <mode>: `. The rows
come in this order:

- `begin`, `paths` and the fd 2 identities of the parent and the helper, read
  before the lockdown.
- `pre` rows of the first Mach snapshot; `warm-up lookup <name>: lookup
  kr=1102 (Unknown service name)` and one `first bootstrap lookup added:
  +<entry>` row per entry the warm-up added (or `first bootstrap lookup
  changed nothing: ...`); one `reference <service>` row per lookup with
  its object and `held-before=`; and whether the snapshot after the
  references equals the warmed one (names, rights, urefs, objects).
- `lockdown: ok|FAILED|skipped` and `state: sandboxed=<sandbox_check>
  nofile=<soft>/<hard>`.
- One `fd` row per entry of `PROC_PIDLISTFDS`. Then the `mach:` count, one
  `changed between the reference snapshot and the listing: <entry>` row per
  difference, the `mach identities:` row, the `firehose:` row of the
  [firehose reading](#firehose-reading), and one `mach` row per
  `mach_port_space_info` entry, tree entries included, each with its
  `class=`.
- One `attempt <name>: <code> <detail>` row per attempt.
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
type, protocol and, for `AF_UNIX`, the peer pid. Accounted descriptors are 0
`/dev/null` read, 1 `/dev/null` write, 2 the same file as the parent's fd 2,
and 3 an `AF_UNIX` stream whose peer is the parent. Anything else is a defect.

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
  reading of the same helper accepted.

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
CUT ... INCOMPLETE` and the stderr listing stays authoritative.

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
The helper exits after the refusal. The overlay owns the keychain item, the
listener and the restoration of `SecKeychainSetUserInteractionAllowed`.

On Windows the hook closes each fresh socket and AFD handle right after its
attempt and each preopened socket right after its `after lowering:` use
(one that could not be made nonblocking at once, before the lowering);
before it returns, and on a lockdown failure, it closes whatever it still
holds and ends its own `WSAStartup`, so no harness socket survives into
Ready. The product probe closes its listener and ends its own start in its
report step. The overlay owns its listeners and closes them after the
helper ended.

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
before `main`. The two Linux campaigns are the last ones below: the first
probed the same-binary Linux helper, the client's own
`/proc/self/exe -widgethelper 3`, the second `TelegramWidgetHelper`, which
replaced it.

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
the hardened runtime and no entitlement, and no probe has run on it since,
so what its unsandboxed control counts is unmeasured.

On Windows the harness lines of [Windows](#windows) ran with the Win32
(x86 on WOW64) Debug build on Windows 11 build 26300, in the evidence
campaign of
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

Not run on Windows: a malformed argument, the 16-target limit, a probe
helper whose lockdown fails, a helper without Winsock or without ntdll's
two functions, a failed preopen, nonblocking switch or `closesocket`, a
harness connect that completed or failed other than by a timeout or
`WSAEACCES`, and architectures other than x86. The Windows runs of the
product probe are in
[Remote engines verification](test_widget_fixture.md#verification).

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
  `/app/libexec/telegram-desktop/TelegramWidgetHelper 3 probe <cwd>
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

Limits: one x86_64 host and kernel. The plain child (no user namespaces)
was not probed again with `TelegramWidgetHelper`: its clone and fallback
are unchanged and exec the same file. aarch64 compiled only.

## Related guidance

- [test_widget_fixture: Remote engines](test_widget_fixture.md#remote-engines)
- [Account fixtures](../account-fixtures.md) for the secrecy rules the
  keychain fixture follows.
