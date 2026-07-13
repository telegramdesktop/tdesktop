---
name: task-think
description: Orchestrate a multi-phase Telegram Desktop implementation workflow with persistent per-project task artifacts under .ai. Use when Codex should drive context gathering, planning, plan assessment, implementation, Debug build verification, review, and native-Windows text normalization through bounded phase handoffs while keeping the parent task lean. Uses current spawn_agent, wait_agent, send_message, followup_task, and interrupt_agent semantics.
---

# Task Pipeline

Run a full implementation workflow with repository artifacts and clear phase boundaries.

## Inputs

Collect:
- task description
- optional project name (if missing, derive a short kebab-case name)
- optional constraints (files, architecture, risk tolerance)
- optional screenshot paths

If screenshots are attached in UI but not present as files, write a brief textual summary into the task artifacts before spawning fresh subagents so later phases can read the requirements without inheriting the whole parent thread.

## Overview

The workflow is organized around projects. Each project lives in `.ai/<project-name>/` and can contain sequential spreadsheet-style task ids (`a`...`z`, `aa`...).

Project structure:
```text
.ai/<project-name>/
  about.md              # Single source of truth for the entire project
  a/                    # First task
    context.md          # Gathered codebase context for this task
    plan.md             # Implementation plan
    review1.md          # Code review documents (up to 3 iterations)
    review2.md
    review3.md
    logs/
      phase-*.prompt.md
      phase-*.progress.md
      phase-*.result.md
  b/                    # Follow-up task
    context.md
    plan.md
    review1.md
    logs/
      ...
  c/                    # Another follow-up task
    ...
```

- `about.md` is the project-level blueprint: a single comprehensive document describing what this project does and how it works, written as if everything is already fully implemented. It contains no temporal state ("current state", "pending changes", "not yet implemented"). It is rewritten, not appended to, each time a new task starts, incorporating the new task's changes as if they were always part of the design.
- Each task folder (`a/`, `b/`, ...) contains self-contained files for that task. The task's `context.md` carries all task-specific information: what specifically needs to change, the delta from the current codebase, gathered file references, and code patterns. Planning, implementation, and review phases should rely on the current task folder.

## Artifacts

Create and maintain:
- `.ai/<project-name>/about.md`
- `.ai/<project-name>/<letter>/context.md`
- `.ai/<project-name>/<letter>/plan.md`
- `.ai/<project-name>/<letter>/review<R>.md` (up to 3 review iterations)
- `.ai/<project-name>/<letter>/logs/phase-<phase-name>.prompt.md`
- `.ai/<project-name>/<letter>/logs/phase-<phase-name>.progress.md` for delegated phases
- `.ai/<project-name>/<letter>/logs/phase-<phase-name>.result.md`

Each `phase-<phase-name>.result.md` uses exact `STATUS:`, `ARTIFACTS:`, `TOUCHED:`, `BLOCKER:`, and
`NOTES:` fields. Each delegated `phase-<phase-name>.progress.md` is a heartbeat: a tiny monotonic counter
plus current step, files being read or edited, concrete findings, and next checkpoint. It lets the
parent distinguish active research from a stuck subagent without rereading large context.

## Phases

Run these phases sequentially:

1. Phase 0: Setup - Record start time, detect follow-up vs new project, create directories.
2. Phase 1: Context Gathering - Read codebase, write `about.md` and `context.md`. Use Phase 1F for follow-up tasks.
3. Phase 2: Planning - Read context, write detailed `plan.md` with numbered steps grouped into phases.
4. Phase 3: Plan Assessment - Review and refine the plan for correctness, completeness, code quality, and phase sizing.
5. Phase 4: Implementation - Execute one implementation unit per plan phase.
6. Phase 5: Build Verification - Build the project, fix any build errors. Skip if no source code was modified.
7. Phase 6: Code Review Loop - Run review and fix iterations until approved or the iteration limit is reached.
8. Phase 7: Windows Text Normalization - On native, non-WSL Windows only, after review passes and before the final summary, normalize LF to CRLF for the text source/config files Codex edited in this task and ensure rewritten UTF-8 project files are saved without BOM. Keep WSL/Linux files LF/no-BOM.

Use the phase prompt templates in `PROMPTS.md`.

## Execution Mode

Use Codex subagents as the primary orchestration mechanism when they are available at the current
agent depth.

