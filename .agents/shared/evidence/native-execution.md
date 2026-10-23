# Native execution gate

`native_exec.py` answers one question before a campaign relies on a standalone
evidence executable (an optimized-Qt probe, a replayer, a consumer binary):
does this exact file run natively on this macOS host as the architecture the
host runs? It launches the file once, in its report entry, with a fresh nonce,
and reads what the running process says about itself. It needs no Telegram
build, launch or account.

A successful link is not that evidence. The QR gift campaign
(`2026/10/15/choose-a-unique-gift-for-the-qr-box`) linked an `x86_64`-only
component on an `arm64` host without Rosetta, and the component never
launched (`Errno86 Bad CPU type`).

## What a PASS proves

For one gate invocation, `verdict: "PASS"` proves that:

- the host is macOS, and the gate process itself runs natively
  (`sysctl.proc_translated` is not 1) as the declared architecture
  (`platform.machine()` equals `--host-arch`);
- `executable.path` (resolved with `realpath`) was a regular file whose
  SHA-256 the gate read just before launching it;
- the kernel launched that file as `<path> --native-report <nonce>` (no
  shell, stdin from `/dev/null`) and it exited 0 within the timeout;
- its stdout held exactly one schema-1 `NATIVE_REPORT` line echoing the nonce
  generated for this launch (`secrets.token_hex(16)`), so the report came
  from this launch and is not canned output;
