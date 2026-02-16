---
name: task-think
description: Orchestrate a multi-phase implementation workflow for this repository with artifact files under .ai/<project-name>/<letter>/ and optional fresh codex exec child runs per phase. Use when the user wants one prompt to drive context gathering, planning, implementation, verification, and review iterations while keeping the main session context clean.
---

# Task Pipeline

Run a full implementation workflow with repository artifacts and clear phase boundaries.

## Inputs

Collect:
- task description
- optional project name (if missing, derive a short kebab-case name)
- optional constraints (files, architecture, deadlines, risk tolerance)
- optional screenshot paths

If screenshots are attached in UI but not present as files, write a brief textual summary in `.ai/<project-name>/about.md` so child runs can consume the requirements.

## Overview

The workflow is organized around **projects**. Each project lives in `.ai/<project-name>/` and can contain multiple sequential **tasks** (labeled `a`, `b`, `c`, ... `z`).

Project structure:
```
.ai/<project-name>/
  about.md              # Single source of truth for the entire project
  a/                    # First task
    context.md          # Gathered codebase context for this task
    plan.md             # Implementation plan
    review.md           # Code review document
  b/                    # Follow-up task
    context.md
    plan.md
    review.md
  c/                    # Another follow-up task
    ...
```

- `about.md` is the project-level blueprint — a single comprehensive document describing what this project does and how it works, written as if everything is already fully implemented. It contains no temporal state ("current state", "pending changes", "not yet implemented"). It is **rewritten** (not appended to) each time a new task starts, incorporating the new task's changes as if they were always part of the design.
- Each task folder (`a/`, `b/`, ...) contains self-contained files for that task. The task's `context.md` carries all task-specific information: what specifically needs to change, the delta from the current codebase, gathered file references and code patterns. Planning, implementation, and review agents only read the current task's folder.

## Artifacts

Create and maintain:
- `.ai/<project-name>/about.md`
- `.ai/<project-name>/<letter>/context.md`
- `.ai/<project-name>/<letter>/plan.md`
- `.ai/<project-name>/<letter>/implementation.md`
- `.ai/<project-name>/<letter>/review.md`
- `.ai/<project-name>/<letter>/logs/phase-*.jsonl` (when running child `codex exec`)

## Execution Mode

Run `codex exec --json` child sessions for each phase.

## Fresh-Run Mode Procedure

1. Detect follow-up vs new project (check if first token of task description matches an existing project name with `about.md`).
2. For new projects: pick unique short name, create `.ai/<project-name>/` and `.ai/<project-name>/a/`.
3. For follow-up tasks: find latest task letter, create `.ai/<project-name>/<next-letter>/`.
4. Run child phase sessions sequentially, waiting for each to finish.
5. After each phase, validate artifact file exists and has substantive content.
6. Summarize status in the parent session after each phase.
7. Stop immediately on blocking errors and report exact blocker.

Use the phase prompt templates in `PROMPTS.md`.

## Verification Rules

- If build or test commands fail due to file locks or access-denied outputs, stop and ask the user to close locking processes before retrying.
- Never claim completion without:
  - implemented code changes present
  - build/test attempt results recorded
  - review pass documented with any follow-up fixes

## Completion Criteria

Mark complete only when:
- plan phases are done
- verification results are recorded
- review issues are addressed or explicitly deferred with rationale
- Remind the user of the project name so they can request follow-up tasks within the same project.

## User Invocation

Use plain language with the skill name in the request, for example:

`Use local task-think skill: make sure FileLoadTask::process does not create or read QPixmap on background threads; use QImage with ARGB32_Premultiplied instead.`

For follow-up tasks on an existing project:

`Use local task-think skill: my-project also handle the case where the file is already cached`

If screenshots are relevant, include file paths in the same prompt.
