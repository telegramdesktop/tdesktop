# Phase Prompts

Use these templates for `codex exec --json` child runs. Replace `<TASK>`, `<PROJECT>`, `<LETTER>`, and `<REPO_ROOT>`.

## Phase 0: Setup

**Record the current time now** and store it as `$START_TIME`. You will use this at the end to display total elapsed time.

Before running any phase prompts, the orchestrator must determine whether this is a new project or a follow-up task.

**Follow-up detection (MANDATORY — do this BEFORE anything else):**
1. Extract the first word/token from the task description. Call it `FIRST_TOKEN`.
2. Run these two checks IN PARALLEL:
   - `ls .ai/` — to see all existing project names
   - `ls .ai/<FIRST_TOKEN>/about.md` — to check if this specific project exists
3. If check 2 **succeeds** (the file exists): this is a **follow-up task**. The project name is `FIRST_TOKEN`. The task description is everything after `FIRST_TOKEN`.
4. If check 2 **fails** (file not found): this is a **new project**. The full input is the task description.

**Do NOT proceed until you have run these checks and determined follow-up vs new.**

**For new projects:**
- Using the list from check 1, pick a unique short name (1-2 lowercase words, hyphen-separated) that doesn't collide with existing projects.
- Create `.ai/<PROJECT>/` and `.ai/<PROJECT>/a/` and `logs/`.
- Set `<LETTER>` = `a`.

**For follow-up tasks:**
- Scan `.ai/<PROJECT>/` for existing task folders (`a/`, `b/`, ...). Find the latest one (highest letter).
- The previous task letter = that highest letter.
- The new task letter = next letter in sequence.
- Create `.ai/<PROJECT>/<LETTER>/` and `logs/`.

Then proceed to Phase 1. Follow-up tasks do NOT skip context gathering — they go through a modified version of it.

## Phase 1: Context (New Project, letter = `a`)

```text
You are a context-gathering agent for a large C++ codebase (Telegram Desktop).

TASK: <TASK>

YOUR JOB: Read AGENTS.md, inspect the codebase, find ALL files and code relevant to this task, and write two documents.

Steps:
1. Read AGENTS.md for project conventions and build instructions.
2. Search the codebase for files, classes, functions, and patterns related to the task.
3. Read all potentially relevant files. Be thorough - read more rather than less.
4. For each relevant file, note:
   - File path
   - Relevant line ranges
   - What the code does and how it relates to the task
   - Key data structures, function signatures, patterns used
5. Look for similar existing features that could serve as a reference implementation.
6. Check api.tl if the task involves Telegram API.
7. Check .style files if the task involves UI.
8. Check lang.strings if the task involves user-visible text.

Write TWO files:

### File 1: .ai/<PROJECT>/about.md

NOTE: This file is NOT used by any agent in the current task. It exists solely as a starting point for a FUTURE follow-up task's context gatherer. No planning, implementation, or review agent will ever read it. Only the context-gathering agent of the next follow-up task reads about.md (together with the latest context.md) to produce a fresh context.md for that next task.

Write it as if the project is already fully implemented and working. It should contain:
- **Project**: What this project does (feature description, goals, scope)
- **Architecture**: High-level architectural decisions, which modules are involved, how they interact
- **Key Design Decisions**: Important choices made about the approach
- **Relevant Codebase Areas**: Which parts of the codebase this project touches, key types and APIs involved

Do NOT include temporal state like "Current State", "Pending Changes", "Not yet implemented", "TODO", or any other framing that distinguishes between "done" and "not done". Describe the project as a complete, coherent whole — as if everything is already working. This is a project overview, not a status tracker. Task-specific work belongs exclusively in context.md.

### File 2: .ai/<PROJECT>/a/context.md

This is the task-specific implementation context. This is the PRIMARY document — all downstream agents (planning, implementation, review) will read ONLY this file. It must be completely self-contained. It should contain:
- **Task Description**: The full task restated clearly
- **Relevant Files**: Every file path with line ranges and descriptions of what's there
- **Key Code Patterns**: How similar things are done in the codebase (with code snippets)
- **Data Structures**: Relevant types, structs, classes
- **API Methods**: Any TL schema methods involved (copied from api.tl)
- **UI Styles**: Any relevant style definitions
- **Localization**: Any relevant string keys
- **Build Info**: Build command and any special notes
- **Reference Implementations**: Similar features that can serve as templates

Be extremely thorough. Another agent with NO prior context will read this file and must be able to understand everything needed to implement the task.

Do not implement code in this phase.
```

