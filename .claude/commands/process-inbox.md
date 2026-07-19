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

If a planner Agent is used, tell it to read
`.claude/ai-workflow-adapter.md` before the shared skill instructions. Use the
Agent tool for delegation; do not start Claude subprocesses through Bash.

Arguments, when present, are additional routing hints:

```text
$ARGUMENTS
```
