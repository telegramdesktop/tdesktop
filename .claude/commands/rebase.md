---
description: Drive an intent-aware rebase, resolving conflicts by reading both sides' history
argument-hint: onto <ref> | <branch> onto <ref> | tail of <branch> on top of <ref> | continue | abort
allowed-tools: Bash, Read, Edit, Write, Grep, Glob, AskUserQuestion, TodoWrite
---

# Intent-Aware Rebase

Read `.agents/skills/rebase/SKILL.md` completely and follow it with the
substitutions below. Do not read `.claude/ai-workflow-adapter.md`: this is a
branch-integration operation, not AI queue work, so none of that pipeline —
task records, phase leaves, publication — applies.

Claude Code substitutions:

- Where the shared skill says to ask, use `AskUserQuestion` with the two
  intents in the question and 2–4 concrete resolution directions as options.
  Let it block: never continue the rebase past an unanswered question.
- Track the replay list with `TodoWrite` when it is more than a handful of
  commits, one item per commit, so a long rebase stays legible.
- The `Bash` tool has no interactive terminal, so the shared skill's
  non-interactive git rules are not optional here — `git -c core.editor=true`
  for every `rebase --continue`, and `GIT_SEQUENCE_EDITOR` for any todo-list
  edit. A command that opens an editor hangs until it times out.
- No delegation. `Agent` is deliberately absent from this command's tools: a
  resolution needs both sides' history in one context, and a subagent that
  returns a summary of what it read cannot supply that. Keep the reading
  targeted instead, per the shared skill's effort tiers.
- Keep stage dumps and the running report in the session scratchpad, never in
  the repository.

Request:

```text
$ARGUMENTS
```
