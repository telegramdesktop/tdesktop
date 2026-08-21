# Grok Build AI Workflow Adapter

Apply this adapter only when a command or shared skill explicitly loads it.
The shared `.agents/skills/` workflow remains authoritative for task
selection, artifacts, source changes, builds, testing, commits,
resumability, and AI publication. This file adapts harness mechanics.

## Delegation

- Use Grok's `spawn_subagent` tool wherever the shared workflow says to spawn
  a subagent, worker, performer, planner, or leaf.
- Treat `fork_turns: "none"` as a fresh `spawn_subagent` call with a
  self-contained prompt containing exact repository, task, artifact, and
  input paths. Do not rely on the parent conversation being inherited.
- Always pass `background: false`. The call returning is the completion
  signal. Validate the required files and repository state right there,
  treating the short reply as notification only.
- Do not use `background: true` plus `get_command_or_subagent_output`,
  shell `sleep`/`until` polling, the Codex wait ladder, heartbeat-mtime
  checks, or five-minute stall windows. Those are Codex-only. Leaves still
  write their progress files (they are cheap resumability evidence), but
  the parent never polls them.
- There is no `wait_agent`, `list_agents`, `send_message`, `followup_task`,
  `interrupt_agent`, or `spawn_agent`. Do not invent them. Do not launch
  `grok`, `claude`, or `codex` from Bash.
- Do not use the `workflow` tool to reimplement this pipeline.
- Do not pass `isolation: worktree`. The shared `workspace.py` helper owns
  every AI and inbox worktree.
- Omit `model` and any reasoning field so every child inherits this
  session. Do not invent tool arguments the schema does not expose.
- Use `subagent_type: "general-purpose"` for inbox, performer, routing,
  consolidation, and phase leaves. Do not restrict `capability_mode`;
  those workers need shell plus writes.
- Tell every disposable phase leaf not to delegate and never to commit.
  Publication-owning orchestrators — `process-inbox`, `perform-task`,
  discovered routing, and pending-task consolidation — follow the shared
  workflow's exact helper, commit, and publication contract. Preserve its
  single-writer and one-stateful-performer constraints.
- When a blocking call returns without its required artifact, retry that
  disposable worker once in a fresh `spawn_subagent` with more specific
  instructions. If Grok returned a completed subagent id and more work is
  needed from that same stateful worker, resume it with `resume_from`;
  never create a duplicate performer or duplicate an agent whose writes
  may still be in flight.
- A long Debug build may run as `run_terminal_command` with
  `background: true`. Wait on its task id; do not poll its log with sleep
  loops.

## Depth

Grok subagents cannot spawn subagents. Depth is one. A child that calls
`spawn_subagent` fails. Do not treat presence of the tool as a successful
nested-delegation probe.

### `/continue` in this session

This session is the scheduler only. Do not plan or implement Telegram
changes here.

Spawn each inbox worker, performer, discovery-routing worker, and
pending-task consolidation worker as a blocking `spawn_subagent`. Tell
every one of those workers, in its initial prompt, to read this adapter
completely before the applicable shared skill or reference.

A performer spawned from `/continue` is already at depth 1. Its prompt
must also say:

```text
You are a Grok subagent at depth 1. Do not call spawn_subagent.
Run every phase leaf as a same-session checklist from
.agents/skills/perform-task/references/phase-prompts.md.
```

That is the shared same-session fallback, selected from the first
performer, not a degraded failure. Do not tell a continue-spawned
performer that it may use bounded leaf delegation.

### `/perform-task` or `/process-inbox` in this session

This session is the orchestrator and may spawn leaves.

- Run each leaf as one blocking `spawn_subagent` with a self-contained
  prompt. Do not tell leaf phase agents to read this adapter.
- Spawn independent leaves that truly share one step — the selected
  specialist reviews in an iteration, or assessed-disjoint implementation
  units — as parallel `spawn_subagent` calls in a single message. Run the
  mandatory general reviewer only after the selected specialist reports
  exist.
- If the first real leaf is rejected before work begins because nested
  delegation is unavailable, use the shared same-session fallback for
  the rest of the run.

## Text handling

Do not run a dedicated line-ending or BOM check, normalization, repair,
phase, artifact, rebuild, or summary. In particular:

- ignore the tracked-text line-ending validation item in `process-inbox`;
- skip perform-task Phase 7 entirely on every host;
- ignore the pipeline's Normalize step and every Phase 7 prompt, result,
  retry, completion check, and reporting requirement;
- do not rewrite a file solely to change LF, CRLF, mixed endings, or BOM
  state.

Let normal Grok editing preserve the checkout's existing text convention.
This exception removes only explicit line-ending work; it does not relax
any content validation, owned-path rule, build, review, test, or
publication gate.

## UI-driver capability

The shared Computer Use reference describes Codex's driver. In Grok,
treat that driver as unavailable unless an equivalent UI-driver tool is
actually exposed in the current session. Preserve the same policy: `auto`
uses the already planned overlay fallback, while `required` reports the
exact unverified interaction. Driver availability never permits skipping
selected runtime, overlay, account-safety, evidence, or other safety checks.

Judge overlay screenshots and supplied mockups by reading the image files
with `read_file`. Saved PNG/JPG artifacts are visual input. A missing
desktop driver is not missing evidence when the overlay captured the
widget or window.

## Compaction

Do not stop a `/continue` drain because a compact is approaching, remaining
compacts are low, or the parent window is large. The scheduler is a thin
loop: helper JSON, worker prompts, artifact checks. A compact of that
parent is fine. If the host compact or the session dies, that is the stop;
leave in-progress task state recoverable and let the next `/continue`
invocation resume it.

The only scheduler state that is not already on disk is the frozen batch
(`invocation_mode`, `initial_batch_task_ids`, `batch_task_ids`,
`discovered_task_ids`, `attempted_blocked`, consolidation records). After
a compact, recover those lists from the compact summary and the last
scheduler notes. Do not take a fresh queue snapshot and freeze a new
batch. A later queue id that was never in this invocation's batch stays
out, exactly as the shared skill says.
