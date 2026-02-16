# Phase Prompts

Use these templates for `codex exec --json` child runs. Replace `<TASK>`, `<PROJECT>`, `<LETTER>`, and `<REPO_ROOT>`.

## Phase 0: Setup

Before running any phase prompts, the orchestrator must determine whether this is a new project or a follow-up task.

**Follow-up detection:**
1. Extract the first word/token from the task description. Call it `FIRST_TOKEN`.
2. Check if `.ai/<FIRST_TOKEN>/about.md` exists.
3. If it exists: this is a **follow-up task**. The project name is `FIRST_TOKEN`. The task description is everything after `FIRST_TOKEN`.
4. If it does not exist: this is a **new project**. The full input is the task description.

**For new projects:**
- List existing `.ai/` folders to pick a unique short name (1-2 lowercase words, hyphen-separated).
- Create `.ai/<PROJECT>/` and `.ai/<PROJECT>/a/` and `logs/`.
- Set `<LETTER>` = `a`.

**For follow-up tasks:**
- Scan `.ai/<PROJECT>/` for existing task folders (`a/`, `b/`, ...). Find the latest one (highest letter).
- The new task letter = next letter in sequence.
- Create `.ai/<PROJECT>/<LETTER>/` and `logs/`.

Then proceed to Phase 1. Follow-up tasks do NOT skip context gathering — they go through a modified version of it.

## Phase 1: Context (New Project, letter = `a`)

```text
You are the context phase for task "<TASK>" in repository <REPO_ROOT>.

Read CLAUDE.md for the basic coding rules and guidelines.

Read AGENTS.md and all relevant source files. Write TWO documents:

### File 1: .ai/<PROJECT>/about.md

NOTE: This file is NOT used by any agent in the current task. It exists solely as a starting point for a FUTURE follow-up task's context gatherer. No planning, implementation, or review agent will ever read it.

Write it as if the project is already fully implemented and working. It should contain:
- **Project**: What this project does (feature description, goals, scope)
- **Architecture**: High-level architectural decisions, which modules are involved, how they interact
- **Key Design Decisions**: Important choices made about the approach
- **Relevant Codebase Areas**: Which parts of the codebase this project touches, key types and APIs involved

Do NOT include temporal state like "Current State", "Pending Changes", "Not yet implemented", "TODO", or any other framing that distinguishes between "done" and "not done". Describe the project as a complete, coherent whole.

### File 2: .ai/<PROJECT>/a/context.md

This is the PRIMARY document — all downstream agents (planning, implementation, review) will read ONLY this file. It must be completely self-contained. Include:
1. Task description restated clearly
2. Relevant files with line ranges and why they matter
3. Existing patterns to follow (with code snippets)
4. Data structures, types, classes
5. API methods (from api.tl if applicable)
6. UI styles (from .style files if applicable)
7. Localization (from lang.strings if applicable)
8. Build info and verification hooks
9. Reference implementations of similar features
10. Risks and unknowns

Be extremely thorough. Another agent with NO prior context will read this file and must be able to understand everything needed to implement the task.

Do not implement code in this phase.
```

## Phase 1F: Context (Follow-up Task, letter = `b`, `c`, ...)

```text
You are the context phase for a follow-up task on an existing project in repository <REPO_ROOT>.

NEW TASK: <TASK>

Read CLAUDE.md for the basic coding rules and guidelines.

Steps:
1. Read AGENTS.md for project conventions.
2. Read .ai/<PROJECT>/about.md — the project-level blueprint describing everything done so far.
3. Read .ai/<PROJECT>/<PREV_LETTER>/context.md — the previous task's gathered context.
4. Understand what has already been implemented by reading the actual source files referenced in about.md and the previous context.
5. Search the codebase for any ADDITIONAL files, classes, functions, and patterns relevant to the new task but not already covered.
6. Read all newly relevant files thoroughly.

Write TWO files:

### File 1: .ai/<PROJECT>/about.md (REWRITE)

NOTE: This file is NOT used by any agent in the current task. It exists solely as a starting point for a FUTURE follow-up task's context gatherer. You are rewriting it now so that the next follow-up has an accurate project overview to start from.

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
- Information that contradicts the new task requirements

### File 2: .ai/<PROJECT>/<LETTER>/context.md

This is the PRIMARY document — all downstream agents (planning, implementation, review) will read ONLY this file. It must be completely self-contained. about.md will NOT be available to them.

It should contain:
- **Task Description**: The new task restated clearly, with enough project background that an implementation agent can understand it without reading any other .ai/ files
- **Relevant Files**: Every file path with line ranges relevant to THIS task (including files modified by previous tasks and any newly relevant files)
- **Key Code Patterns**: How similar things are done in the codebase
- **Data Structures**: Relevant types, structs, classes
- **API Methods**: Any TL schema methods involved
- **UI Styles**: Any relevant style definitions
- **Localization**: Any relevant string keys
- **Build Info**: Build command and any special notes
- **Reference Implementations**: Similar features that can serve as templates

Be extremely thorough. Another agent with NO prior context will read ONLY this file and must be able to understand everything needed to implement the new task. Do NOT assume the reader has seen about.md or any previous task files.

Do not implement code in this phase.
```

