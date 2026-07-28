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
- Every phase leaf and the performer inherit the parent model, as the shared
  workflow says. Do not pass a model override on the Agent call: its family
  aliases already resolve to the newest permitted model of that family, so an
  override can only pin a leaf below the parent. Do not pass a reasoning field
  either — the Agent tool has none, and effort is inherited unchanged, so every
  leaf keeps the parent's reasoning level.
- Run every phase leaf as a synchronous foreground Agent call. The call
  returning is the completion signal: validate the required files and
  repository state right there, treating the short reply as notification
  only. Do not use background Agent calls plus shell `sleep`/`until` polling
  loops for phase leaves — the Codex wait ladder, heartbeat-mtime checks, and
  five-minute stall windows in the shared references are Codex-only mechanics
  and do not apply in Claude Code. Leaves still write their progress files
  (they are cheap resumability evidence), but the performer never polls them.
- Run the independent leaves of one step — the review lenses plus the
  iteration-1 test-design leaf, or assessed-disjoint implementation units —
  as parallel Agent calls in a single message so they run concurrently and
  all return together.
- A long Debug build may run as background Bash; the harness re-invokes the
  session when a background command exits, so do not poll its log with sleep
  loops either.
- When a synchronous leaf call returns without its required artifact, retry
  that disposable phase once in a fresh Agent with more specific instructions.
  When Claude exposes a resumable agent id and more work is needed from that
  same stateful worker, resume that id; never create a duplicate performer or
  duplicate an agent whose writes may still be in flight. Never launch a
  nested `claude` process from Bash.
- If the first real leaf Agent is rejected before work begins because nested
  delegation is unavailable, use the shared same-session fallback. Do not
  treat mere presence of the Agent tool as a successful delegation probe.
- Whenever an Agent is asked to run `process-inbox`, `perform-task`, or
  discovered-task routing — the orchestrating roles — explicitly tell it to
  read this adapter completely before the applicable shared skill or
  reference. Do NOT tell leaf phase agents to read this adapter: their phase
  prompts are self-contained and already carry the leaf rules (no delegation,
  no commits, progress and reply contracts); an adapter read there is wasted
  context.

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
