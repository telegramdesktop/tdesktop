---
description: Implement a feature using multi-agent workflow, then iteratively test and fix it in-app
allowed-tools: Read, Write, Edit, Glob, Grep, Bash, Task, AskUserQuestion, TodoWrite
---

# WithTest - Multi-Agent Implementation + Testing Workflow

You orchestrate a multi-phase implementation workflow followed by an iterative testing/fixing loop. This is an extended version of `/task` that adds in-app programmatic testing after the build succeeds.

**Arguments:** `$ARGUMENTS` = "$ARGUMENTS"

If `$ARGUMENTS` is provided, it's the task description. If empty, ask the user what they want implemented.

## Overview

The workflow produces `.ai/<feature-name>/` containing:
- `context.md` - Gathered codebase context relevant to the task
- `plan.md` - Detailed implementation plan with phases and status
- `testN.md` - Test plan for iteration N
- `resultN.md` - Test result report for iteration N
- `planN.md` - Fix plan for iteration N (if implementation bugs found)
- `screenshots/` - Screenshots captured during test runs

Two major stages:
1. **Implementation** (Phases 0-5) - same as `/task`
2. **Testing Loop** (Phase 6) - iterative test-plan → test-do → test-run → test-check cycle

---

## STAGE 1: IMPLEMENTATION (Phases 0-5)

These phases are identical to the `/task` workflow.

### Phase 0: Setup

1. Understand the task from `$ARGUMENTS` or ask the user.
2. **Follow-up detection:** Check if `$ARGUMENTS` starts with a task name (the first word/token before any whitespace or newline). Look for `.ai/<that-name>/` directory:
   - If `.ai/<that-name>/` exists AND contains both `context.md` and `plan.md`, this is a **follow-up task**. Read both files. The rest of `$ARGUMENTS` (after the task name) is the follow-up task description describing what additional changes are needed.
   - If no matching directory exists, this is a **new task** - proceed normally.
3. For new tasks: check existing folders in `.ai/` to pick a unique short name (1-2 lowercase words, hyphen-separated) and create `.ai/<feature-name>/`.
4. For follow-up tasks: the folder already exists, skip creation.

### Follow-up Task Flow

When a follow-up task is detected (existing `.ai/<name>/` with `context.md` and `plan.md`):

1. Skip Phase 1 (Context Gathering) - context already exists.
2. Skip Phase 2 (Planning) - original plan already exists.
3. Go directly to **Phase 2F (Follow-up Planning)** instead of Phase 3.

**Phase 2F: Follow-up Planning**

Spawn an agent (Task tool, subagent_type=`general-purpose`) with this prompt:

```
You are a planning agent for a follow-up task on an existing implementation.

Read these files:
- .ai/<feature-name>/context.md - Previously gathered codebase context
- .ai/<feature-name>/plan.md - Previous implementation plan (already completed)

Then read the source files referenced in context.md and plan.md to understand what was already implemented.

FOLLOW-UP TASK: <paste the follow-up task description here>

The previous plan was already implemented and tested. Now there are follow-up changes needed.

YOUR JOB:
1. Understand what was already done from plan.md (look at the completed phases).
2. Read the actual source files to see the current state of the code.
3. If context.md needs updates for the follow-up task (new files relevant, new patterns needed), update it with additional sections marked "## Follow-up Context (iteration 2)" or similar.
4. Create a NEW follow-up plan. Update plan.md by:
   - Keep the existing content as history (do NOT delete it)
   - Add a new section at the end:

   ---
   ## Follow-up Task
   <description>

   ## Follow-up Approach
   <high-level description>

   ## Follow-up Files to Modify
   <list>

   ## Follow-up Implementation Steps

   ### Phase F1: <name>
   1. <specific step>
   2. ...

   ### Phase F2: <name> (if needed)
   ...

   ## Follow-up Status
   Phases: <N>
   - [ ] Phase F1: <name>
   - [ ] Phase F2: <name> (if applicable)
   - [ ] Build verification
   - [ ] Testing
   Assessed: yes

Reason carefully. The follow-up plan should be self-contained enough that an implementation agent can execute it by reading context.md and the updated plan.md.
```

After this agent completes, read `plan.md` to verify the follow-up plan was written. Then proceed to Phase 4 (Implementation), using the follow-up phases (F1, F2, etc.) instead of the original phases. After implementation and build verification, proceed to Stage 2 (Testing Loop) as normal.

### New Task Flow

