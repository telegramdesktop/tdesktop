# Phase Prompts

Use these templates as Codex subagent messages. Use them as same-session checklists only for Phase 0, intentional current-session build work, Phase 7, or when delegation is unavailable from the start at the current agent depth. Replace every applicable placeholder: `<TASK>`, `<PROJECT>`, `<LETTER>`, `<PREV_LETTER>`, `<BUILD>`, `<N>`, `<OWNED_WRITE_SET>`, `<R>`, `<R-1>`, and `<phase-name>`.

## Orchestration Rules

- Phase 0 runs in the main session.
- When delegation is available, use a fresh subagent for Phase 1, Phase 2, Phase 3, each Phase 4 implementation unit, and each Phase 6 pass. Do not switch those phases to same-session midstream because of a timeout or missing artifact.
- Treat delegation as selected only after the first real phase spawn succeeds; tool presence is insufficient. An immediate depth/capacity/policy rejection before phase work selects same-session checklists and is not a delegated retry.
- Phase 7 runs in the current session on native, non-WSL Windows because it depends on the final local diff and touched-file set. Skip it on WSL and keep files LF/no-BOM there.
- Write each phase prompt to `.ai/<PROJECT>/<LETTER>/logs/phase-<phase-name>.prompt.md` before execution.
- If you delegate a phase, send the prompt file contents as the initial `spawn_agent` message.
- When writing the phase prompt file, append the standard progress file contract and the standard compact reply block below so the subagent knows how to surface progress before the final artifact.
- After each phase completes, write `.ai/<PROJECT>/<LETTER>/logs/phase-<phase-name>.result.md` with exact
  `STATUS:`, `ARTIFACTS:`, `TOUCHED:`, `BLOCKER:`, and `NOTES:` fields.
- Use `fork_turns: "none"` by default. If the phase depends on thread-only context or UI attachments, pass it explicitly or use the smallest positive turn fork needed.
- Use only fields the current `spawn_agent` schema exposes; do not invent role, model, or reasoning arguments. Inherit the parent model/reasoning selection, or match it if the host explicitly supports overrides.
- Give each phase a unique lowercase/digit/underscore task name, store the canonical target returned by `spawn_agent`, and tell the phase it is a leaf that must not delegate.
- Poll with `wait_agent` for at most 60 seconds per call; use elapsed wall-clock windows for stall decisions. Use 30-60 second polls when a phase appears close to landing.
- `wait_agent` is mailbox-wide and may wake for another agent or user input. A timeout is not failure. After every wake, handle new user input if any, inspect the saved target with `list_agents`, and check the expected artifact and matching progress file.
- If the expected artifact exists and shows progress, wait again.
- If the expected artifact is not ready but the progress file mtime moved or its heartbeat counter increased since the previous check, wait again. Prefer mtime checks first and avoid rereading the file unless you need detail. Do not count that as a failed wait.
- If neither the expected artifact nor progress file moved for a full five-minute blocked-check window, use `send_message` while the target is running or `followup_task` when it is idle, asking it to refresh progress, finish the artifact, and return the compact block.
- If a second five-minute window after that follow-up still produces no usable artifact or movement, use `interrupt_agent` if needed, confirm the turn stopped, and retry the disposable phase once with a new unique name. There is no close-agent operation.
- For Phase 1, Phase 2, Phase 3, Phase 4, and Phase 6, if delegated retries still fail, stop and ask the user rather than rerunning the phase locally.
- Never use `codex exec`, background shell child processes, or JSONL child-session logging from this skill.

## Standard Progress File Contract

Append this verbatim to every delegated phase prompt:

```text
You are a leaf phase worker. Do not spawn or delegate to other agents.

Before deep work, create or update the matching progress file in `.ai/<PROJECT>/<LETTER>/logs/`.

Use `phase-<phase-name>.progress.md` as a concise heartbeat with:
- `Heartbeat: <N>` on the first line, incremented on each meaningful update
- Current step
- Files being read or edited
- Concrete findings or decisions so far
- Blocker or next checkpoint

Update it sparingly: preferably at natural milestones, and otherwise only after a longer quiet stretch such as roughly 5-10 minutes.
Keep it tiny so the parent can usually rely on file mtime or the heartbeat counter instead of rereading the whole file.
Do not wait until the final artifact to write progress.
```

## Standard Compact Reply Block

