---
description: Implement one existing AI task by short name or full id
allowed-tools: Read, Write, Edit, Glob, Grep, Bash, Agent, AskUserQuestion, TodoWrite
---

# Perform One AI Task

Read `.claude/ai-workflow-adapter.md` and
`.agents/skills/perform-task/SKILL.md` completely. Follow the shared skill with
the Claude adapter's delegation and text-handling substitutions. Resolve, start
or resume, implement, verify, and publish only the named task. Do not
continue with other queue work, route discoveries, or consolidate pending tasks
afterward; those post-result phases belong to the `continue` scheduler.
If assessment or review convergence proves the task intrinsically broad,
publish the shared `split-required` result with retained source state and stop;
do not create its replacement tasks in this command.

Apply the shared pipeline's conditional `[ai] ` commit-subject rule exactly;
decide it per commit, require it only when all changes and the purpose are
exclusively AI workflow or infrastructure, and forbid it for product or mixed
commits.

Apply the shared source-lineage gate before Phase 1 and pass explicit source
task requirements to `start` or `retry`. A pre-Phase-1 mismatch returns the
clean routing stop to the caller without integrating history or publishing a
Block. A mismatch first established after Phase 1 publishes the shared clean
task-local Block; it never becomes a cherry-pick, rebase, merge, backport, or
branch-sync operation inside this command.

Delegate phases with synchronous foreground Agent calls per the adapter; leaf
phase agents receive self-contained prompts and are not told to read the
adapter. Use the Agent tool for delegation; do not start Claude subprocesses
through Bash.

Task short name or full id:

```text
$ARGUMENTS
```
