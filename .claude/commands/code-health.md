---
description: Measure code health, survey an area, or check a change
allowed-tools: Read, Write, Edit, Glob, Grep, Bash, Agent, AskUserQuestion
---

# Code Health

Read `.claude/ai-workflow-adapter.md` and
`.agents/skills/code-health/SKILL.md` completely. Follow the shared skill with
the Claude adapter's delegation substitutions. The arguments choose the mode:
none or `scan` for a scan, `survey [area]` for one area survey, `check [base]`
for a change. Route proposals into the inbox only when the arguments say
`route`; the scheduled run is `scan route`. Codex verifier leaves run in the
`work` Codex home unless the arguments ask for `codex-default`.

Arguments:

```text
$ARGUMENTS
```
