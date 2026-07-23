---
description: Continue work from the shared ai-tdesktop queue
allowed-tools: Read, Write, Edit, Glob, Grep, Bash, Agent, AskUserQuestion, TodoWrite
---

# Continue AI Work

Read `.claude/ai-workflow-adapter.md` and
`.agents/skills/continue/SKILL.md` completely. Follow the shared skill with the
Claude adapter's delegation and text-handling substitutions. Apply the shared
skill's startup inbox gate exactly once: if the initial queue contains task
work to resume, retry, or start, never invoke `process-inbox` during this run,
even after that work drains. Only an invocation whose initial queue has no
task work may process one inbox transaction, after which it drains the created
and other eligible tasks without checking the inbox again. Continue until the
scheduler's normal stop condition or a global hard stop.

Every inbox worker, performer, and discovered-task routing Agent must be told
in its initial prompt to read `.claude/ai-workflow-adapter.md` completely before
the applicable shared skill or reference. Use the Agent tool for those workers;
do not start Claude subprocesses through Bash.

Arguments, when present, are natural-language hints for new shared work; their
wording decides whether they merely prioritize or strictly restrict selection:

```text
$ARGUMENTS
```