Append this verbatim to every delegated phase prompt:

```text
Before replying in chat, write the required artifact(s) to disk.

Reply in 8 lines or fewer using exactly these keys:
STATUS: <DONE|BLOCKED|APPROVED|NEEDS_CHANGES>
ARTIFACTS: <paths>
TOUCHED: <repo paths or none>
BLOCKER: <none or one short line>

Do not restate the full context, plan, diff, or long reasoning in the chat reply.
```

## Artifact-Based Completion Checks

- Phase 1 is complete only when `about.md` and `context.md` both exist and are non-empty.
- Phase 2 is complete only when `plan.md` exists, contains a `## Status` section, and no unintended source edits were made.
- Phase 3 is complete only when `plan.md` contains both `Phases:` in the Status section and `Assessed: yes`.
- Phase 4 is complete only when the target phase checkbox changed to checked and the touched-file list matches the owned write set, or the blocker explains any mismatch.
- Phase 5 is complete only when the build outcome is known and the build checkbox is updated on success.
- Phase 6a is complete only when `review<R>.md` exists and contains a verdict line.
- Phase 6b is complete only when the requested fixes were applied and the post-fix build outcome is known.
- An implement-specific visual design phase is complete only when `visual.md` cites its available
  design sources (images when supplied; otherwise request facts and repository/baseline anchors),
  records assumptions, and contains desktop anchors, an ordered derivation, tolerances, and
  falsifiable geometry checks. Missing mockups alone never make the phase incomplete.

## Phase 0: Setup

Record the current time now and store it as `$START_TIME`. You will use this at the end to display total elapsed time.

Before running any phase prompts, determine whether this is a new project or a follow-up task.

Follow-up detection:
1. Extract the first word or token from the task description. Call it `FIRST_TOKEN`.
2. Check `.ai/` to see existing project names.
3. Check whether `.ai/<FIRST_TOKEN>/about.md` exists.
4. If the file exists, this is a follow-up task. The project name is `FIRST_TOKEN`. The task description is everything after `FIRST_TOKEN`.
5. If the file does not exist, this is a new project. The full input is the task description.

Do not proceed until you have determined follow-up vs new.

For new projects:
- Using the list of existing projects, pick a unique short name (1-2 lowercase words, hyphen-separated) that does not collide.
- Create `.ai/<PROJECT>/`, `.ai/<PROJECT>/a/`, and `.ai/<PROJECT>/a/logs/`.
- Set `<LETTER>` = `a`.

For follow-up tasks:
- Scan `.ai/<PROJECT>/` for spreadsheet-style task folders (`a/`...`z/`, `aa/`...). Find the latest id.
- The previous task id = that highest id.
- The new task id = next spreadsheet-style id; never reuse an existing artifact directory.
- Create `.ai/<PROJECT>/<LETTER>/` and `.ai/<PROJECT>/<LETTER>/logs/`.

Then proceed to Phase 1. Follow-up tasks do not skip context gathering. They use a modified Phase 1F prompt.

## Phase 1: Context (New Project, letter = `a`)

```text
You are a context-gathering agent for a large C++ codebase (Telegram Desktop).

TASK: <TASK>

YOUR JOB: Read AGENTS.md, inspect the codebase, find all files and code relevant to this task, and write two documents.

Steps:
1. Read AGENTS.md for project conventions and build instructions.
2. Search the codebase for files, classes, functions, and patterns related to the task.
3. Read all potentially relevant files. Be thorough and prefer reading more rather than less.
4. For each relevant file, note:
   - file path
   - relevant line ranges
   - what the code does and how it relates to the task
   - key data structures, function signatures, and patterns used
5. Look for similar existing features that could serve as a reference implementation.
6. Check api.tl if the task involves Telegram API.
7. Check .style files if the task involves UI.
8. Check lang.strings if the task involves user-visible text.

Write two files.

File 1: .ai/<PROJECT>/about.md

This file is not used by any agent in the current task. It exists solely as a starting point for a future follow-up task's context gatherer. No planning, implementation, or review phase should rely on it during the current task.

Write it as if the project is already fully implemented and working. It should contain:
- Project: What this project does (feature description, goals, scope)
- Architecture: High-level architectural decisions, which modules are involved, how they interact
- Key Design Decisions: Important choices made about the approach
- Relevant Codebase Areas: Which parts of the codebase this project touches, key types and APIs involved

Do not include temporal state like "Current State", "Pending Changes", "Not yet implemented", or "TODO". Describe the project as a complete, coherent whole.

File 2: .ai/<PROJECT>/<LETTER>/context.md

This is the primary task-specific implementation context. All downstream phases should be able to work from this file plus the referenced source files. It must be self-contained. Include:
- Task Description: The full task restated clearly
- Relevant Files: Every file path with line ranges and descriptions
- Key Code Patterns: How similar things are done in the codebase, with snippets when useful
- Data Structures: Relevant types, structs, classes
- API Methods: Any TL schema methods involved, copied from api.tl when useful
- UI Styles: Any relevant style definitions
- Localization: Any relevant string keys
- Build Info: Build command and any special notes
- Reference Implementations: Similar features that can serve as templates

Be extremely thorough. Another agent with no prior context will rely on this file.

Do not implement code in this phase.
```

