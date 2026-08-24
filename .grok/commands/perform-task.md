---
name: perform-task
description: Implement one existing AI task by short name or full id. Use when the user invokes /perform-task or $perform-task in Grok Build with a known task name.
---

# Perform One AI Task

Read `.grok/ai-workflow-adapter.md` and
`.agents/skills/perform-task/SKILL.md` completely. Follow the shared skill
with the Grok adapter's delegation, depth, text-handling, and UI-driver
substitutions. Resolve, start or resume, implement, verify, and publish only
the named task. Do not continue with other queue work, route discoveries, or
consolidate pending tasks afterward; those post-result phases belong to the
`continue` scheduler.

Apply the shared pipeline's conditional `[ai] ` commit-subject rule exactly;
decide it per commit, require it only when all changes and the purpose are
exclusively AI workflow or infrastructure, and forbid it for product or mixed
commits. Count `.grok/` with `.agents/` and `.claude/` for that decision.

This session is the performer, so leaf delegation is available. Delegate
phases with blocking `spawn_subagent` calls per the adapter; leaf phase
agents receive self-contained prompts and are not told to read the adapter.
If the first real leaf is rejected because nested delegation is
unavailable, use the shared same-session fallback. Do not start Grok
subprocesses through Bash.

Task short name or full id:

```text
$ARGUMENTS
```
