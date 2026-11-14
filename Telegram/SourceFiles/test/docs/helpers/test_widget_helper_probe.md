# test_widget_helper_probe

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Internal dependency.
Source contract: [test_widget_helper_probe.h](../../test_widget_helper_probe.h).

## Use when

A run must show what the macOS widget helper can still do once it locked
itself down, and list every descriptor and Mach right it keeps. The probe is
the helper's lockdown hook in a Debug build: it takes the listing after the
real `Platform::WidgetHelper::Lockdown()`, tries what the sandbox must deny
and refuses the stream with a summary, so no widget is ever served.

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

- macOS only. On other platforms a probe mode refuses with `the widget helper
  probe is not available on this platform`.
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
- An environment-selected probe has no fixture. Its keychain item read and
  its loopback connect report `INCOMPLETE`, which is never a pass. Every
  widget of such a launch dies with `the widget engine process ended`. Each
  refusal counts as a helper failure, so the third probe latches the helper
  for the process.
- A pristine listing still holds two send rights no reference service
  matches. They are reported as `UNACCOUNTED` with the annotation
  `candidate: libtrace firehose push port to logd (unproven by this
  listing)`, and the summary never calls that inventory clean. Naming them
  takes a same-process creation trace (see [Verification](#verification)).
- `task_for_pid` fails in the unsandboxed control too (no debugger
  entitlement). The probe also tries it on itself as the API control.
  `task_name_for_pid` is the parent query that only the profile denies.
- `com.apple.locationd.registration` is not registered on macOS 27 (1102 in
  the control). `com.apple.locationd.desktop.registration` is, and the probe
  looks up both.
- Never AVFoundation discovery: it crashes a locked process. Devices are
  counted with CoreMediaIO and CoreAudio property sizes only, with no capture,
  no consent prompt and no keychain UI.

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
- One `fd` row per entry of `PROC_PIDLISTFDS`, and one `mach` row per
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
- `libSystem service com.apple.logd` and `com.apple.system.notification_center`.

A right to any other reference service, any other kernel object, a thread or
task port that is not the helper's own, or a failed identity query is a
defect or a failure. Any other send right is `UNACCOUNTED`.

Attempt codes are `ok`, an errno name, `kr<n>`, `status<n>`, `denied` or
`allowed` for `sandbox_check` readings, or `INCOMPLETE` with the missing
fixture. With the zero descriptor ceiling, `socket`, `opendir` and `open`
report `EMFILE`. `stat`, `access` and `file-read-data` show the profile's
own denial.

The refusal is `the engine process refused the stream: widget helper probe
...`, kept under 3900 bytes. It holds the pid, mode, lockdown and state,
`inventory: clean` or `inventory: NOT clean (<reasons>)`, the fd and per-class
Mach counts, every failure, defect and unaccounted entry, and every attempt as
`name=code`. It ends by naming the stderr rows and their last row. `inventory:
clean` needs a successful lockdown in `probe`, no failure, defect, unaccounted
entry, cut row or write error, and no injected fixture. `unaccounted: none`
appears only on a complete listing. Otherwise it reads `unaccounted: unknown
(listing incomplete)`. When items do not fit, the summary says `SUMMARY
CUT ... INCOMPLETE` and the stderr listing stays authoritative.

## Lifetime and cleanup

The probe keeps no state. It releases every array, looked-up right, task,
thread and host right, IOKit object and CF object it takes, and reaps a
successful `fork` or `posix_spawn` child within 2 s (`SIGKILL` and another
second otherwise). Near its end it closes stdin and tries a socket again.
The helper exits after the refusal. The overlay owns the keychain item, the
listener and the restoration of `SecKeychainSetUserInteractionAllowed`.

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
  logd, notifyd, and two send rights reported `UNACCOUNTED`.
- Same-process attribution: a rerun added a disposable overlay hunk to the
  probe that, right after the post-lockdown listing, read libdispatch's
  firehose buffer header through `voucher_activity_get_metadata_buffer()`.
  In that canonical helper, whose listing was again complete with the same
  counts, the header held the listing's logd name followed by exactly the
  two `UNACCOUNTED` names, and the buffer's receive right was one of the
  own send-receive rows. The `probe-unsandboxed` helper's reading matched
  its own rows the same way. The probe itself still reports the two as
  unaccounted.
- Creation trace: a tracer injected into a re-signed clone of the final
  bundle ran `probe` with a valid opening, and its complete listing had the
  same composition. Both names were created by libdispatch's
  `firehose_client_reconnect`, every other row had a traced creation or was
  present before interposition and identical in an empty program, and the
  header reading in that process found the same two names. They are
  libtrace's firehose push ports to logd.

Limits: one macOS version (27.0.1), ad hoc signatures, and the header
reading is an overlay technique that relies on a private libdispatch
layout; the probe has no such reading.

## Related guidance

- [test_widget_fixture: Remote engines](test_widget_fixture.md#remote-engines)
- [Account fixtures](../account-fixtures.md) for the secrecy rules the
  keychain fixture follows.