When this is a new task (no existing folder), proceed with Phases 1-5 as described below.

### Phase 1: Context Gathering

Spawn an agent (Task tool, subagent_type=`general-purpose`) with this prompt structure:

```
You are a context-gathering agent for a large C++ codebase (Telegram Desktop).

TASK: <paste the user's task description here>

YOUR JOB: Read CLAUDE.md, inspect the codebase, find ALL files and code relevant to this task, and write a comprehensive context document.

Steps:
1. Read CLAUDE.md for project conventions and build instructions.
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

Write your findings to: .ai/<feature-name>/context.md

The context.md should contain:
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
```

After this agent completes, read `context.md` to verify it was written properly.

### Phase 2: Planning

Spawn an agent (Task tool, subagent_type=`general-purpose`) with this prompt structure:

```
You are a planning agent. You must create a detailed implementation plan.

Read these files:
- .ai/<feature-name>/context.md - Contains all gathered context
- Then read the specific source files referenced in context.md to understand the code deeply.

Think carefully about the implementation approach.

Create a detailed plan in: .ai/<feature-name>/plan.md

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
- [ ] Testing
```

After this agent completes, read `plan.md` to verify it was written properly.

### Phase 3: Plan Assessment

Spawn an agent (Task tool, subagent_type=`general-purpose`) with this prompt structure:

```
You are a plan assessment agent. Review and refine an implementation plan.

Read these files:
- .ai/<feature-name>/context.md
- .ai/<feature-name>/plan.md
- Then read the actual source files referenced to verify the plan makes sense.

Carefully assess the plan:

1. **Correctness**: Are the file paths and line references accurate? Does the plan reference real functions and types?
2. **Completeness**: Are there missing steps? Edge cases not handled?
3. **Code quality**: Will the plan minimize code duplication? Does it follow existing codebase patterns from CLAUDE.md?
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
```

After this agent completes, read `plan.md` to verify it was assessed.

### Phase 4: Implementation

Now read `plan.md` yourself to understand the phases.

For each phase in the plan that is not yet marked as done, spawn an implementation agent (Task tool, subagent_type=`general-purpose`):

```
You are an implementation agent working on phase <N> of an implementation plan.

Read these files first:
- .ai/<feature-name>/context.md - Full codebase context
- .ai/<feature-name>/plan.md - Implementation plan

Then read the source files you'll be modifying.

YOUR TASK: Implement ONLY Phase <N> from the plan:
<paste the specific phase steps here>

Rules:
- Follow the plan precisely
- Follow CLAUDE.md coding conventions (no comments except complex algorithms, use auto, empty line before closing brace, etc.)
- Do NOT modify .ai/ files except to update the Status section in plan.md
- When done, update plan.md Status section: change `- [ ] Phase <N>: ...` to `- [x] Phase <N>: ...`
- Do NOT work on other phases

When finished, report what you did and any issues encountered.
```

After each implementation agent returns:
1. Read `plan.md` to check the status was updated.
2. If more phases remain, spawn the next implementation agent.
3. If all phases are done, proceed to build verification.

### Phase 5: Build Verification

Spawn a build verification agent (Task tool, subagent_type=`general-purpose`):

```
You are a build verification agent.

Read these files:
- .ai/<feature-name>/context.md
- .ai/<feature-name>/plan.md

The implementation is complete. Your job is to build the project and fix any build errors.

Steps:
1. Run: cmake --build "c:\Telegram\tdesktop\out" --config Debug --target Telegram
2. If the build succeeds, update plan.md: change `- [ ] Build verification` to `- [x] Build verification`
3. If the build fails:
   a. Read the error messages carefully
   b. Read the relevant source files
   c. Fix the errors in accordance with the plan and CLAUDE.md conventions
   d. Rebuild and repeat until the build passes
   e. Update plan.md status when done

Rules:
- Only fix build errors, do not refactor or improve code
- Follow CLAUDE.md conventions
- If build fails with file-locked errors (C1041, LNK1104), STOP and report - do not retry

When finished, report the build result.
```

After the build agent returns, read `plan.md` to confirm build verification passed. If it did, proceed to Stage 2.

---

## STAGE 2: TESTING LOOP (Phase 6)

This stage iteratively tests the implementation in-app and fixes issues. It maintains an iteration counter `N` starting at 1.

**Key concept:** Since the project has tight coupling and no unit test infrastructure, we test by injecting `#ifdef _DEBUG` blocks into the app code that perform actions, write to `log.txt`, save screenshots, and call `Core::Quit()` when done. An agent then runs the app and observes the output.