## Phase 1F: Context (Follow-up Task, letter = `b`, `c`, ...)

```text
You are a context-gathering agent for a follow-up task on an existing project in a large C++ codebase (Telegram Desktop).

NEW TASK: <TASK>

YOUR JOB: Read the existing project state, gather any additional context needed, and produce fresh documents for the new task.

Steps:
1. Read AGENTS.md for project conventions and build instructions.
2. Read .ai/<PROJECT>/about.md — this is the project-level blueprint describing everything done so far.
3. Read .ai/<PROJECT>/<PREV_LETTER>/context.md — this is the previous task's gathered context.
4. Understand what has already been implemented by reading the actual source files referenced in about.md and the previous context.
5. Based on the NEW TASK description, search the codebase for any ADDITIONAL files, classes, functions, and patterns that are relevant to the new task but not already covered.
6. Read all newly relevant files thoroughly.

Write TWO files:

### File 1: .ai/<PROJECT>/about.md (REWRITE)

NOTE: This file is NOT used by any agent in the current task. It exists solely as a starting point for a FUTURE follow-up task's context gatherer. No planning, implementation, or review agent will ever read it. You are rewriting it now so that the next follow-up has an accurate project overview to start from.

REWRITE this file (not append). The new about.md must be a single coherent document that describes the project as if everything — including this new task's changes — is already fully implemented and working.

It should incorporate:
- Everything from the old about.md that is still accurate and relevant
- The new task's functionality described as part of the project (not as "changes to make")
- Any changed design decisions or architectural updates from the new task requirements

It should NOT contain:
- Any temporal state: "Current State", "Pending Changes", "TODO", "Not yet implemented"
- History of how requirements changed between tasks
- References to "the old approach" vs "the new approach"
- Task-by-task changelog or timeline
- Any distinction between "what was done before" and "what this task adds"
- Information that contradicts the new task requirements (if the new task changes direction, the about.md should reflect the NEW direction as if it was always the plan)

### File 2: .ai/<PROJECT>/<LETTER>/context.md

This is the PRIMARY document — all downstream agents (planning, implementation, review) will read ONLY this file. It must be completely self-contained. about.md will NOT be available to them.

It should contain:
- **Task Description**: The new task restated clearly, with enough project background (from about.md and previous context.md) that an implementation agent can understand it without reading any other .ai/ files
- **Relevant Files**: Every file path with line ranges relevant to THIS task (including files modified by previous tasks and any newly relevant files)
- **Key Code Patterns**: How similar things are done in the codebase
- **Data Structures**: Relevant types, structs, classes
- **API Methods**: Any TL schema methods involved
- **UI Styles**: Any relevant style definitions
- **Localization**: Any relevant string keys
- **Build Info**: Build command and any special notes
- **Reference Implementations**: Similar features that can serve as templates

Be extremely thorough. Another agent with NO prior context will read ONLY this file and must be able to understand everything needed to implement the new task. Do NOT assume the reader has seen about.md or any previous task files. The context.md is the single source of truth for all downstream agents — it must include all relevant project background, not just the delta.

Do not implement code in this phase.
```

## Phase 2: Plan

```text
You are a planning agent. You must create a detailed implementation plan.

Read these files:
- .ai/<PROJECT>/<LETTER>/context.md - Contains all gathered context for this task
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
- Exact file paths
- Exact function names
- What code to add/modify/remove
- Where exactly in the file (after which function, in which class, etc.)

Number every step. Group steps into phases if there are more than ~8 steps.

### Phase 1: <name>
1. <specific step>
2. <specific step>
...

### Phase 2: <name> (if needed)
...

## Build Verification
- Build command to run
- Expected outcome

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

1. **Correctness**: Are the file paths and line references accurate? Does the plan reference real functions and types?
2. **Completeness**: Are there missing steps? Edge cases not handled?
3. **Code quality**: Will the plan minimize code duplication? Does it follow existing codebase patterns from AGENTS.md?
4. **Design**: Could the approach be improved? Are there better patterns already used in the codebase?
5. **Phase sizing**: Each phase should be implementable by a single agent in one session. If a phase has more than ~8-10 substantive code changes, split it further.

Update plan.md with your refinements. Keep the same structure but:
- Fix any inaccuracies
- Add missing steps
- Improve the approach if you found better patterns
- Ensure phases are properly sized for single-agent execution
- Add a line at the top of the Status section: `Phases: <N>` indicating how many implementation phases there are
- Add `Assessed: yes` at the bottom of the file

If the plan is small enough for a single agent (roughly <=8 steps), mark it as a single phase.

Do not implement code in this phase.
```

