# Codex CLI leaves

Use this contract when a non-Codex host runs a phase leaf through the Codex
CLI, which today means Claude Code running perform-task reviews and
assessments or the `cross-review` command. A Codex host never does this: it
delegates through native subagents and never shells out to `codex exec`.

The helper is `.agents/shared/scripts/codex_leaf.py`. Run it with the host's
Python 3 (`python3`, `python` or `py -3`, `-I` where available).

## Availability

```bash
python3 SOURCE_ROOT/.agents/shared/scripts/codex_leaf.py check
```

It prints one JSON line and exits non-zero when `codex` is missing from
`PATH`, does not start, or is not logged in. Check once per invocation before
the first Codex leaf. When it fails, run the same phases as the host's native
leaves exactly as before and record `Review provider: native (codex
unavailable: <reason>)` in the phase result `NOTES:`. Never install, update or
log in to Codex on the user's behalf.

## Effort and model

Select effort with [phase effort](phase-effort.md) and pass it as `--effort`
(`medium`, `high` or `xhigh`; review and assessment phases default to
`xhigh`). Pass `--model` only where
[lens models](phase-effort.md#lens-models) pin one (the reuse, structure and
performance lenses); otherwise the leaf uses the model configured for the
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

Each leaf keeps `<name>.json`, `<name>.jsonl`, `<name>.stderr.txt` and
`<name>.last.md` in one run directory. These transcripts are bulky and
machine-specific: perform-task keeps them in `TASK_DIR/.local/codex/`, and
other callers in ignored storage outside the working tree. Never commit them.

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

A finished leaf is `done` or `failed`. `done` only means Codex exited with a
final message; validate the phase's own completion checks on its artifacts,
and confirm `git -C SOURCE_ROOT status --porcelain` is unchanged by the leaf.
The final message is the compact reply the phase prompt asks for; copy what
the result log needs from `<name>.last.md`.

## Continuing a leaf

A phase that continues the same reviewer, such as the general reviewer's
synthesis pass, starts a new leaf with `--resume <earlier leaf name>`. It
continues that Codex session with the new prompt and the same sandbox rules.
Resume only a `done` leaf.

## Recovery

- A leaf runs at most 120 minutes; the helper then kills it and marks it
  `failed`. Pass `--max-minutes` only for a phase known to need longer.
- On `failed`, or on `done` with an incomplete artifact, read the reported
  stderr tail, then rerun that one leaf once with `--replace` and more
  specific instructions. A second failure runs the phase as the host's native
  leaf; record the switch in `NOTES:`.
- `start` refuses a name that is still running. Never run two leaves that
  write the same artifact: `stop --name` the old one first, and only then
  `--replace` it.
- A Codex usage-limit or authentication error is an availability failure, not
  a phase failure: switch the remaining Codex phases of this invocation to
  native leaves and record why.