### Git Submodule Awareness

Before ANY git operation (commit, stash, stash pop), the agent must:
1. Run `git submodule status` to check for modified submodules.
2. If submodules have changes, commit/stash those submodules FIRST, individually:
   ```
   cd <submodule-path> && git add -A && git commit -m "[wip-N] test changes" && cd <repo-root>
   ```
   or for stash:
   ```
   cd <submodule-path> && git stash && cd <repo-root>
   ```
3. Then operate on the main repo.

### Step 6a: Test Plan (test-plan agent)

Spawn an agent (Task tool, subagent_type=`general-purpose`):

```
You are a test-planning agent for Telegram Desktop (C++ / Qt).

Read these files:
- .ai/<feature-name>/context.md
- .ai/<feature-name>/plan.md
<if N > 1, also include:>
- .ai/<feature-name>/result<N-1>.md - Previous test result
<if a planN.md triggered this iteration:>
- .ai/<feature-name>/plan<trigger>.md - Fix plan that was just implemented

CURRENT ITERATION: <N>

YOUR TASKS:

1. **Commit current implementation changes.**
   - Run `git submodule status` to check for modified submodules.
   - If any submodules are dirty, go into each one and commit:
     `cd <submodule> && git add -A && git commit -m "[wip-<N>]" && cd <repo-root>`
   - Then in main repo: `git add -A && git commit -m "[wip-<N>]"`
   - Do NOT add files in .ai/ to the commit.

2. <If N > 1> **Restore previous test code.**
   - Run `git submodule status` and `git stash list` in any dirty submodules to check for stashed test code.
   - Pop submodule stashes first: `cd <submodule> && git stash pop && cd <repo-root>`
   - Then pop main repo stash: `git stash pop`
   - Read the previous test<N-1>.md to understand what was tested before.
   - Decide: reuse/modify existing test code or start fresh.

3. **Plan the test code.**
   Carefully design test code that will verify the implementation works correctly.

   The test code must:
   - Be wrapped in `#ifdef _DEBUG` blocks so it only runs in Debug builds
   - Be injected at appropriate points in the app lifecycle (e.g., after main window shows, after chats load, etc.)
   - Write progress and results to a log file. Use a dedicated path like:
     `QFile logFile("c:/Telegram/tdesktop/.ai/<feature-name>/test_log.txt");`
     Open with `QIODevice::Append | QIODevice::Text`, write with QTextStream, and flush after every write.
   - Save screenshots where visual verification is needed:
     `widget->grab().save("c:/Telegram/tdesktop/.ai/<feature-name>/screenshots/<name>.png");`
     Log each screenshot save: `"SCREENSHOT: <full-path>"`
   - Use `QTimer::singleShot(...)` or deferred calls to schedule test steps after UI events settle
   - Call `Core::Quit()` when all test steps complete, so the app exits cleanly
   - Log `"TEST_COMPLETE"` right before `Core::Quit()` so the test-run agent knows testing finished
   - Log `"TEST_STEP: <description>"` before each major step for progress tracking
   - Log `"TEST_RESULT: PASS: <what>"` or `"TEST_RESULT: FAIL: <what> - <details>"` for each check

   Consider what needs testing:
   - Does the new UI appear correctly?
   - Do interactions work (clicks, navigation)?
   - Does data flow correctly?
   - Are there edge cases to verify?

4. **Write the test plan** to `.ai/<feature-name>/test<N>.md` containing:

   ## Test Iteration <N>
   ## What We're Testing
   <description of what this test verifies>

   ## Test Steps
   1. <step>: what we do, what we expect, how we verify
   2. ...

   ## Code Injection Points
   - File: <path>, Location: <where in file>, Purpose: <what this block does>
   - ...

   ## Expected Log Output
   <example of what test_log.txt should contain if everything works>

   ## Expected Screenshots
   - <name>.png: should show <description>
   - ...

   ## Success Criteria
   - <criterion 1>
   - <criterion 2>
   - ...

When finished, report what test plan was created.
```

### Step 6b: Test Implementation (test-do agent)

Spawn an agent (Task tool, subagent_type=`general-purpose`):

```
You are a test implementation agent for Telegram Desktop (C++ / Qt).

Read these files:
- .ai/<feature-name>/context.md
- .ai/<feature-name>/plan.md
- .ai/<feature-name>/test<N>.md - The test plan to implement