## Phase 1F: Context (Follow-up Task, letter = `b`, `c`, ...)

```text
You are a context-gathering agent for a follow-up task on an existing project in a large C++ codebase (Telegram Desktop).

NEW TASK: <TASK>

YOUR JOB: Read the existing project state, gather any additional context needed, and produce fresh documents for the new task.

Steps:
1. Read AGENTS.md for project conventions and build instructions.
2. Read .ai/<PROJECT>/about.md. This is the project-level blueprint describing everything done so far.
3. Read .ai/<PROJECT>/<PREV_LETTER>/context.md. This is the previous task's gathered context.
4. Understand what has already been implemented by reading the actual source files referenced in about.md and the previous context.
5. Based on the new task description, search the codebase for any additional files, classes, functions, and patterns that are relevant to the new task but not already covered.
6. Read all newly relevant files thoroughly.

Write two files.

File 1: .ai/<PROJECT>/about.md (rewrite)

Rewrite this file instead of appending to it. The new about.md must be a single coherent document that describes the project as if everything, including this new task's changes, is already fully implemented and working.

It should incorporate:
- everything from the old about.md that is still accurate and relevant
- the new task's functionality described as part of the project, not as a pending change
- any changed design decisions or architectural updates from the new task requirements

It should not contain:
- temporal state such as "Current State", "Pending Changes", or "TODO"
- history of how requirements changed between tasks
- references to "the old approach" versus "the new approach"
- task-by-task changelog or timeline
- information that contradicts the new task requirements

File 2: .ai/<PROJECT>/<LETTER>/context.md

This is the primary document for the new task. It must be self-contained and should include:
- Task Description: The new task restated clearly, with enough project background that an implementation agent can understand it without reading any other .ai files
- Relevant Files: Every file path with line ranges relevant to this task
- Key Code Patterns: How similar things are done in the codebase
- Data Structures: Relevant types, structs, classes
- API Methods: Any TL schema methods involved
- UI Styles: Any relevant style definitions
- Localization: Any relevant string keys
- Build Info: Build command and any special notes
- Reference Implementations: Similar features that can serve as templates

Be extremely thorough. Another agent with no prior context should be able to work from this file alone.

Do not implement code in this phase.
```

## Phase 2: Plan

```text
You are a planning agent. You must create a detailed implementation plan.

Read these files:
- .ai/<PROJECT>/<LETTER>/context.md
- Then read the specific source files referenced in context.md to understand the code deeply.

Create a detailed plan in: .ai/<PROJECT>/<LETTER>/plan.md

The plan.md should contain:

## Task
<one-line summary>

## Approach
<high-level description of the implementation approach>

## Files to Modify
<list of files that will be created or modified>

## Files to Create
<list of new files, if any>

## Implementation Steps

Each step must be specific enough that an agent can execute it without ambiguity:
- exact file paths
- exact function names
- what code to add, modify, or remove
- where exactly in the file (after which function, in which class, and so on)

Number every step. Group steps into phases if there are more than about eight steps.

### Phase 1: <name>
1. <specific step>
2. <specific step>

### Phase 2: <name> (if needed)
1. <specific step>

## Build Verification
- build command to run
- expected outcome

## Status
- [ ] Phase 1: <name>
- [ ] Phase 2: <name> (if applicable)
- [ ] Build verification
- [ ] Code review

Do not implement code in this phase.
```

## Phase 3: Plan Assessment