## Phase 2: Plan

```text
You are the planning phase for task "<TASK>" in repository <REPO_ROOT>.

Read CLAUDE.md for the basic coding rules and guidelines.

Read:
- .ai/<PROJECT>/<LETTER>/context.md

Then read the specific source files referenced in context.md to understand the code deeply.

Create:
- .ai/<PROJECT>/<LETTER>/plan.md

Plan requirements:
1. Concrete file-level edits
2. Ordered phases (each phase implementable by a single agent, roughly <=8 steps)
3. Verification commands
4. Rollback/risk notes
5. Status section with checklist of phases, build verification, and code review
```

## Phase 3: Implement

```text
You are the implementation phase for task "<TASK>" in repository <REPO_ROOT>.

Read CLAUDE.md for the basic coding rules and guidelines.

Read:
- .ai/<PROJECT>/<LETTER>/context.md
- .ai/<PROJECT>/<LETTER>/plan.md

Implement the plan in code. Then write:
- .ai/<PROJECT>/<LETTER>/implementation.md

Include:
1. Files changed
2. What was implemented
3. Any deviations from plan and why
4. Update plan.md Status section to mark completed phases
```

## Phase 4: Verify

```text
You are the verification phase for task "<TASK>" in repository <REPO_ROOT>.

Read CLAUDE.md for the basic coding rules and guidelines.

Read:
- .ai/<PROJECT>/<LETTER>/plan.md
- .ai/<PROJECT>/<LETTER>/implementation.md

Run the relevant build/test commands from AGENTS.md and plan.md.
Append results to:
- .ai/<PROJECT>/<LETTER>/implementation.md

If blocked by locked files or access errors, stop and report exact blocker.
```

## Phase 5: Review

```text
You are the review phase for task "<TASK>" in repository <REPO_ROOT>.

Read AGENTS.md for the basic coding rules and guidelines.
Read REVIEW.md for the style and formatting rules you must enforce.

Read:
- .ai/<PROJECT>/<LETTER>/context.md
- .ai/<PROJECT>/<LETTER>/plan.md
- .ai/<PROJECT>/<LETTER>/implementation.md

Run `git diff` to see all uncommitted changes made by the implementation. Implementation phases do not commit, so `git diff` shows exactly the current feature's changes. Then read the modified source files in full.

Perform a code review using these criteria (in order of importance):

1. Correctness and safety: logic errors, null-check gaps at API boundaries, crashes, use-after-free, dangling references, race conditions.
2. Dead code: code added or left behind that is never called or used. Unused variables, unreachable branches, leftover scaffolding.
3. Redundant changes: changes in the diff with no functional effect — moving declarations or code blocks without reason, reformatting untouched code, reordering includes or fields with no purpose. Every line in the diff should serve the feature. If a file appears in `git diff` but contains only no-op rearrangements, flag it for revert.
4. Code duplication: unnecessary repetition of logic that should be shared.
5. Wrong placement: code added to a module where it doesn't logically belong.
6. Function decomposition: for longer functions (~50+ lines), consider whether a sub-task could be cleanly extracted. Only suggest when there is a genuinely self-contained piece of logic.
7. Module structure: only flag if a large amount of new code (hundreds of lines) is logically distinct from its host module.
8. Style compliance: verify adherence to REVIEW.md rules and AGENTS.md conventions.

Write:
- .ai/<PROJECT>/<LETTER>/review.md

If issues are found, implement fixes and update implementation.md/review.md with final status.
```

## Example Runner Commands

```powershell
codex exec --json -C <REPO_ROOT> "<PHASE_PROMPT>" | Tee-Object .ai/<PROJECT>/<LETTER>/logs/phase-1-context.jsonl
codex exec --json -C <REPO_ROOT> "<PHASE_PROMPT>" | Tee-Object .ai/<PROJECT>/<LETTER>/logs/phase-2-plan.jsonl
codex exec --json -C <REPO_ROOT> "<PHASE_PROMPT>" | Tee-Object .ai/<PROJECT>/<LETTER>/logs/phase-3-implement.jsonl
codex exec --json -C <REPO_ROOT> "<PHASE_PROMPT>" | Tee-Object .ai/<PROJECT>/<LETTER>/logs/phase-4-verify.jsonl
codex exec --json -C <REPO_ROOT> "<PHASE_PROMPT>" | Tee-Object .ai/<PROJECT>/<LETTER>/logs/phase-5-review.jsonl
```
