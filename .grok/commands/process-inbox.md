---
name: process-inbox
description: Route the local AI inbox into durable planned tasks. Use when the user invokes /process-inbox or $process-inbox in Grok Build.
---

# Process Inbox

Read `.grok/ai-workflow-adapter.md` and
`.agents/skills/process-inbox/SKILL.md` completely. Follow the shared skill
with the Grok adapter's delegation, depth, and text-handling substitutions.
Process the ignored inbox associated with the current Telegram Desktop
checkout. Route and plan tasks only; do not implement, build, or test them.

Preserve the shared skill's project-continuity bias. A request derived from
existing project work stays in that project unless it passes the skill's
affirmative independence test; shared or cross-surface files alone do not
make it standalone.

Parallel task work is expected. Use the shared skill's `inbox_worktree`,
`inbox_branch`, and `inbox-*` helper commands exactly as documented. Never
substitute the task `slot_worktree`, stop merely because that slot is dirty,
or run broad Git staging from Bash. The shared helper owns the isolated
inbox commit, rebase, master publication, and resumable failure handling.

If a planner subagent is used, tell it to read
`.grok/ai-workflow-adapter.md` before the shared skill instructions. Use
`spawn_subagent` with `background: false` for delegation; do not start Grok
subprocesses through Bash.

Arguments, when present, are additional routing hints:

```text
$ARGUMENTS
```