- the report says `translated: false`, and both `buildArch` (the compiled
  slice's architecture) and `runningArch` (`uname()` of the running process)
  equal the declared host architecture.

## What it does not prove

- That the campaign mode of the same file works, terminates or produces
  correct results: the gate runs only the report entry.
- Anything about other hosts, other architectures, other build
  configurations of the component or other platforms. Non-macOS hosts are
  refused, not emulated; nothing here says how a component builds, links or
  behaves there.
- That a later run used the gated file: `check_verdict` decides that when the
  verdict is used.
- Authenticity of the report: it is the component's own reading. A component
  that ignores the protocol cannot pass; one that deliberately prints a
  matching report with the nonce is not detected.
- A file header (`file`, `lipo -archs`, `otool`) or a link log never passes,
  because the gate reads neither. The self-tests refuse a linked file for the
  other architecture and an object file whose header names the host, both by
  executing them.

## Component protocol

Include `.agents/shared/evidence/native_report.h` (header-only; standard
library and POSIX, no Qt) and make this the first statement of `main()`,
before any application object, environment change, file or output:

```cpp
if (NativeReport::Requested(argc, argv)) {
	return NativeReport::Print(argv[2]);
}
```

- `Requested` is true for exactly `<exe> --native-report <nonce>`
  (`NativeReport::kFlag`).
- `Print` writes one line,
  `NATIVE_REPORT {"schema":1,"nonce":"...","buildArch":"...","runningArch":"...","translated":false}`,
  flushes stdout and returns 0. It does not validate the nonce; the gate
  always sends 32 hex characters.
- `buildArch` is `arm64` or `x86_64` from the compiler's target macros
  (`unknown` otherwise), `runningArch` is the `uname()` machine (empty on
  failure), `translated` is `sysctl.proc_translated == 1` (a missing key
  counts as native).
- The header stops compilation with `#error` outside macOS. Put the evidence
  directory on the include path (`-I .agents/shared/evidence`, or
  `target_include_directories` in the component's CMake).
- Nothing may run before the report: an application object, plugin loading
  or file work could fail or block for reasons unrelated to native execution,
  and would perform campaign side effects during the gate.

## Command

From the checkout root, with a native interpreter (`/usr/bin/python3` on
macOS):

```bash
python3 .agents/shared/evidence/native_exec.py \
  --executable BUILD_DIR/COMPONENT \
  --host-arch HOST_ARCH \
  --out RUN_DIR/native-gate.json
```

`HOST_ARCH` is `arm64` or `x86_64`: the architecture the host runs natively,
as `uname -m` prints it in a native shell. `--timeout-seconds S` (default 30)
bounds the launch. The verdict JSON is printed and, with `--out`, also
written to that file, which must not name the executable. Exit 0 is PASS, 1
is a refusal, 2 is a usage error or an unwritable `--out`.

## Verdict

- `schema` (1), `tool` (`native_exec`), `verdict` (`PASS` or `REFUSED`),
  `reason` (null on PASS, else a refusal code below), `message` (what
  happened and what to do).
- `startedAt`, `finishedAt`: epoch milliseconds around the gate call.
- `host`: the gate process's `platform`, `machine` and `translated` (null
  when unreadable), plus `declaredArch`.
- `executable`: `path` (realpath) and, once the file was found, `size`,
  `mtimeNs` and `sha256`.
- `execution`, present only when a launch was attempted: `argv`, `nonce`,
  `exitCode`, `errno` (launch errors only), `durationMs`, `stdout` and
  `stderr` (first 4096 characters each), and `report` (the parsed report,
  kept on `report-nonce-mismatch` too).
- `proves` (the PASS statement) and `limits`, in every verdict.

## Refusals

Checked in this order; the first failure decides.

| Code | Meaning | Action |
|---|---|---|
| `unsupported-host-platform` | The gate does not run on macOS. | Run the component and the gate on a macOS host. |
| `host-mismatch` | `--host-arch` is not the gate process's `machine`, or the gate process runs translated, or its translation state cannot be read. | Declare what `uname -m` prints in a native shell; run the gate with a native `python3` such as `/usr/bin/python3`. |
| `missing-executable` | The path is not an existing, readable regular file. | Build the component and pass the exact path of the linked executable. |
| `unsupported-architecture` | The kernel refused the launch with `EBADARCH` (errno 86): the file has no slice this host runs natively, such as an `x86_64`-only file on Apple silicon without Rosetta. | Rebuild with `-DCMAKE_OSX_ARCHITECTURES=HOST_ARCH` (or `-arch HOST_ARCH`) and gate the rebuilt file. |
| `launch-failed` | Any other launch failure, such as `ENOEXEC` (errno 8: an object file, a library, a damaged image) or a permission error, or no exit within the timeout. | Gate the linked executable itself; after a timeout, make the report entry the first statement of `main()`. |
| `nonzero-exit` | The report entry exited nonzero or was killed by a signal (named in the message). | Read `execution.stderr`; make the report entry the first statement of `main()` and return `Print`'s result. |
| `no-architecture-report` | stdout does not hold exactly one schema-1 `NATIVE_REPORT` line with exactly the five keys and their types. | Add the protocol statement and print the report only through `NativeReport::Print`. |
| `report-nonce-mismatch` | The report echoes another nonce: canned or replayed output. | Pass the received `argv[2]` to `Print`; never print a stored report. |
| `translated-execution` | The report says `translated: true` (Rosetta). | Rebuild for the host architecture and gate again. |
| `architecture-mismatch` | The report's `buildArch` or `runningArch` differs from the declared host architecture. | Rebuild for the host architecture and print the report only through `Print`. |

## Using it in a campaign

1. Build the component for the host: `-DCMAKE_OSX_ARCHITECTURES=HOST_ARCH`
   in CMake or `-arch HOST_ARCH` on a compiler line.
2. Gate the exact file with `--out` before any campaign run uses it, and keep
   the verdict with the run's command line, working directory, environment
   additions, exit code and log.
3. Run the campaign with the same, unchanged file. A rebuild needs a new gate.
4. An assessor that consumes the component's results calls
   `native_exec.check_verdict(verdict, executable)` with the path those
   results name; an empty list means the verdict is usable for that file.
   `paint_replay.py` does this for its replay component, and also refuses a
   gate that finished after the replay started or declared another host
   architecture.

`check_verdict` returns `{code, message}` problems:

| Code | Meaning |
|---|---|
| `gate-not-passed` | Not a `native_exec` schema-1 verdict, a refused verdict, or a PASS that its own record contradicts (nonzero exit, translated host or report, architectures other than the declared one). |
| `gate-no-execution` | No execution record with a report: the verdict did not come from a launch. |
| `gate-nonce` | The report's nonce is not the nonce the gate sent. |
| `gate-executable-mismatch` | The gated path and the used path differ after `realpath`. Both are resolved because macOS temp directories sit behind the `/var` to `/private/var` link and `QCoreApplication::applicationFilePath()` need not be canonical. |
| `gate-stale-binary` | The file's current SHA-256 differs from the gated one, or the file cannot be read: it was rebuilt or replaced after gating. |

`check_verdict` checks a verdict's consistency with the file at hand, not
who wrote the verdict.

## Self-tests

```bash
python3 .agents/shared/evidence/native_exec_test.py -v
```

- `ReportTests`, `ControlledGateTests` and `VerdictTests` are pure. Injected
  host facts and launchers supply the cases this host cannot produce: a
  non-macOS host, a translated or mismatched gate process, a translated
  report, errno 86 and errno 8 launch errors, a timeout and a signal exit.
  `launch()` itself is also run for real on a missing path (errno 2) and on a
  `/bin/sleep` that outlives a 0.2 s timeout.
- `CompiledGateTests` (macOS) compiles fixtures against `native_report.h`
  with `xcrun --find clang++` (or `NATIVE_EXEC_TEST_CXX`) and the SDK from
  `SDKROOT` or `xcrun --show-sdk-path`, then launches them through the gate.
  A missing compiler fails the class; `NATIVE_EXEC_TEST_SKIP_COMPILE=1` skips
  only this class.

On an `arm64` host without Rosetta these refusals come from real execution:
the positive fixture passes (twice, with distinct nonces); a report entry
returning 3 is `nonzero-exit`; a well-formed report naming the other
architecture is `architecture-mismatch`; a canned nonce printed through the
header is `report-nonce-mismatch`; a component printing `hello` is
`no-architecture-report`; the positive source linked for the other
architecture (premise: `lipo -archs` names only that architecture) is
`unsupported-architecture` with errno 86; the positive source compiled as an
object file with execute permission (premise: `lipo -archs` names the host)
is `launch-failed` with errno 8; a path never built is `missing-executable`;
and the CLI exits 0, 1 and 2 for PASS, refusal and usage errors. When Rosetta
is installed (probed with `arch -x86_64 /usr/bin/true`), the other-architecture
fixture must be refused `translated-execution` with a report saying
`translated: true` instead. `unsupported-host-platform`, `host-mismatch` and,
without Rosetta, `translated-execution` stay controlled cases.

## Limits

- macOS (Mach-O) hosts only: the header stops compilation elsewhere and the
  gate refuses other host platforms.
- `arm64e` reports as `arm64`, in `uname()` and in the compiler macros; the
  gate does not tell them apart.
- The report is self-reported: a component that ignores the protocol cannot
  pass, and a deliberately forged matching report is not detected.
- One launch of one file on one host: another build configuration or host
  needs its own gate.
- The file identity is read just before the launch, so a swap during the
  launch itself is not detected; `check_verdict` re-reads the file when the
  verdict is used.
