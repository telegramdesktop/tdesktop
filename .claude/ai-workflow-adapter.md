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
- Tell every disposable phase leaf Agent not to delegate and never to commit.
  Publication-owning orchestrators — `process-inbox`, `perform-task`, split
  routing, discovered routing, and pending-task consolidation — follow the
  shared workflow's exact helper, commit, and publication contract instead.
  Preserve its single-writer and one-stateful-performer constraints.
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
- Run independent leaves that truly share one step — the surviving specialist
  reviews in an iteration, or assessed-disjoint implementation units — as
  parallel Agent calls in a single message so they run concurrently and all
  return together. The mandatory general reviewer is not one of them: it runs
  alone before them to emit the retirement list, and alone again after their
  reports exist to own the verdict.
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
- Whenever an Agent is asked to run `process-inbox`, `perform-task`, split-task
  routing, discovered-task routing, or pending-task consolidation — the
  orchestrating roles — explicitly tell it to read this adapter completely before the
  applicable shared skill or reference. Do NOT tell leaf phase agents to read
  this adapter: their phase prompts are self-contained and already carry the
  leaf rules (no delegation, no commits, progress and reply contracts); an
  adapter read there is wasted context.

## Model self-reporting

`workspace.py finish` requires `--model` and records it in the task's
`state.yaml`. In Claude Code, report the Claude model actually running the
performer, lowercase, without a context-window or date suffix:
`claude-opus-5`, `claude-sonnet-5`, `claude-fable-5`, `claude-haiku-4-5`. A
model id such as `claude-opus-5[1m]` becomes `claude-opus-5`. Take the value
from the model you are running as, never from the task, the queue, or another
checkout's records.

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

## Source lineage and branch routing

The shared skills' source-lineage policy applies unchanged. Keep its timing
boundaries in the Claude orchestrator rather than delegating the decision to a
leaf:

- The parent `continue` command runs the startup `source-lineage` scan before
  freezing the batch. On a mismatch, report the lineage JSON and use
  `AskUserQuestion` to pause for human direction. Do not start an Agent, switch
  branches, or perform or route a cherry-pick, rebase, merge, backport,
  forward-port, or branch-sync task at this boundary.
- Once the batch is frozen, the parent scheduler owns the shared skill's safe
  pre-Phase-1 branch switch. Use Bash only for the required cleanliness,
  recovery-ref, process, and `git worktree list --porcelain` checks and for
  `git switch` to an existing compatible local branch. Never ask a performer or
  leaf Agent to integrate history.
- When a synchronous performer returns the shared pre-Phase-1 lineage stop,
  switch safely in the parent and resume that Agent id when Claude exposes one.
  If it exposes no resumable id, start one replacement performer only after the
  returned worker and repository checks prove there are no phase artifacts,
  source refs, overlay, or writes. This is a clean setup retry, not a second
  concurrent performer.
- A lineage mismatch first established after Phase 1 stays inside the
  performer: it restores owned/disposable state and publishes the shared
  task-local `Block`. Once the foreground call returns, `continue` records the
  attempt and proceeds with non-dependent batch work; it does not ask the human
  merely because that one task blocked.

`process-inbox` and discovered-routing Agents must use the shared receipt-only
disposition for requests whose sole work is moving an existing commit between
branches. Their initial prompts already require this adapter and the applicable
shared skill, so do not restate or weaken that routing rule in a leaf prompt.

## UI-driver capability

The shared Computer Use reference describes Codex's driver. In Claude Code,
treat that particular driver as unavailable unless an equivalent UI-driver
tool is actually exposed in the current session. Preserve the same policy:
`auto` uses the already planned overlay fallback, while `required` reports the
exact unverified interaction. Driver availability never permits skipping the
selected runtime, overlay, account-safety, evidence, or other safety checks.
