# Codex CLI leaves

Use this contract when a non-Codex host runs a phase leaf through the Codex
CLI, which today means Claude Code running perform-task reviews and
assessments or the `cross-review` command. A Codex host never does this: it
delegates through native subagents and never shells out to `codex exec`.

The helper is `.agents/shared/scripts/codex_leaf.py`. Run it with the host's
Python 3 (`python3`, `python` or `py -3`, `-I` where available).

## Codex home

Codex keeps its login and session history in its home folder. `check` and
`start` take `--home`:

- `work`, the default, runs Codex with
  `CODEX_HOME=~/.codex-accounts/work` (`%USERPROFILE%\.codex-accounts\work` on
  Windows), a login kept apart from the user's own;
- `default` leaves `CODEX_HOME` to the environment, which normally means
  `~/.codex`.

Each home has its own `config.toml`, so the `work` home needs a copy of the
user's settings, including the Windows sandbox mode on Windows.

Pass `--home default` to `check` and every `start` when the invocation's
review provider is `codex-default`, per the host adapter; otherwise omit it. A
`--resume` leaf always runs in its source leaf's home, because the thread
lives there, whatever `--home` it is given.

## Availability

```bash
python3 SOURCE_ROOT/.agents/shared/scripts/codex_leaf.py check [--home default]
```

It prints one JSON line and exits non-zero when `codex` is found neither on
`PATH` nor bundled in the Codex app, does not start, its home folder does not
exist, or it is not logged in there. Check once per invocation before the
first Codex leaf. When it fails, run the same phases as the host's native
leaves exactly as before and record `Review provider: native (codex
unavailable: <reason>)` in the phase result `NOTES:`. Never install, update
or log in to Codex on the user's behalf, and never switch to the other home
on your own: a missing `work` login is reported, not replaced with `~/.codex`.

## Effort and model

Select effort with [phase effort](phase-effort.md) and pass it as `--effort`
(`medium`, `high` or `xhigh`; review and assessment phases default to
`xhigh`). Pass `--model` only where
[lens models](phase-effort.md#lens-models) pin one (the reuse, structure and
performance lenses, and every review and assessment leaf of a harness task);
otherwise the leaf uses the model configured for the
user's Codex CLI. Record the effort and any pinned model in the phase result
`NOTES:`.

## Working directory and sandbox

A Codex leaf never edits the source checkout, never commits, and never runs
with `danger-full-access` or a sandbox bypass.

- **A leaf that writes artifacts**, such as a perform-task review or
  assessment, runs with `--cwd WORK_DIR --sandbox workspace-write
  --no-project-doc`. The sandbox lets it write only inside `WORK_DIR` (and
  temporary storage) while it can read everything else. `--no-project-doc`
  keeps the AI queue repository's own `AGENTS.md` out of its context, because
  `WORK_DIR` lives in that repository.
- **A leaf that only reports** runs with `--cwd SOURCE_ROOT --sandbox
  read-only` and delivers its result as its final message. The source
  checkout's `AGENTS.md` loads automatically.

On Windows the sandbox runs commands as a separate local user. The helper
marks repositories safe for git through the child's environment only, so the
leaf can run read-only git commands on `SOURCE_ROOT`; never change the user's
global git configuration for it.

## Prompt

Use the phase's ordinary prompt file unchanged after this preamble, which the
caller prepends when the leaf's working directory is not `SOURCE_ROOT`:

```text
Source checkout: <SOURCE_ROOT>
Your working directory is <WORK_DIR>; you can write only there. Resolve every
relative repository path in this prompt, including AGENTS.md and REVIEW.md,
against the source checkout, and read AGENTS.md and REVIEW.md there before
reviewing. Run git as `git -C <SOURCE_ROOT> ...`. Never try to edit files in
the source checkout or work around the sandbox.
```

The helper feeds the prompt file to `codex exec` on standard input, so its
text needs no shell quoting. Keep the phase prompt files where the calling
workflow already keeps them.

## Run directory