```text
You are a plan assessment agent. Review and refine an implementation plan.

Read these files:
- .ai/<PROJECT>/<LETTER>/context.md
- .ai/<PROJECT>/<LETTER>/plan.md
- Then read the actual source files referenced to verify the plan makes sense.

Assess the plan:

1. Correctness: Are the file paths and line references accurate? Does the plan reference real functions and types?
2. Completeness: Are there missing steps? Edge cases not handled?
3. Code quality: Will the plan minimize code duplication? Does it follow existing codebase patterns from AGENTS.md?
4. Design: Could the approach be improved? Are there better patterns already used in the codebase?
5. Phase sizing: Each phase should be implementable by a single agent in one session. If a phase has more than about 8-10 substantive code changes, split it further.

Update plan.md with your refinements. Keep the same structure but:
- fix any inaccuracies
- add missing steps
- improve the approach if you found better patterns
- ensure phases are properly sized for single-agent execution
- add a line at the top of the Status section: `Phases: <N>`
- add `Assessed: yes` at the bottom of the file

If the plan is small enough for a single agent (roughly 8 steps or fewer), mark it as a single phase.

Do not implement code in this phase.
```

## Phase 4: Implementation

Run one implementation unit per plan phase. Keep implementation phases sequential by default. Parallelize only if their write sets are disjoint and the plan makes that safe.

For each phase in the plan that is not yet marked as done, use this prompt:

```text
You are an implementation agent working on phase <N> of an implementation plan.

Read these files first:
- .ai/<PROJECT>/<LETTER>/context.md
- .ai/<PROJECT>/<LETTER>/plan.md

Then read the source files you will be modifying.

Your owned write set for this phase:
<OWNED_WRITE_SET>

YOUR TASK: Implement only Phase <N> from the plan:
<paste the specific phase steps here>

Rules:
- Follow the plan precisely.
- Follow AGENTS.md coding conventions.
- You are not alone in the codebase. Respect existing changes and do not revert unrelated work.
- Do not modify .ai/ files except the Status section in plan.md and the matching
  `logs/phase-<phase-name>.progress.md` heartbeat required by this prompt.
- When done, update plan.md Status section: change `- [ ] Phase <N>: ...` to `- [x] Phase <N>: ...`
- Do not work on other phases.

When finished, report what you did, which files you changed, and any issues encountered.
```

After each implementation phase:
1. Use a narrow read or search to confirm the status line was updated.
2. Verify the owned write set and touched files with a small diff summary such as `git diff --name-only`.
3. If more phases remain, run the next implementation phase.
4. If all phases are done, proceed to build verification.

## Phase 5: Build Verification

Only run this phase if the task modified project source code.

Prefer running the build in the main session because it is critical-path work. If you delegate it, use a worker subagent and wait immediately for the result.

```text
You are a build verification agent.

Read these files:
- .ai/<PROJECT>/<LETTER>/context.md
- .ai/<PROJECT>/<LETTER>/plan.md

The implementation is complete. Your job is to build the project and fix any build errors that block the planned work.

Steps:
1. Run the resolved Debug build command from context.md (`<BUILD>`) at the repository root. On WSL
   this is the repository Docker entry point; do not run native Windows CMake against that tree.
2. If the build succeeds, update plan.md: change `- [ ] Build verification` to `- [x] Build verification`
3. If the build fails:
   a. Read the error messages carefully
   b. Read the relevant source files
   c. Fix the errors in accordance with the plan and AGENTS.md conventions
   d. Rebuild and repeat until the build passes
   e. Update plan.md status when done

Rules:
- Only fix build errors. Do not refactor or improve code beyond what is needed for a passing build.
- Follow AGENTS.md conventions.
- If build fails with file-locked errors (C1041, LNK1104, "cannot open output file", or similar access-denied lock issues), stop and report the lock. Do not retry.
- You are not alone in the codebase. Respect existing changes and do not revert unrelated work.

When finished, report the build result and which files, if any, you changed.
```

## Phase 6: Code Review Loop

After build verification passes, run up to 3 review-fix iterations. Set iteration counter `R = 1`.

Review loop:

```text
LOOP:
  1. Run review phase 6a with iteration R.
  2. Read review<R>.md verdict:
     - "APPROVED" -> go to FINISH
     - "NEEDS_CHANGES" -> run fix phase 6b
  3. After fix work completes and build passes:
     R = R + 1
     If R > 3 -> go to FINISH
     Otherwise -> go to step 1

FINISH:
  - Update plan.md: change `- [ ] Code review` to `- [x] Code review`
  - Proceed to Phase 7 on native, non-WSL Windows; otherwise proceed to Completion
```

### Step 6a: Code Review

```text
You are a code review agent for Telegram Desktop (C++ / Qt).

Read these files:
- .ai/<PROJECT>/<LETTER>/context.md
- .ai/<PROJECT>/<LETTER>/plan.md
- REVIEW.md
- If R > 1, also read .ai/<PROJECT>/<LETTER>/review<R-1>.md

Then run `git diff` to see the current uncommitted changes for this task.

Read the modified source files in full to understand the changes in context.

Perform a focused code review using these criteria, in order:

1. Correctness and safety: Obvious logic errors, missing null checks at API boundaries, potential crashes, use-after-free, dangling references, race conditions.
2. Dead code: Added or left-behind code that is never used within the scope of the changes.
3. Redundant changes: Diff hunks that have no functional effect.
4. Code duplication: Repeated logic that should be shared.
5. Wrong placement: Code added to a module where it does not logically belong.
6. Function decomposition: Whether an extracted helper would clearly improve readability.
7. Module structure: Only in exceptional cases where a large new chunk of code clearly belongs elsewhere.
8. Style compliance: REVIEW.md rules and AGENTS.md conventions.

Important guidelines:
- Review only the changes made, not pre-existing code outside the scope of the task.
- Be pragmatic. Each suggestion should have a clear, concrete benefit.
- Do not suggest comments, docstrings, or over-engineering.

Write your review to: .ai/<PROJECT>/<LETTER>/review<R>.md

The review document should contain:

## Code Review - Iteration <R>

## Summary
<1-2 sentence overall assessment>

## Verdict: <APPROVED or NEEDS_CHANGES>

If the verdict is NEEDS_CHANGES, continue with:

## Changes Required

### <Issue 1 title>
- Category: <dead code | duplication | wrong placement | function decomposition | module structure | style | correctness>
- File(s): <file paths>
- Problem: <clear description>
- Fix: <specific description of what to change>

Keep the list focused. Prioritize the most impactful issues.

When finished, report your verdict clearly as: APPROVED or NEEDS_CHANGES.
```

### Step 6b: Review Fix

```text
You are a review fix agent. You implement improvements identified during code review.

Read these files:
- .ai/<PROJECT>/<LETTER>/context.md
- .ai/<PROJECT>/<LETTER>/plan.md
- .ai/<PROJECT>/<LETTER>/review<R>.md

Then read the source files mentioned in the review.

YOUR TASK: Implement all changes listed in review<R>.md.

Rules:
- Implement exactly the review changes, nothing more.
- Follow AGENTS.md coding conventions.
- You are not alone in the codebase. Respect existing changes and do not revert unrelated work.
- Do not modify .ai/ files except where the review process explicitly requires it.

After all changes are made:
1. Run the resolved Debug build command from context.md (`<BUILD>`) at the repository root.
2. If the build fails, fix build errors and rebuild until it passes.
3. If build fails with file-locked errors (C1041, LNK1104, "cannot open output file", or similar access-denied lock issues), stop and report the lock. Do not retry.

When finished, report what changes were made and which files you touched.
```

## Phase 7: Native-Windows Text Normalization

Run this phase only in a native, non-WSL Windows checkout and only after the review loop has
finished. Keep WSL/Linux text LF/no-BOM.

Use the current task's result logs as the source of truth for what Codex touched. Do not sweep the whole repo and do not rewrite unrelated files from a dirty worktree.

