---
description: Implement one existing AI task by short name or full id
allowed-tools: Read, Write, Edit, Glob, Grep, Bash, Agent, AskUserQuestion, TodoWrite
---

# Perform One AI Task

Read `.claude/ai-workflow-adapter.md` and
`.agents/skills/perform-task/SKILL.md` completely. Follow the shared skill with
the Claude adapter's delegation and text-handling substitutions. Resolve,
claim when needed, implement, verify, and publish only the named task. Do not
continue with other queue work afterward.

Tell every phase Agent to read `.claude/ai-workflow-adapter.md` before its
shared phase prompt. Use the Agent tool for delegation; do not start Claude
subprocesses through Bash.

Task short name or full id:

```text
$ARGUMENTS
```