## Phase 4: Implementation

For each phase in the plan that is not yet marked as done, run a separate child session:

```text
You are an implementation agent working on phase <N> of an implementation plan.

Read these files first:
- .ai/<PROJECT>/<LETTER>/context.md - Full codebase context
- .ai/<PROJECT>/<LETTER>/plan.md - Implementation plan

Then read the source files you'll be modifying.

YOUR TASK: Implement ONLY Phase <N> from the plan:
<paste the specific phase steps here>

Rules:
- Follow the plan precisely
- Follow AGENTS.md coding conventions (no comments except complex algorithms, use auto, empty line before closing brace, etc.)
- Do NOT modify .ai/ files except to update the Status section in plan.md
- When done, update plan.md Status section: change `- [ ] Phase <N>: ...` to `- [x] Phase <N>: ...`
- Do NOT work on other phases

When finished, report what you did and any issues encountered.
```

After each implementation agent returns:
1. Read `plan.md` to check the status was updated.
2. If more phases remain, run the next implementation child session.
3. If all phases are done, proceed to build verification.

## Phase 5: Build Verification

Only run this phase if the task involved modifying project source code (not just docs or config).

```text
You are a build verification agent.

Read these files:
- .ai/<PROJECT>/<LETTER>/context.md
- .ai/<PROJECT>/<LETTER>/plan.md

The implementation is complete. Your job is to build the project and fix any build errors.

Steps:
1. Run (from repository root): cmake --build ./out --config Debug --target Telegram
2. If the build succeeds, update plan.md: change `- [ ] Build verification` to `- [x] Build verification`
3. If the build fails:
   a. Read the error messages carefully
   b. Read the relevant source files
   c. Fix the errors in accordance with the plan and AGENTS.md conventions
   d. Rebuild and repeat until the build passes
   e. Update plan.md status when done

Rules:
- Only fix build errors, do not refactor or improve code
- Follow AGENTS.md conventions
- If build fails with file-locked errors (C1041, LNK1104), STOP and report - do not retry

When finished, report the build result.
```

## Phase 6: Code Review Loop

After build verification passes, run up to 3 review-fix iterations to improve code quality. Set iteration counter `R = 1`.

### Review Loop

```
LOOP:
  1. Run review agent (Step 6a) with iteration R
  2. Read review<R>.md verdict:
     - "APPROVED" → go to FINISH
     - Has improvement suggestions → run fix agent (Step 6b)
  3. After fix agent completes and build passes:
     R = R + 1
     If R > 3 → go to FINISH (stop iterating, accept current state)
     Otherwise → go to step 1

FINISH:
  - Update plan.md: change `- [ ] Code review` to `- [x] Code review`
  - Proceed to Completion
```

### Step 6a: Code Review Agent