```text
You are performing the final native-Windows-only text normalization phase for task-think.

Read these files:
- .ai/<PROJECT>/<LETTER>/plan.md
- .ai/<PROJECT>/<LETTER>/logs/phase-4*.result.md
- .ai/<PROJECT>/<LETTER>/logs/phase-5*.result.md
- .ai/<PROJECT>/<LETTER>/logs/phase-6*.result.md

Your job:
- Collect the union of repo file paths listed in the exact `TOUCHED:` fields in those result logs.
- Keep only files inside the repository that currently exist and are textual project files: source, headers, build/config files, localization files, style files, and similar text assets.
- Exclude `.ai/`, `out/`, binary files, and unrelated user files that were not touched by Codex in this task.
- Rewrite each kept file so all line endings are CRLF.
- If a kept file is UTF-8 or ASCII text, write it back as UTF-8 without BOM. Never add a UTF-8 BOM to source/config/project text files.
- Preserve file content otherwise. Preserve whether the file ended with a trailing newline.

Rules:
- Run this phase in the current session on native, non-WSL Windows.
- Do not modify files outside the touched-file set for the current task.
- Do not rewrite binary files.
- When scripting this phase, do not use writer APIs or defaults that emit UTF-8 with BOM.
- If a file cannot be normalized safely, record it as a failure instead of silently skipping it.

When finished:
1. Write `.ai/<PROJECT>/<LETTER>/logs/phase-7-line-endings.result.md`
2. Include:
   - whether the phase completed
   - which files were normalized
   - which files were skipped and why
   - whether any UTF-8 BOMs were removed or verified absent
   - any failures that need to be mentioned in the final summary
```

## Completion

When all phases, including build verification, code review, and Windows line ending normalization when applicable, are done:
1. Read the final `plan.md` and report the summary to the user.
2. Show which files were modified or created.
3. Note any issues encountered during implementation.
4. Summarize the code review iterations: how many rounds, what was found and fixed, or whether it was approved on the first pass.
5. On native, non-WSL Windows, mention the text-normalization result briefly: which project files were normalized, whether any BOMs were removed, or whether nothing needed changes.
6. Calculate and display the total elapsed time since `$START_TIME` (format as `Xh Ym Zs`, omitting zero components).
7. Remind the user of the project name so they can request follow-up tasks within the same project.

## Error Handling

- If any phase fails or gets stuck, follow the timeout and retry rules above. Do not close an agent solely because the final artifact is missing while its progress file is still advancing. For Phase 1, Phase 2, Phase 3, Phase 4, and Phase 6, do not rerun locally after delegated retries fail; ask the user instead.
- If `context.md` or `plan.md` is not written properly by a phase, rerun that phase in a fresh subagent with more specific instructions.
- If build errors persist after the build phase's attempts, report the remaining errors to the user.
- If a review-fix phase introduces new build errors that it cannot resolve, report to the user.

## Prompt Delivery And Logs

For each phase:
1. Write the full prompt to `.ai/<PROJECT>/<LETTER>/logs/phase-<phase-name>.prompt.md`
2. Delegate by sending that prompt text to a fresh subagent, or use it as a same-session checklist only for the designated main-session phases or when delegation was unavailable from the start
3. For delegated phases, expect a matching `.ai/<PROJECT>/<LETTER>/logs/phase-<phase-name>.progress.md` heartbeat while work is in flight
4. Save `.ai/<PROJECT>/<LETTER>/logs/phase-<phase-name>.result.md` with `STATUS:`, `ARTIFACTS:`,
   `TOUCHED:`, `BLOCKER:`, and `NOTES:` fields.

For review iterations, include the iteration in the file name, for example:
- `phase-6a-review-1.prompt.md`
- `phase-6a-review-1.result.md`
- `phase-6b-fix-1.prompt.md`
- `phase-6b-fix-1.result.md`

## Subagent Pattern

Use this pattern conceptually for delegated phases:

1. Write the phase prompt file.
2. Spawn a fresh leaf subagent with a unique tool-valid task name and `fork_turns: "none"` unless a minimal recent-turn fork is required.
3. Require the agent to create the matching progress file early and refresh it sparingly: at natural milestones when possible, otherwise only after a longer quiet stretch such as roughly 5-10 minutes.
4. Poll for at most 60 seconds at a time. After any mailbox wake, inspect the saved target with `list_agents`; use elapsed five-minute windows rather than poll count for stall checks.
5. Prefer filesystem mtime checks on the progress file first. If its mtime moved or the heartbeat counter increased, keep waiting; do not treat that as a stall.
6. After a full blocked-check window with no movement, use `send_message` for a running target or `followup_task` for an idle one. After a second unchanged window, interrupt if needed and retry the disposable phase once with a unique task name.
7. Validate the expected artifact or code changes with small shell summaries and the completion checks above.
8. Write the result log from the validated outcome and the compact reply block.

Do not replace this pattern with shell-launched `codex exec`.
