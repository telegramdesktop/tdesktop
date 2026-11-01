# Autonomous Windows Build-Lock Recovery

This contract applies to Debug builds run by `continue` / `perform-task`.
Those workflows own the selected checkout's build tree for the duration of
the task, so a transient lock is recoverable and is not a task `Block` or a
global hard stop on first sight.

## Safety boundary

Resolve `SOURCE_ROOT`, its exact `BUILD_ROOT`, and the exact checkout `EXE`
before acting.

- Process control may target the exact `EXE`, or a verified compiler/linker
  process tied to `BUILD_ROOT` or directly holding one named artifact inside
  it.
- File deletion may target only exact named artifacts inside `BUILD_ROOT`.
- Never kill by image name, stop an installed Telegram client, touch another
  checkout, delete a directory, or terminate an IDE/debugger or an unknown
  process.
- In this autonomous workflow, an exact-path `EXE` process is a disposable
  task/test straggler even if a previous phase launched it. It may be stopped.

## Before every build

Serialize builds that share `BUILD_ROOT`. Stop only an exact-path app
straggler:

```bash
python SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
  test-cleanup --exe EXE
```

Do not pass `--delete-exe` during this proactive step.

## Recover a lock

Treat `C1041`, `LNK1104`, `cannot open output file`, `access denied`, `file in
use`, and equivalent failures on an output under `BUILD_ROOT` as one recovery
round:

1. Preserve the complete build output in the task's `.local/build-logs/`.
2. Extract every exact locked output path from the error. For an
   `LNK1104` naming the task executable, use `EXE`. Do not guess a path that
   the log did not identify.
3. Run:

   ```bash
   python SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
     build-lock-recover \
     --source-root SOURCE_ROOT \
     --build-root BUILD_ROOT \
     --exe EXE \
     --artifact LOCKED_PATH \
     --wait 10
   ```

   Repeat `--artifact` for multiple named outputs. The helper waits for
   transient scanners, stops the exact checkout executable, uses Windows
   Restart Manager to identify direct holders, terminates only verified build
   processes, and deletes only the named artifacts so the next build
   regenerates them. An artifact that disappears during the wait is already
   recovered and is reported as `already_absent`.
4. When the helper reports `safe_to_retry: true`, rerun the same Debug build.
   A recovery round consumes neither an implementation attempt nor a test run.
   A Restart Manager query error never authorizes terminating an unverified
   process. It may still be safe to retry when every named artifact was
   deleted or became absent without doing so; if any artifact remains, the
   round is unsafe.

Run at most three recovery-and-build rounds for one build phase. A new locked
artifact reported by the retry is handled by the next round within that same
cap.

## Delayed retries

When three rounds still produce a lock and every holder was this checkout's
own or none was found, the lock usually belongs to a slow scanner, indexer or
another build that will finish. Wait 5, 10, 15 and then 30 minutes, running
one more bounded round after each wait under the same exact-path rules,
until about an hour has passed. Nobody is asked anything meanwhile.

## Exhaustion

Hard-stop only when:

- the delayed retries still produce a lock;
- the failure does not identify an exact locked path, or identifies one
  outside `BUILD_ROOT`;
- the helper reports a surviving IDE, debugger, unknown, or other non-owned
  holder; or
- exact-path safety cannot be established.

Leave the task `in-progress` with its source and artifacts recoverable. Report
the final build error, the helper JSON, the named artifact, and any surviving
holder. Do not publish a task `Block` for this environment failure.
