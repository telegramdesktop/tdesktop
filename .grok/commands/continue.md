---
name: continue
description: Continue work from the shared ai-tdesktop queue. Use when the user invokes /continue or $continue in Grok Build, or asks to resume the active task and drain the frozen startup batch.
---

# Continue AI Work

Read `.grok/ai-workflow-adapter.md` and
`.agents/skills/continue/SKILL.md` completely. Follow the shared skill with
the Grok adapter's delegation, depth, text-handling, UI-driver, and
compaction substitutions. At startup choose exactly one shared-skill mode
and record its frozen task ids: put any active task first and append the
matching existing queue snapshot, drain that snapshot when no task is
active, or, when both are empty, process one inbox transaction and drain
only its routed tasks. Never add unrelated tasks observed later in the
run. Append and implement only new follow-up tasks routed from results
produced by this invocation, including their transitive discovered
follow-ups. Preserve the shared skill's source-project inheritance rule;
detaching a follow-up requires its affirmative independence test. Continue
until that frozen-and-derived batch reaches the scheduler's normal stop
condition or a global hard stop. Do not stop on your own because a compact
is approaching.

Apply the shared skill's pending-task consolidation phase too. At each
eligible clean AI-slot boundary, recover an older `pending_consolidations`
marker or consolidate newly routed tasks in one fresh blocking
`spawn_subagent`. Give it the effective frozen batch, keep its queue scan
and merge reasoning outside this scheduler context, and require it to read
the complete shared consolidation reference. Never run consolidation inside
the performer or discovery-routing worker, and defer it while the active
task owns dirty local phase state.

Every inbox worker, performer, discovered-task routing worker, and
pending-task consolidation worker must be told in its initial prompt to
read `.grok/ai-workflow-adapter.md` completely before the applicable shared
skill or reference. Use `spawn_subagent` with `background: false` for those
workers. A continue-spawned performer is at depth 1 and must run every
phase leaf as a same-session checklist. Do not start Grok subprocesses
through Bash.

Arguments, when present, are natural-language hints for new shared work;
their wording decides whether they merely prioritize or strictly restrict
selection:

```text
$ARGUMENTS
```