- When delegation is available, Phase 1, Phase 2, Phase 3, each Phase 4 implementation unit, and each Phase 6 review or review-fix pass must run in fresh subagents. Do not rerun those phases in the main session midstream just because a wait timed out or an artifact is missing.
- Run Phase 7 in the main session on native, non-WSL Windows because it depends on the final local file state and exact touched-file set. Skip it on WSL and preserve LF/no-BOM there.
- When any same-session helper rewrites native-Windows project text files, preserve CRLF and write UTF-8 without BOM. Avoid writer APIs or defaults that silently inject a UTF-8 BOM.
- The main session may read `context.md` once after Phase 1 and `plan.md` once after Phase 3. After that, prefer narrow shell checks, file existence checks, and status-line reads instead of rereading full documents or diffs.
- Use only fields exposed by the current `spawn_agent` schema. Some hosts do not expose worker/explorer roles or per-spawn model settings; do not invent them.
- Use `fork_turns: "none"` by default. Pass the phase prompt and explicit file paths instead of the whole thread. Use the smallest positive turn count only for genuinely thread-only context or attachments.
- Inherit the parent task's model and reasoning selection. If a host exposes overrides, match the parent rather than downshifting. Custom agent files use `model_reasoning_effort`.
- Give every spawned phase a unique lowercase/digit/underscore `task_name`, save its returned canonical target, and explicitly make the phase worker a leaf that must not delegate further.
- Tool presence alone does not prove delegation is allowed. Choose delegated mode only after the first real phase spawn succeeds; an immediate depth/capacity/policy rejection before phase work selects same-session checklists. Do not block or launch `codex exec` merely because the default nesting depth is one.
- Write the exact phase prompt to the matching `logs/phase-<phase-name>.prompt.md` file before you delegate. Use the same prompt file as a checklist if you later need to fall back to same-session execution.
- For delegated phases, require an early `logs/phase-<phase-name>.progress.md` heartbeat before deep work. The subagent should create or update it early, keep it tiny, and refresh it sparingly: preferably at natural milestones, and otherwise only after a longer quiet stretch such as roughly 5-10 minutes.
- In every delegated prompt, require a compact final reply with only status, artifact paths, touched files, and blocker or `none`. Detailed reasoning belongs in `.ai/` artifacts, not in the chat reply.
- After a subagent finishes, verify that the expected artifacts or code changes exist, then write `logs/phase-<phase-name>.result.md` with the canonical fields.
- Poll delegated work with `wait_agent` for at most 60 seconds per call. Use elapsed wall-clock windows, not the number of poll timeouts, for stall decisions. When a phase looks close to completion, use 30-60 second polls.
- A timeout is not a failure; it only means no final status arrived yet. Do not treat short waits as stall detection for research-heavy phases.
- `wait_agent` is mailbox-wide and may wake for another agent or steered user input. After every wake, handle new user input if any, inspect the saved target with `list_agents`, then validate the expected artifact and progress-file mtime. Prefer mtime checks first; only reread the progress file when you need detail.
- If the progress file mtime moved or its heartbeat counter increased since the previous check, treat that as active progress and wait again.
- If no usable final artifact exists yet but the progress file is appearing or advancing, keep the same subagent alive. Progress-file movement does not count toward the retry limit.
- If no usable final artifact exists and neither it nor the progress file has moved for a full five-minute blocked-check window, use `send_message` when the target is still running, or `followup_task` when it is idle, asking it to refresh progress, finish the artifact, and return the compact block.
- If there is still no meaningful movement for a second five-minute window after that follow-up, use `interrupt_agent` if it is running, confirm the turn stopped, and retry the disposable phase once with a new unique task name. There is no `close_agent` operation.
- Use `wait_agent` only when the next step is blocked on the result. While the delegated phase runs, do small non-overlapping local tasks such as validating directory structure or preparing the next prompt file.
- Build verification is critical-path work. Prefer running the build in the main session, and only delegate a bounded build-fix phase when there is a concrete reason.
- If subagents are unavailable in the current environment, current depth, or policy from the start, run the phase in the current session using the same prompt files. Otherwise, do not switch a pre-build phase to same-session midstream. Never fall back to shell-spawned `codex exec` child processes from this skill.

## Verification Rules

- If build or test commands fail due to file locks or access-denied outputs (C1041, LNK1104), stop and ask the user to close locking processes before retrying.
- Treat a delegated phase as complete only when the required artifact or status update exists on disk and matches the phase goals; do not rely on the chat reply alone.
- Never claim completion without:
  - implemented code changes present
  - build attempt results recorded
  - review pass documented with any follow-up fixes
  - on native, non-WSL Windows, if the task edited project source/config text files, a CRLF / no-BOM normalization pass recorded after review

## Completion Criteria

Mark complete only when:
- All plan phases are done
- Build verification is recorded
- Review issues are addressed or explicitly deferred with rationale
- On native, non-WSL Windows, Codex-edited project source/config text files have been normalized to CRLF, any UTF-8 rewrites were saved without BOM, and the result is logged
- Display total elapsed time since start (format: `Xh Ym Zs`, omitting zero components)
- Remind the user of the project name so they can request follow-up tasks within the same project

## Error Handling

- If any phase fails, times out, or gets stuck, follow the retry ladder from Execution Mode. Do not close an agent solely because the final artifact is missing while its progress file is still moving. After two delegated attempts remain blocked with no meaningful progress, report the issue to the user. Do not absorb the phase into the main session before build unless delegation was unavailable from the start.
- If `context.md` or `plan.md` is not written properly by a phase, rerun that phase in a fresh subagent with more specific instructions. Do not repair it locally before build unless delegation was unavailable from the start.
- If build errors persist after the build phase's attempts, report the remaining errors to the user.
- If a review-fix phase introduces new build errors that it cannot resolve, report to the user.
- If Phase 7 cannot safely normalize a touched file on native, non-WSL Windows or remove an introduced UTF-8 BOM from a touched project text file, record the failure in the result log and report it in the final summary instead of silently skipping it.

## User Invocation

Use plain language with the skill name in the request, for example:

`Use local task-think skill with subagents: make sure FileLoadTask::process does not create or read QPixmap on background threads; use QImage with ARGB32_Premultiplied instead.`

For follow-up tasks on an existing project:

`Use local task-think skill with subagents: my-project also handle the case where the file is already cached`

If screenshots are relevant, include file paths in the same prompt when possible.
