---
description: Continue work from the shared ai-tdesktop queue
allowed-tools: Read, Write, Edit, Glob, Grep, Bash, Agent, AskUserQuestion, TodoWrite
---

# Continue AI Work

Read `.claude/ai-workflow-adapter.md` and
`.agents/skills/continue/SKILL.md` completely. Follow the shared skill with the
Claude adapter's delegation and text-handling substitutions. Keep processing
the inbox and eligible work until the scheduler's normal stop condition or a
global hard stop.

Every inbox worker, performer, and discovered-task routing Agent must be told
in its initial prompt to read `.claude/ai-workflow-adapter.md` completely before
the applicable shared skill or reference. Use the Agent tool for those workers;
do not start Claude subprocesses through Bash.

Arguments, when present, are natural-language scheduling or claim-scope hints:

```text
$ARGUMENTS
```