```text
You are a code review agent for Telegram Desktop (C++ / Qt).

Read these files:
- .ai/<PROJECT>/<LETTER>/context.md - Codebase context
- .ai/<PROJECT>/<LETTER>/plan.md - Implementation plan
- REVIEW.md - Style and formatting rules to enforce
<if R > 1, also read:>
- .ai/<PROJECT>/<LETTER>/review<R-1>.md - Previous review (to see what was already addressed)

Then run `git diff` to see all uncommitted changes made by the implementation. Implementation agents do not commit, so `git diff` shows exactly the current feature's changes.

Then read the modified source files in full to understand changes in context.

Perform a thorough code review.

REVIEW CRITERIA (in order of importance):

1. **Correctness and safety**: Obvious logic errors, missing null checks at API boundaries, potential crashes, use-after-free, dangling references, race conditions. This is the highest priority — bugs and safety issues must be caught first. Do NOT nitpick internal code that relies on framework guarantees.

2. **Dead code**: Any code added or left behind that is never called or used, within the scope of the changes. Unused variables, unreachable branches, leftover scaffolding.

3. **Redundant changes**: Changes in the diff that have no functional effect — moving declarations or code blocks to a different location without reason, reformatting untouched code, reordering includes or fields with no purpose. Every line in the diff should serve the feature. If a file appears in `git diff` but contains only no-op rearrangements, flag it for revert.

4. **Code duplication**: Unnecessary repetition of logic that should be shared. Look for near-identical blocks that differ only in minor details and could be unified.

5. **Wrong placement**: Code added to a module where it doesn't logically belong. If another existing module is a clearly better fit for the new code, flag it. Consider the existing module boundaries and responsibilities visible in context.md.

6. **Function decomposition**: For longer functions (roughly 50+ lines), consider whether a logical sub-task could be cleanly extracted into a separate function. This is NOT a hard rule — a 100-line function that flows naturally and isn't easily divisible is perfectly fine. But sometimes even a 20-line function contains a clear isolated subtask that reads better as two 10-line functions. The key is to think about it each time: does extracting improve readability and reduce cognitive load, or does it just scatter logic across call sites for no real benefit? Only suggest extraction when there's a genuinely self-contained piece of logic with a clear name and purpose.

7. **Module structure**: Only in exceptional cases — if a large amount of newly added code (hundreds of lines) is logically distinct from the rest of its host module, suggest extracting it into a new module. But do NOT suggest new modules lightly: every module adds significant build overhead due to PCH and heavy template usage. Only suggest this when the new code is both large enough AND logically separated enough to justify it. At the same time, don't let modules grow into multi-thousand-line monoliths either.

8. **Style compliance**: Verify adherence to REVIEW.md rules (empty line before closing brace, operators at start of continuation lines, minimize type checks with direct cast instead of is+as, no if-with-initializer when simpler alternatives exist) and AGENTS.md conventions (no unnecessary comments, `auto` usage, no hardcoded sizes — must use .style definitions), etc.

IMPORTANT GUIDELINES:
- Review ONLY the changes made, not pre-existing code in the repository.
- Be pragmatic. Don't suggest changes for the sake of it. Each suggestion should have a clear, concrete benefit.
- Don't suggest adding comments, docstrings, or type annotations — the codebase style avoids these.
- Don't suggest error handling for impossible scenarios or over-engineering.

Write your review to: .ai/<PROJECT>/<LETTER>/review<R>.md

The review document should contain:

## Code Review - Iteration <R>

## Summary
<1-2 sentence overall assessment>

## Verdict: <APPROVED or NEEDS_CHANGES>

<If APPROVED, stop here. Everything looks good.>

<If NEEDS_CHANGES, continue with:>

## Changes Required

### <Issue 1 title>
- **Category**: <dead code | duplication | wrong placement | function decomposition | module structure | style | correctness>
- **File(s)**: <file paths>
- **Problem**: <clear description of what's wrong>
- **Fix**: <specific description of what to change>

### <Issue 2 title>
...

Keep the list focused. Only include issues that genuinely improve the code. If you find yourself listing more than ~5-6 issues, prioritize the most impactful ones.

When finished, report your verdict clearly as: APPROVED or NEEDS_CHANGES.
```

### Step 6b: Review Fix Agent

```text
You are a review fix agent. You implement improvements identified during code review.

Read these files:
- .ai/<PROJECT>/<LETTER>/context.md - Codebase context
- .ai/<PROJECT>/<LETTER>/plan.md - Original implementation plan
- .ai/<PROJECT>/<LETTER>/review<R>.md - Code review with required changes

Then read the source files mentioned in the review.

YOUR TASK: Implement ALL changes listed in review<R>.md.

For each issue in the review:
1. Read the relevant source file(s).
2. Make the specified change.
3. Verify the change makes sense in context.

After all changes are made:
1. Build (from repository root): cmake --build ./out --config Debug --target Telegram
2. If the build fails, fix build errors and rebuild until it passes.
3. If build fails with file-locked errors (C1041, LNK1104), STOP and report - do not retry.

Rules:
- Implement exactly the changes from the review, nothing more.
- Follow AGENTS.md coding conventions.
- Do NOT modify .ai/ files.

When finished, report what changes were made.
```

## Completion

