# test_widget_helper_probe

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Internal dependency.
Source contract: [test_widget_helper_probe.h](../../test_widget_helper_probe.h).

## Use when

A run must show what the macOS widget helper can still do once it locked
itself down, and list every descriptor and Mach right it keeps. The probe is
the helper's lockdown hook in a Debug build: it takes the listing after the
real `Platform::WidgetHelper::Lockdown()`, tries what the sandbox must deny
and refuses the stream with a summary, so no widget is ever served. On
Windows the same hook runs a different, native probe and the helper goes on
serving widgets ([Windows](#windows)). On Linux it runs its own probe in
forked copies of the helper and around the real lockdown, writes its lines
to stderr, and the helper goes on serving widgets ([Linux](#linux)).

Callers: the `_DEBUG` block of `tgw/tgw_remote.cpp` is the only product caller.
`ChooseRemote()` accepts `probe` and `probe-unsandboxed` as values of
`TDESKTOP_WIDGET_ENGINES` (both select the remote engine),
`HelperArguments()` passes `WidgetHelperProbeArguments()` to
`Platform::WidgetHelper::Launch()`, and `HelperLockdown()` gives the
helper's `serve` call `WidgetHelperLockdown()`. Release builds, harness ones
included, return no arguments and call the real lockdown whatever argv says.
A disposable overlay supplies the positive-control fixtures itself (see
[Minimal use](#minimal-use)).

For the widget fixture's remote-engine switch, lifecycle and logs, read the
[Remote engines](test_widget_fixture.md#remote-engines) section instead.

## Preconditions and limits

- The listing, attempts, fixtures and summary on this page are macOS only.
  On Windows `probe` runs the native probe of [Windows](#windows) and
  `probe-unsandboxed` refuses with `the widget helper probe is not
  available on this platform`. On Linux `probe` and `probe-unsandboxed`
  run the probe of [Linux](#linux), and both serve widgets. The non-Linux
  all-other platforms start no helper; their hook would refuse every probe
  mode with the same text as Windows' `probe-unsandboxed`.
- `probe` locks down, then probes. `probe-unsandboxed` runs the same probe
  without the lockdown: it is the control, always in a separate helper.
- The real lockdown starts with its service check
  ([Remote engines](test_widget_fixture.md#remote-engines)). Its lookups
  run after the probe's reference snapshot and are released, so they show
  up only as a `changed between the reference snapshot and the listing`
  row if one leaves a right behind. A helper that already holds a
  forbidden connection reports `lockdown: FAILED the widget helper holds a
  connection to <service> before lockdown` and is then listed and probed
  without the profile. A packaged build (`DESKTOP_APP_USE_PACKAGED`) keeps
  its engines in process by default, because its external Qt lacks the
  startup patches; only an explicit Debug value starts its helper, and
  that check refuses it when its Qt connected to a listed service before
  `main`.
- The argv tail after `-widgethelper <token>` is `mode cwd
  application-support [keychain-account loopback-port [connect-control]]`.
  `cwd` is the client's `cWorkingDir()`. `application-support` is
  `QStandardPaths::GenericDataLocation`. A helper with nothing after the
  token runs the lockdown alone and serves. Any other mode refuses with its
  name. A malformed port or seventh argument is a defect in the report.
- On macOS an environment-selected probe has no fixture. Its keychain item
  read and its loopback connect report `INCOMPLETE`, which is never a pass.
  Every widget of such a launch dies with `the widget engine process
  ended`. Each refusal counts as a helper failure, so the third probe
  latches the helper for the process.
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

With `probe` the client appends the same argv tail, and the hook calls
`Platform::WidgetHelper::PrepareProbe()` first: the native probe in
`platform/win/widget_sandbox_win.cpp` takes its `before lowering:` controls
while the helper still impersonates its initial token. Then the hook calls
the real lockdown. A failure refuses the stream with the lockdown's own text
and writes no probe line. Otherwise the probe makes its `after lowering:`
attempts and readings, the hook writes every line of its report to stderr
as `Widget helper probe: <line>` (`app_stderr.txt` in a test run) and
returns "", and the helper serves widgets as in `helper` mode, so a probe
launch is no helper failure. The Windows probe reads none of the arguments
after the mode, takes no fixture and is no inventory of what the helper
holds. Its lines, their controls and the measured results, the remaining
network access included, are in
[Remote engines](test_widget_fixture.md#remote-engines) (**Windows probe**
and **What the Windows lowering leaves**), and its runs in that page's
[Verification](test_widget_fixture.md#verification).

## Linux

With `probe` or `probe-unsandboxed` the client appends the same argv tail
on both of its spawn paths, inside Flatpak through the portal's argv, and
the hook runs `RunLinuxProbe()` in `test_widget_helper_probe.cpp`. It
reads the mode, `cwd` and `application-support` (the data directory below)
and ignores every argument after them, so it takes no fixture. It calls
nothing in `platform/linux/` but the `lockdown` callable, which is the
real `Platform::WidgetHelper::Lockdown()`. It runs on serve's thread while
the helper is still single-threaded. Every line is one `write` to
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
mode, and the Release `HelperLockdown()` ignores argv.

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
  call, and it consumes nothing.
- After its lockdown the helper only reads, writes, peeks and allocates.
  Its opens are expected to fail with `ENOSYS`.

The expected readings below are expectations, not results
([Verification](#verification)). They assume a kernel with Landlock ABI
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
checks it is gone. The listener records each line it receives. Launch the
same legs with `probe-unsandboxed` as the control. With
`.connectControl = true`, the helper preopens one TCP socket before the
lockdown and connects it to the listener afterwards. That run lists the
injected descriptor, so it never counts as a clean inventory.

## Readings and refusals

Every row goes to the helper's stderr, which is the client's
(`app_stderr.txt` in a test run). Each row is one `write`, cut past 2048
bytes with a marker, prefixed `widget-helper-probe <pid> <mode>: `. The rows
come in this order:

- `begin`, `paths` and the fd 2 identities of the parent and the helper, read
  before the lockdown.
- `pre` rows of the first Mach snapshot, one `reference <service>` row per
  lookup with its object, and whether the second snapshot equals the first
  (names, rights, urefs, objects).
- `lockdown: ok|FAILED|skipped` and `state: sandboxed=<sandbox_check>
  nofile=<soft>/<hard>`.
- One `fd` row per entry of `PROC_PIDLISTFDS`. Then the `mach:` count, the
  `mach identities:` row, the `firehose:` row of the
  [firehose reading](#firehose-reading), and one `mach` row per
  `mach_port_space_info` entry, tree entries included, each with its
  `class=`.
- One `attempt <name>: <code> <detail>` row per attempt.
- `end: rows=<n> fds=<n> mach=<n> attempts=<n> cut-rows=<n> stderr=ok|FAILED`.

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
fixture. With the zero descriptor ceiling, `socket`, `opendir` and `open`
report `EMFILE`. `stat`, `access` and `file-read-data` show the profile's
own denial.

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
allocation. The traced runs under [Verification](#verification) found the
buffer initialized before `main`, by Qt's `+load` logging; that is an
observation, not a guarantee for every future helper. The scan relies on a
private layout and cannot prove it: a changed layout can leave no single
match, which only fails the reading, or match unrelated words, so a reading
on another macOS is trusted only once its names were joined with that
helper's own rows.

## Lifetime and cleanup

The probe keeps no state. It releases every array, looked-up right, task,
thread and host right, IOKit object and CF object it takes, and any
object-name right `mach_vm_region()` returns. It reaps a successful `fork`
or `posix_spawn` child within 2 s (`SIGKILL` and another second otherwise).
Near its end it closes stdin and tries a socket again.
The helper exits after the refusal. The overlay owns the keychain item, the
listener and the restoration of `SecKeychainSetUserInteractionAllowed`.

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
  `com.apple.locationd.registration`), 2 camera and 2 audio devices and the
  parent readings. Raising the descriptor limit did not apply there: its
  hard limit is already unlimited, so the probe reports it `n/a`.
  `task_for_pid` of the parent failed in both modes.
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

The probe's own reading was then executed on macOS 27.0.1 arm64 in a
Debug build, in the evidence campaign of
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

Limits: both campaigns ran on one macOS version (27.0.1 arm64) with ad hoc
signatures, so the private libdispatch layout that the header reading
relies on is proved only there: as an overlay technique in the first, and
through the probe's own [firehose reading](#firehose-reading) in the
second. On another layout the reading can fail and leave both rights
`UNACCOUNTED`; as that section says, nothing promises that every
incompatible layout fails rather than matching unrelated words.

On Linux the probe has not run in a Telegram helper yet, so the readings
under [Linux](#linux) are expectations.

## Related guidance

- [test_widget_fixture: Remote engines](test_widget_fixture.md#remote-engines)
- [Account fixtures](../account-fixtures.md) for the secrecy rules the
  keychain fixture follows.
