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

Apply the shared skill's pending-task consolidation phase too. At each eligible
clean AI-slot boundary, recover an older `pending_consolidations` marker or
consolidate newly routed tasks in one fresh foreground Agent. Give it the
effective frozen batch, keep its queue scan and merge reasoning outside this
scheduler context, and require it to read the complete shared consolidation
reference. Never run consolidation inside the performer or discovery-routing
Agent, and defer it while the active task owns dirty local phase state.

Every inbox worker, performer, discovered-task routing Agent, and pending-task
consolidation Agent must be told in its initial prompt to read
`.claude/ai-workflow-adapter.md` completely before the applicable shared skill
or reference. Use the Agent tool for those workers; do not start Claude
subprocesses through Bash.

Arguments, when present, are natural-language hints for new shared work; their
wording decides whether they merely prioritize or strictly restrict selection:

```text
$ARGUMENTS
```