When all phases including build verification and code review are done:
1. Read the final `plan.md` and report the summary to the user.
2. Show which files were modified/created.
3. Note any issues encountered during implementation.
4. Summarize code review iterations: how many rounds, what was found and fixed, or if it was approved on first pass.
5. Calculate and display the total elapsed time since `$START_TIME` (format as `Xh Ym Zs`, omitting zero components — e.g. `12m 34s` or `1h 5m 12s`).
6. Remind the user of the project name so they can request follow-up tasks within the same project.

## Error Handling

- If any phase fails or gets stuck, report the issue to the user and ask how to proceed.
- If context.md or plan.md is not written properly by a phase, re-run that phase with more specific instructions.
- If build errors persist after the build phase's attempts, report the remaining errors to the user.
- If a review fix phase introduces new build errors that it cannot resolve, report to the user.

## Model And Reasoning

All child sessions must use:
- `--model gpt-5.4`
- `-c model_reasoning_effort="xhigh"`

Use the same settings for every phase, including setup, implementation, build verification, and review-fix runs.

## Prompt Delivery

Phase prompts can be large and contain complex formatting. Do not inline them as a quoted CLI argument. For each phase:
1. Write the full prompt to a file in `.ai/<PROJECT>/<LETTER>/logs/phase-<name>.prompt.md`
2. Pipe the file contents into `codex exec` with `Get-Content -Raw <prompt-file> | codex exec ... -`
3. Save the JSONL stream to the matching `phase-<name>.jsonl`

If a sandboxed child launch fails with `Access is denied` in Codex desktop, retry the same command through an escalated shell invocation before falling back to same-session execution.

## Example Runner Commands

```powershell
$PromptFile = ".ai/<PROJECT>/<LETTER>/logs/phase-1-context.prompt.md"
Get-Content -Raw $PromptFile | codex exec --json --full-auto --model gpt-5.4 -c model_reasoning_effort="xhigh" -C <REPO_ROOT> - | Tee-Object .ai/<PROJECT>/<LETTER>/logs/phase-1-context.jsonl

$PromptFile = ".ai/<PROJECT>/<LETTER>/logs/phase-2-plan.prompt.md"
Get-Content -Raw $PromptFile | codex exec --json --full-auto --model gpt-5.4 -c model_reasoning_effort="xhigh" -C <REPO_ROOT> - | Tee-Object .ai/<PROJECT>/<LETTER>/logs/phase-2-plan.jsonl

$PromptFile = ".ai/<PROJECT>/<LETTER>/logs/phase-3-assess.prompt.md"
Get-Content -Raw $PromptFile | codex exec --json --full-auto --model gpt-5.4 -c model_reasoning_effort="xhigh" -C <REPO_ROOT> - | Tee-Object .ai/<PROJECT>/<LETTER>/logs/phase-3-assess.jsonl

$PromptFile = ".ai/<PROJECT>/<LETTER>/logs/phase-4-impl-N.prompt.md"
Get-Content -Raw $PromptFile | codex exec --json --full-auto --model gpt-5.4 -c model_reasoning_effort="xhigh" -C <REPO_ROOT> - | Tee-Object .ai/<PROJECT>/<LETTER>/logs/phase-4-impl-N.jsonl

$PromptFile = ".ai/<PROJECT>/<LETTER>/logs/phase-5-build.prompt.md"
Get-Content -Raw $PromptFile | codex exec --json --full-auto --model gpt-5.4 -c model_reasoning_effort="xhigh" -C <REPO_ROOT> - | Tee-Object .ai/<PROJECT>/<LETTER>/logs/phase-5-build.jsonl

$PromptFile = ".ai/<PROJECT>/<LETTER>/logs/phase-6a-review-R.prompt.md"
Get-Content -Raw $PromptFile | codex exec --json --full-auto --model gpt-5.4 -c model_reasoning_effort="xhigh" -C <REPO_ROOT> - | Tee-Object .ai/<PROJECT>/<LETTER>/logs/phase-6a-review-R.jsonl

$PromptFile = ".ai/<PROJECT>/<LETTER>/logs/phase-6b-fix-R.prompt.md"
Get-Content -Raw $PromptFile | codex exec --json --full-auto --model gpt-5.4 -c model_reasoning_effort="xhigh" -C <REPO_ROOT> - | Tee-Object .ai/<PROJECT>/<LETTER>/logs/phase-6b-fix-R.jsonl
```