YOUR TASK: Implement the test code described in test<N>.md.

Rules:
- ALL test code MUST be inside `#ifdef _DEBUG` blocks
- Place test code at the injection points specified in the test plan
- Make sure the screenshots folder exists: create `.ai/<feature-name>/screenshots/` directory
- Delete any old test_log.txt before the test starts (in code, at the first test step)
- Use QTimer::singleShot for delayed operations to let the UI settle
- Flush log writes immediately (don't buffer)
- End with logging "TEST_COMPLETE" and calling Core::Quit()
- Follow CLAUDE.md coding conventions
- Make sure the code compiles: run `cmake --build "c:\Telegram\tdesktop\out" --config Debug --target Telegram`
- If build fails, fix errors and rebuild until it passes
- If build fails with file-locked errors (C1041, LNK1104), STOP and report

When finished, report what test code was added and where.
```

### Step 6c: Test Run (test-run agent)

Spawn an agent (Task tool, subagent_type=`general-purpose`):

```
You are a test execution agent. You run the Telegram Desktop app and observe test output.

Read these files:
- .ai/<feature-name>/test<N>.md - The test plan (so you know what to expect)

YOUR TASK: Run the built app and monitor test execution.

Steps:

1. **Prepare.**
   - Delete old test_log.txt if it exists: `del "c:\Telegram\tdesktop\docs\ai\work\<feature-name>\test_log.txt" 2>nul`
   - Ensure screenshots folder exists: `mkdir "c:\Telegram\tdesktop\docs\ai\work\<feature-name>\screenshots" 2>nul`

2. **Launch the app.**
   - Run in background: `start "" "c:\Telegram\tdesktop\out\Debug\Telegram.exe"`
   - Note the time of launch.

3. **Monitor test_log.txt in a polling loop.**
   - Every 5 seconds, read the log file to check for new output.
   - When you see `"SCREENSHOT: <path>"`, read the screenshot image file to visually verify it.
   - Track which TEST_STEP entries appear.
   - Track TEST_RESULT entries (PASS/FAIL).

4. **Detect completion or failure.**
   - **Success**: Log contains `"TEST_COMPLETE"` - the app should exit on its own shortly after.
   - **Crash**: The process disappears before `"TEST_COMPLETE"`. Check for crash dumps or error dialogs.
   - **Hang/Timeout**: If no new log output for 120 seconds and no `"TEST_COMPLETE"`, kill the process:
     `taskkill /IM Telegram.exe /F`
   - **No log at all**: If no test_log.txt appears within 60 seconds of launch, kill the process.

5. **After the process exits (or is killed), wait 5 seconds, then:**
   - Read the full final test_log.txt
   - Read all screenshot files saved during the test
   - Check for any leftover Telegram.exe processes: `tasklist /FI "IMAGENAME eq Telegram.exe"` and kill if needed

6. **Write the result report** to `.ai/<feature-name>/result<N>.md`:

   ## Test Result - Iteration <N>
   ## Outcome: <PASS / FAIL / CRASH / TIMEOUT>

   ## Log Output
   <full contents of test_log.txt, or note that it was empty/missing>

   ## Screenshot Analysis
   - <name>.png: <description of what you see, whether it matches expectations from test<N>.md>
   - ...

   ## Test Results Summary
   - PASS: <list>
   - FAIL: <list>

   ## Issues Found
   <any problems observed, unexpected behavior, etc.>

   ## Raw Details
   <process exit code if available, timing information, any stderr output>

When finished, report the test outcome.
```

After the test-run agent returns, read `result<N>.md`.

### Step 6d: Test Assessment (test-check agent)

Spawn an agent (Task tool, subagent_type=`general-purpose`):

```
You are a test assessment agent. You analyze test results and decide next steps.

Read these files:
- .ai/<feature-name>/context.md
- .ai/<feature-name>/plan.md
- .ai/<feature-name>/test<N>.md
- .ai/<feature-name>/result<N>.md
<if N > 1, also read previous test/result pairs for history>

Carefully analyze the test results.

DECIDE one of three outcomes:

### Outcome A: ALL TESTS PASS
If all test results are PASS and screenshots look correct:
1. Write to result<N>.md (append): `\n## Verdict: PASS`
2. Report "ALL_TESTS_PASS" so the orchestrator knows to finish.

### Outcome B: TEST CODE NEEDS CHANGES
If the test itself was flawed (wrong assertions, bad timing, insufficient waits, screenshot taken too early, wrong injection point, etc.) but the implementation seems correct:
1. Describe what's wrong with the test and what to change.
2. Make the changes directly to the test code in the source files.
3. Rebuild: `cmake --build "c:\Telegram\tdesktop\out" --config Debug --target Telegram`
4. If build fails with file-locked errors (C1041, LNK1104), STOP and report.
5. Write the updated test description to `.ai/<feature-name>/test<N+1>.md` explaining what changed and why.
6. Report "TEST_NEEDS_RERUN" so the orchestrator goes back to step 6c.

### Outcome C: IMPLEMENTATION HAS BUGS
If the test results indicate actual bugs in the implementation (not test issues):
1. Analyze what's wrong with the implementation.
2. Write a fix plan to `.ai/<feature-name>/plan<N>.md`:

   ## Fix Plan - Iteration <N>
   ## Problem
   <what the test revealed>

   ## Root Cause
   <analysis of why the implementation is wrong>

   ## Fix Steps
   1. <specific fix with file path, location, what to change>
   2. ...

3. Stash the test code (it will be restored later):
   - Run `git submodule status` and stash dirty submodules first:
     `cd <submodule> && git stash && cd <repo-root>`
   - Then: `git stash`
4. Report "IMPLEMENTATION_NEEDS_FIX" so the orchestrator goes to re-implementation.

When finished, report your verdict clearly as one of: ALL_TESTS_PASS, TEST_NEEDS_RERUN, IMPLEMENTATION_NEEDS_FIX.
```

### Orchestrator Loop Logic

After Phase 5 (build verification) succeeds, you (the orchestrator) run the testing loop:

```
Set N = 1

LOOP:
  1. Spawn test-plan agent (Step 6a) with iteration N
  2. Spawn test-do agent (Step 6b) with iteration N
  3. Spawn test-run agent (Step 6c) with iteration N
  4. Spawn test-check agent (Step 6d) with iteration N
  5. Read the verdict:
     - "ALL_TESTS_PASS" → go to FINISH
     - "TEST_NEEDS_RERUN" →
         N = N + 1
         go to step 3 (skip 6a and 6b, test code was already updated by test-check)
     - "IMPLEMENTATION_NEEDS_FIX" →
         Spawn implementation fix agent (see below)
         N = N + 1
         go to step 1 (full restart: new commit, stash pop test code, etc.)
  6. Safety: if N > 5, stop and report to user - too many iterations.

FINISH:
  - Stash or revert all test code (#ifdef _DEBUG blocks):
    - git submodule status, stash submodules if dirty
    - git stash (to save test code separately, user may want it later)
  - Update plan.md: change `- [ ] Testing` to `- [x] Testing`
  - Report to user
```

### Implementation Fix Agent

When test-check reports IMPLEMENTATION_NEEDS_FIX, spawn this agent:

```
You are an implementation fix agent.

Read these files:
- .ai/<feature-name>/context.md
- .ai/<feature-name>/plan.md
- .ai/<feature-name>/plan<N>.md - The fix plan from test assessment

Then read the source files mentioned in the fix plan.

YOUR TASK: Implement the fixes described in plan<N>.md.

Steps:
1. Read and understand the fix plan.
2. Make the specified code changes.
3. Build: `cmake --build "c:\Telegram\tdesktop\out" --config Debug --target Telegram`
4. Fix any build errors.
5. If build fails with file-locked errors (C1041, LNK1104), STOP and report.

Rules:
- Only make changes specified in the fix plan
- Follow CLAUDE.md conventions
- Do NOT touch test code or .ai/ files (except plan.md status if relevant)

When finished, report what was fixed.
```

---

## Completion

When the testing loop finishes (ALL_TESTS_PASS or user stops it):
1. Read the final `plan.md` and report full summary to the user.
2. List all files modified/created by the implementation.
3. Summarize test iterations: how many rounds, what was found and fixed.
4. Note that test code is stashed (available via `git stash pop` if needed).
5. Note any remaining concerns.

## Error Handling

- If any agent fails or gets stuck, report the issue to the user and ask how to proceed.
- If context.md or plan.md is not written properly by an agent, re-spawn that agent with more specific instructions.
- If build errors persist after agent attempts, report remaining errors to the user.
- If the testing loop exceeds 5 iterations, stop and report - something fundamental may be wrong.
- If the app crashes repeatedly, report to user - may need manual investigation.
- If file-locked build errors occur at ANY point, stop immediately and ask user to close Telegram.exe.
