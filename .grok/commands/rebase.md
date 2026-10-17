---
name: rebase
description: Drive an intent-aware rebase, resolving conflicts by reading both sides' history. Use when the user invokes /rebase or $rebase in Grok Build with a rebase target, or asks to continue or abort a rebase in progress.
---

# Intent-Aware Rebase

Read `.agents/skills/rebase/SKILL.md` completely and follow it with the
substitutions below. Do not read `.grok/ai-workflow-adapter.md`: this is a
branch-integration operation, not AI queue work, so none of that pipeline —
task records, phase leaves, publication — applies.

Grok Build substitutions:

- Where the shared skill says to ask, end the turn with the two intents stated
  and 2–4 numbered resolution directions, and stop. Never continue the rebase
  past an unanswered question.
- Do not `spawn_subagent`. A resolution needs both sides' history in one
  context, and a leaf that returns a summary of what it read cannot supply
  that. Keep the reading targeted instead, per the shared skill's effort tiers.
- Bash here has no interactive terminal, so the shared skill's non-interactive
  git rules are not optional — `git -c core.editor=true` for every
  `rebase --continue`, and `GIT_SEQUENCE_EDITOR` for any todo-list edit. A
  command that opens an editor hangs until it times out.

Request:

```text
$ARGUMENTS
```
