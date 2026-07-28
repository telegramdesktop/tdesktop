---
description: Implement one existing AI task by short name or full id
allowed-tools: Read, Write, Edit, Glob, Grep, Bash, Agent, AskUserQuestion, TodoWrite
---

# Perform One AI Task

Read `.claude/ai-workflow-adapter.md` and
`.agents/skills/perform-task/SKILL.md` completely. Follow the shared skill with
the Claude adapter's delegation and text-handling substitutions. Resolve, start
or resume, implement, verify, and publish only the named task. Do not
continue with other queue work afterward.

Delegate phases with synchronous foreground Agent calls per the adapter; leaf
phase agents receive self-contained prompts and are not told to read the
adapter. Use the Agent tool for delegation; do not start Claude subprocesses
through Bash.

Task short name or full id:

```text
$ARGUMENTS
```
