# Claude Code AI Workflow Adapter

Apply this adapter only when a command explicitly loads it. The shared
`.agents/skills/` workflow remains authoritative for task selection, artifacts,
source changes, builds, testing, commits, resumability, and AI publication.
This file adapts harness mechanics and removes unnecessary text normalization.

## Delegation

- Use Claude Code's `Agent` tool wherever the shared workflow says to spawn a
  subagent, worker, performer, planner, or leaf.
- Treat `fork_turns: "none"` as a fresh Agent invocation with a self-contained
  prompt containing exact repository, task, artifact, and input paths. Do not
  rely on the parent conversation being inherited.
- Tell every leaf Agent not to delegate and never to commit. Preserve the
  shared workflow's single-writer and one-stateful-performer constraints.
- A foreground Agent call may replace Codex-specific polling. Treat its short
  reply as notification and validate the required files and repository state.
  When Claude exposes a resumable agent id and more work is needed from that
  same stateful worker, resume that id; never create a duplicate performer or
  duplicate an agent whose writes may still be in flight.
- Translate Codex-specific wait, message, follow-up, list, and interrupt calls
  to the closest available Agent operation. Preserve the artifact heartbeat,
  stall windows, one-retry limit, and terminal-state rules. Never launch a
  nested `claude` process from Bash.
- If the first real leaf Agent is rejected before work begins because nested
  delegation is unavailable, use the shared same-session fallback. Do not
  treat mere presence of the Agent tool as a successful delegation probe.
- Whenever an Agent is asked to run `process-inbox`, `perform-task`, a phase
  prompt, or discovered-task routing, explicitly tell it to read this adapter
  completely before the applicable shared skill or reference.

## Text handling

Do not run a dedicated line-ending or BOM check, normalization, repair, phase,
artifact, rebuild, or summary in Claude Code. In particular:

- ignore the tracked-text line-ending validation item in `process-inbox`;
- skip perform-task Phase 7 entirely on every host;
- ignore the pipeline's Normalize step and every Phase 7 prompt, result, retry,
  completion check, and reporting requirement;
- do not rewrite a file solely to change LF, CRLF, mixed endings, or BOM state.

Let normal Claude editing preserve the checkout's existing text convention.
This exception removes only explicit line-ending work; it does not relax any
content validation, owned-path rule, build, review, test, or publication gate.

## UI-driver capability

The shared Computer Use reference describes Codex's driver. In Claude Code,
treat that particular driver as unavailable unless an equivalent UI-driver
tool is actually exposed in the current session. Preserve the same policy:
`auto` uses the already planned overlay fallback, while `required` reports the
exact unverified interaction. Driver availability never permits skipping the
ordinary overlay test loop, account setup, evidence, or safety rules.
