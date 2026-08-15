---
description: Route the local AI inbox into durable planned tasks
allowed-tools: Read, Write, Edit, Glob, Grep, Bash, Agent, AskUserQuestion, TodoWrite
---

# Process Inbox

Read `.claude/ai-workflow-adapter.md` and
`.agents/skills/process-inbox/SKILL.md` completely. Follow the shared skill
with the Claude adapter's delegation and text-handling substitutions. Process
the ignored inbox associated with the current Telegram Desktop checkout. Route
and plan tasks only; do not implement, build, or test them.

Preserve the shared skill's project-continuity bias. A request derived from
existing project work stays in that project unless it passes the skill's
affirmative independence test; shared or cross-surface files alone do not make
it standalone.

Parallel task work is expected. Use the shared skill's `inbox_worktree`,
`inbox_branch`, and `inbox-*` helper commands exactly as documented. Never
substitute the task `slot_worktree`, stop merely because that slot is dirty, or
run broad Git staging from Bash. The shared helper owns the isolated inbox
commit, rebase, master publication, and resumable failure handling.

If a planner Agent is used, tell it to read
`.claude/ai-workflow-adapter.md` before the shared skill instructions. Use the
Agent tool for delegation; do not start Claude subprocesses through Bash.

Arguments, when present, are additional routing hints:

```text
$ARGUMENTS
```