Each leaf keeps its files in one run directory on a local disk: `<name>.json`
(state), `<name>.jsonl` (Codex's event transcript), `<name>.stderr.txt`
(Codex's stderr), `<name>.last.md` (the final message), `<name>.runner.log`
(the helper's own launch and exit diagnostics, apart from Codex's stderr) and
`<name>.lock` (serializes state changes). They are bulky and
machine-specific: perform-task keeps them in `TASK_DIR/.local/codex/`, and
other callers in ignored storage outside the working tree. Never commit them,
and never move or delete a leaf's files by hand; `start --replace` archives
them.

## Start and wait

Start each leaf, then wait for all leaves of the step together:

```bash
python3 SOURCE_ROOT/.agents/shared/scripts/codex_leaf.py start \
  --run-dir RUN_DIR --name review1-lifetime --cwd WORK_DIR \
  --prompt-file WORK_DIR/logs/phase-6a-review-1-lifetime.prompt.md \
  --sandbox workspace-write --no-project-doc --effort xhigh
python3 SOURCE_ROOT/.agents/shared/scripts/codex_leaf.py wait \
  --run-dir RUN_DIR --name review1-lifetime --name review1-reuse
```

`start` returns at once; the leaf runs detached. Start independent leaves of
one step back to back so they run concurrently. `wait` blocks until every
named leaf has finished or about nine minutes have passed. Exit 0 means all
finished; exit 2 means some are still running, so call `wait` again at once in
the foreground. This blocking call is the completion signal for a Codex leaf.
It is not an artifact poll: do not add sleeps, mtime checks, progress reads or
commentary between calls, and never end the turn while a leaf is running.

`wait` watches the leaf's own processes, not only the detached runner process
that started Codex. A leaf whose runner was lost while Codex works on shows as
`running ... runner-lost`, with its processes as `pid=` when known. That is
not a failure: keep calling `wait`, and never stop, rerun or replace the leaf;
`wait` judges its outcome from the transcript and final message once Codex
exits, and ends it at the time limit. A running leaf shown with
`unresolved=<detail>` is one for which the helper cannot prove whether a
process still runs, for example because a probe failed; keep calling `wait`,
and `stop` it only if it is still `unresolved` past its time limit.

A finished leaf is `done` or `failed`, or `stopped` after `stop`; `missing`
means the run directory has no leaf of that name. `done` means the
transcript's last turn event is `turn.completed`, this run wrote a nonempty
final message, and Codex exited 0 when its exit is known; anything else is
`failed` with a `reason=`. `done` says nothing about the phase's artifacts:
validate the phase's own completion checks on them, and confirm `git -C
SOURCE_ROOT status --porcelain` is unchanged by the leaf. The final message is
the compact reply the phase prompt asks for; copy what the result log needs
from `<name>.last.md`.

## Continuing a leaf

A phase that continues the same reviewer, such as the general reviewer's
synthesis pass, starts a new leaf with `--resume <earlier leaf name>`. It
continues that Codex session with the new prompt and the same sandbox rules.
Resume only a `done` leaf. The helper takes the Codex thread from the earlier
leaf's state or, failing that, its transcript, and runs in that leaf's Codex
home. It refuses with `resume-source-running` while the earlier leaf still
runs, `resume-no-thread` when it has no Codex thread, and `resume-self` when
`--resume` names the new leaf itself.

## Recovery

A refused command exits 1 with one `codex_leaf:` line; for the refusals below
it reads `codex_leaf: <reason>: <text>`.

- A leaf runs at most 120 minutes: the runner then ends its processes and
  marks it `failed` with `exceeded N minutes`, or, after runner loss, the next
  `wait` does, with `exceeded N minutes (runner lost; ended by wait)`. Pass
  `--max-minutes` only for a phase known to need longer.
- `home-missing` means the selected Codex home folder does not exist; `start`
  creates nothing. Treat it like a failed `check`.
- On `failed`, or on `done` with an incomplete artifact, read the `reason=`,
  the `!` lines (Codex's stderr) and the `runner:` and `>` lines (the launch
  mode and the runner log), then rerun that one leaf once with `--replace` and
  more specific instructions. A second failure runs the phase as the host's
  native leaf; record the switch in `NOTES:`.
- Without `--replace`, `start` refuses a name that already has a state
  (`exists`) or files (`files-without-state`). Whatever status the old leaf
  records, `start --replace` refuses while a process of it runs
  (`still-running`) or cannot be shown to have exited (`unresolved`), naming
  the PIDs when known; a refusal moves or changes no file of the old leaf and
  starts nothing. Wait for the old leaf or `stop --name` it first. Never run
  two leaves that write the same artifact.
- `start --replace` archives the old run's files, all but `<name>.lock`,
  under one `.prev-<stamp>` suffix, or none: `archive-failed` names the file
  that would not move and leaves every file at its path, or names each one it
  could not move back. Retry once whatever held the file is gone.
- `stop --name` marks a running leaf `stopped`, keeps a finished leaf's
  status, and ends the leaf's processes, also after runner loss. It exits 0
  once none is left; otherwise it prints `still running (...)` naming what it
  could not end and exits 1, and nothing may replace the leaf or write its
  artifacts until those end. It cannot reach processes Codex itself detaches.
- `state-lock-busy`, `state-lock-failed` or `state-write-failed` means the
  leaf's state could not be locked or written; fix the cause and repeat the
  command once. `launch-failed` leaves the leaf `failed`; recover it as above.
- A Codex usage-limit or authentication error, such as a `turn failed:`
  reason naming a 429 status, is an availability failure, not a phase
  failure: switch the remaining Codex phases of this invocation to native
  leaves and record why.
