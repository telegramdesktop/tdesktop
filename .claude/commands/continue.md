---
description: Continue work from the shared ai-tdesktop queue
allowed-tools: Read, Write, Edit, Glob, Grep, Bash, Agent, AskUserQuestion, TodoWrite
---

# Continue AI Work

Read `.claude/ai-workflow-adapter.md` and
`.agents/skills/continue/SKILL.md` completely. Follow the shared skill with the
Claude adapter's delegation and text-handling substitutions. At startup choose
exactly one shared-skill mode and record its frozen task ids: put any active
task first and append the matching existing queue snapshot, drain that snapshot
when no task is active, or, when both are empty, process one inbox transaction
and drain only its routed tasks. Never add unrelated tasks observed later in
the run. Append and implement only new follow-up tasks routed from results
produced by this invocation, including their transitive discovered follow-ups.
Preserve the shared skill's source-project inheritance rule; detaching a
follow-up requires its affirmative independence test. Continue until that
frozen-and-derived batch reaches the scheduler's normal stop condition or a
global hard stop.

Apply the shared `split-required` result and routing phase. A performer publishes
that state; this parent launches one fresh foreground split Agent that reads the
complete shared split reference, creates and publishes the replacements, and
retires the source task. Replace the source id in the frozen batch with those
replacements. When retained implementation exists, start the checkout-owned
carrier first so the helper transfers its sealed source state; do not reset or
checkpoint that work.

Apply the shared source-lineage gate in this parent scheduler. Before batch
freeze, a missing approved prerequisite pauses through `AskUserQuestion` after
the exact branch and compatible-branch report. After freeze but before Phase 1,
the parent may make only the shared safe switch to an existing compatible local
branch and then resume the returned performer. After Phase 1, let the performer
publish a task-local Block and continue non-dependent batch work. Never create,
route, or execute branch-integration work as a queue task.

Apply the shared skill's pending-task consolidation phase too. At each eligible
clean AI-slot boundary, recover an older `pending_consolidations` marker or
consolidate newly routed tasks in one fresh foreground Agent. Give it the
effective frozen batch, keep its queue scan and merge reasoning outside this
scheduler context, and require it to read the complete shared consolidation
reference. Never run consolidation inside the performer or discovery-routing
Agent, and defer it while the active task owns dirty local phase state.

Every inbox worker, performer, split-routing Agent, discovered-task routing
Agent, and pending-task consolidation Agent must be told in its initial prompt
to read `.claude/ai-workflow-adapter.md` completely before the applicable shared
skill or reference. Use the Agent tool for those workers; do not start Claude
subprocesses through Bash.

Arguments, when present, are natural-language hints for new shared work; their
wording decides whether they merely prioritize or strictly restrict selection:

```text
$ARGUMENTS
```
