# Autonomous Windows Failed-Build and Lock Recovery

This contract applies to Debug builds run by `continue` / `perform-task`.
Those workflows own the selected checkout's build tree for the duration of
the task, so a failed build or a transient lock is recoverable and is not a
task `Block` or a global hard stop on first sight.

## Safety boundary

Resolve `SOURCE_ROOT`, its exact `BUILD_ROOT`, and the exact checkout `EXE`
before acting.

- Process control may target the exact `EXE`, or a verified compiler/linker
  process tied to `BUILD_ROOT` or directly holding one named artifact inside
  it.
- File deletion may target only exact named artifacts inside `BUILD_ROOT`:
  an output a lock error names, or the Debug object that the tree's own
  `Cl.items.tlog` maps to a unit the failed build's log reports a compile
  error for.
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

## Recover a failed build

A Debug build of `BUILD_ROOT` has failed when it exits nonzero or when its
complete output holds any line matching `(error|fatal error) (C|LNK|MSB)[0-9]+`,
including errors hidden by an exit 0. In a `/MP` batch a unit that fails, even
after it opened its PCH, can keep its old object while MSBuild shrinks the
unit's tracked header inputs to what the compiler read before it stopped. A
later build then neither recompiles that unit nor reports an error, and links
an object compiled against an older header layout, so an error-free link
after a failure proves nothing. Recover every failed build of the task, not
only the latest one, before the next build of that tree:

1. Preserve the complete build output in the task's `.local/build-logs/` as
   `BUILD_LOG`.
2. Run:

   ```bash
   python SOURCE_ROOT/.agents/skills/process-inbox/scripts/workspace.py \
     build-lock-recover \
     --source-root SOURCE_ROOT \
     --build-root BUILD_ROOT \
     --exe EXE \
     --failed-log BUILD_LOG \
     --wait 0
   ```

   The helper attributes each `error C<digits>` line of the log to its unit
   through the `(compiling source file '...')` line right after it, unless
   that line carries an MSBuild stream prefix (`N>`) other than the
   diagnostic's, or else through the diagnostic's own `.c`, `.cc`, `.cpp` or
   `.cxx` path. Interleaved `/MP` output can put another unit's attribution
   there, so an attribution that names a unit other than the diagnostic's own
   source file is rejected. The helper maps each unit through its project's
   Debug `Cl.items.tlog` and deletes exactly those objects under the same
   holder checks. `failed_sources` lists the units. A log with only link or
   MSBuild errors derives no object. When the log also reports a lock, run
   `Recover a lock` instead: its round passes the same
   `--failed-log BUILD_LOG` together with the exact `--artifact` paths.
3. `safe_to_retry: true` means the failed objects are gone and no holder
   remains, not that the next build will pass. A code error still needs source
   repair. A PCH commit or heap failure (`C3859`, `C1076`, `C1060`) is an
   environment failure, usually concurrent builds exhausting memory: never
   work around it in source; rebuild within the three recovery-and-build
   rounds of `Recover a lock`, and if it still fails, stop under `Exhaustion`
   with the failed objects deleted.
4. A helper error (an unrecognized, unattributed or contradictorily
   attributed compile error, a reported unit without a Debug item, a project
   or object outside `BUILD_ROOT`, a missing log) leaves the recovery
   unresolved and the tree not evidence-ready. Never guess objects from
   source names, `CL.read` tlogs or Release items; stop under `Exhaustion`.

A validation or evidence build may follow a failed build only after that
build's failed objects have been recovered.

## Recover a lock

Treat `C1041`, `LNK1104`, `cannot open output file`, `access denied`, `file in
use`, and equivalent failures on an output under `BUILD_ROOT` as one recovery
round:

1. Preserve the complete build output in the task's `.local/build-logs/` as
   `BUILD_LOG`.
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
     --failed-log BUILD_LOG \
     --artifact LOCKED_PATH \
     --wait 10
   ```

   Repeat `--artifact` for multiple named outputs. `--failed-log` adds the
   objects of the same build's failed compile units, as in `Recover a failed
   build`, and a helper error about it leaves the round unresolved. The
   helper waits for transient scanners, stops the exact checkout executable,
   uses Windows Restart Manager to identify direct holders, terminates only
   verified build processes, and deletes only the named artifacts so the next
   build regenerates them. An artifact that disappears during the wait is
   already recovered and is reported as `already_absent`.
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
Delayed retries are for locks only: never wait out a source error or a PCH
memory failure this way, and never invent a locked path for either.

## Exhaustion

Hard-stop only when:

- the delayed retries still produce a lock;
- three rounds still fail with a PCH commit or heap failure;
- the failure does not identify an exact locked path, or identifies one
  outside `BUILD_ROOT`;
- the helper rejects a failed build's log: an unresolved compile mapping,
  which is not a lock without an exact path. Neither authorizes guessing an
  artifact;
- the helper reports a surviving IDE, debugger, unknown, or other non-owned
  holder; or
- exact-path safety cannot be established.

Leave the task `in-progress` with its source and artifacts recoverable. Report
the final build error, the helper JSON or its rejection, the named artifact,
and any surviving holder. Do not publish a task `Block` for this environment
failure.
