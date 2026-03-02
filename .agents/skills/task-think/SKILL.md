---
name: task-think
description: Orchestrate a multi-phase implementation workflow for this repository with artifact files under .ai/<project-name>/<letter>/ and fresh codex exec child runs per phase. Use when the user wants one prompt to drive context gathering, planning, plan assessment, implementation, build verification, and review iterations while keeping the main session context clean.
---

# Task Pipeline

Run a full implementation workflow with repository artifacts and clear phase boundaries.

## Inputs

Collect:
- task description
- optional project name (if missing, derive a short kebab-case name)
- optional constraints (files, architecture, risk tolerance)
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
    review1.md          # Code review documents (up to 3 iterations)
    review2.md
    review3.md
  b/                    # Follow-up task
    context.md
    plan.md
    review1.md
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
- `.ai/<project-name>/<letter>/review<R>.md` (up to 3 review iterations)
- `.ai/<project-name>/<letter>/logs/phase-*.jsonl` (when running child `codex exec`)

## Phases

The workflow runs these phases sequentially via `codex exec --json` child sessions:

1. **Phase 0: Setup** — Record start time, detect follow-up vs new project, create directories.
2. **Phase 1: Context Gathering** — Read codebase, write `about.md` and `context.md`. (Phase 1F for follow-ups.)
3. **Phase 2: Planning** — Read context, write detailed `plan.md` with numbered steps grouped into phases.
4. **Phase 3: Plan Assessment** — Review and refine the plan for correctness, completeness, code quality, and phase sizing.
5. **Phase 4: Implementation** — One child session per plan phase. Each implements only its assigned phase and updates `plan.md` status.
6. **Phase 5: Build Verification** — Build the project, fix any build errors. Skip if no source code was modified.
7. **Phase 6: Code Review Loop** — Up to 3 review-fix iterations:
   - 6a: Review agent writes `review<R>.md` with verdict (APPROVED or NEEDS_CHANGES).
   - 6b: Fix agent implements review changes and rebuilds.
   - Loop until APPROVED or R > 3.

Use the phase prompt templates in `PROMPTS.md`.

## Execution Mode

Run `codex exec --json` child sessions for each phase. Wait for each to finish before starting the next. After each phase, validate that the expected artifact file exists and has substantive content.

Phases that require elevated reasoning (Planning, Plan Assessment, Code Review) must use `-c model_reasoning_effort="xhigh"`. See example commands in `PROMPTS.md`.

## Verification Rules

- If build or test commands fail due to file locks or access-denied outputs (C1041, LNK1104), stop and ask the user to close locking processes before retrying.
- Never claim completion without:
  - implemented code changes present
  - build attempt results recorded
  - review pass documented with any follow-up fixes

## Completion Criteria

Mark complete only when:
- All plan phases are done
- Build verification is recorded
- Review issues are addressed or explicitly deferred with rationale
- Display total elapsed time since start (format: `Xh Ym Zs`, omitting zero components)
- Remind the user of the project name so they can request follow-up tasks within the same project

## Error Handling

- If any phase fails or gets stuck, report the issue to the user and ask how to proceed.
- If context.md or plan.md is not written properly by a phase, re-run that phase with more specific instructions.
- If build errors persist after the build phase's attempts, report the remaining errors to the user.
- If a review fix phase introduces new build errors that it cannot resolve, report to the user.

## User Invocation

Use plain language with the skill name in the request, for example:

`Use local task-think skill: make sure FileLoadTask::process does not create or read QPixmap on background threads; use QImage with ARGB32_Premultiplied instead.`

For follow-up tasks on an existing project:

`Use local task-think skill: my-project also handle the case where the file is already cached`

If screenshots are relevant, include file paths in the same prompt.
