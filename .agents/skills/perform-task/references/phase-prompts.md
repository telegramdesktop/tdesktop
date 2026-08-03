# Telegram Task Phase Prompts

## Contents

- [Orchestration rules](#orchestration-rules)
- [Completion checks](#artifact-based-completion-checks)
- [Context and plan](#phase-1-context-and-plan)
- [Verification: measurement plan](#phase-1v-context-and-measurement-plan)
- [Verification: falsifiability assessment](#phase-3v-falsifiability-assessment)
- [Assessment](#phase-3-plan-assessment)
- [Implementation and build](#phase-4-implementation)
- [Review](#phase-6-code-review-loop)
- [Test-flaw recovery](#test-flaw-recovery-and-directness)
- [Windows normalization](#phase-7-native-windows-text-normalization)
- [Prompt delivery](#prompt-delivery-and-logs)

Use these templates as subagent messages on any host. Use them as same-session
checklists only for intentional current-session build work, Phase 7, the
small-task fast path, or when delegation is unavailable from the start at the
current agent depth. Replace
every applicable placeholder: `<TASK>`, `<TASK_ID>`, `<WORK_DIR>`,
`<PROJECT_FILE>`, `<PREVIOUS_CONTEXT>`, `<BUILD>`, `<N>`,
`<OWNED_WRITE_SET>`, `<R>`, `<R-1>`, and `<phase-name>`.

## Orchestration Rules

- When delegation is available, use a fresh subagent for Phase 1 (context and plan), Phase 3, each Phase 4 implementation unit, each Phase 6a lens, the Phase 6d test-design leaf, each Phase 6s synthesis, and each Phase 6b fix. Do not switch those phases to same-session midstream because of a timeout or missing artifact.
- The Phase 6a lenses of one iteration are independent and write disjoint report files, so spawn them together when capacity allows. The Phase 6d test-design leaf writes its own disjoint artifact and joins the iteration-1 fan-out. Never let one lens read another's report, and never collapse them into a single combined reviewer: their independence is the point of the phase.
- Treat delegation as selected only after the first real phase spawn succeeds; tool presence is insufficient. An immediate depth/capacity/policy rejection before phase work selects same-session checklists and is not a delegated retry.
- Phase 7 runs in the current session on native, non-WSL Windows because it depends on the final local diff and touched-file set. Skip it on WSL and keep files LF/no-BOM there.
- Write each phase prompt to `<WORK_DIR>/logs/phase-<phase-name>.prompt.md` before execution.
- If you delegate a phase, send the prompt file contents as the initial subagent message.
- When writing the phase prompt file, append the standard progress file contract and the standard compact reply block below so the subagent knows how to surface progress before the final artifact.
- After each phase completes, write `<WORK_DIR>/logs/phase-<phase-name>.result.md` with exact
  `STATUS:`, `ARTIFACTS:`, `TOUCHED:`, `BLOCKER:`, and `NOTES:` fields.
- Use `fork_turns: "none"` by default. If the phase depends on thread-only context or UI attachments, pass it explicitly or use the smallest positive turn fork needed.
- Use only fields the current spawn schema exposes; do not invent role, model, or reasoning arguments. Inherit the parent model/reasoning selection, or match it if the host explicitly supports overrides.
- Give each phase a unique lowercase/digit/underscore task name and tell the phase it is a leaf that must not delegate.
- For Phase 1, Phase 3, Phase 4, and Phase 6, if delegated retries still fail, stop and ask the user rather than rerunning the phase locally.
- Never use `codex exec`, background shell child processes, or JSONL child-session logging from this skill.

### Claude Code: synchronous delegation

- Run each leaf as one synchronous foreground Agent call. The call returning
  is the completion signal; there is no polling, no heartbeat-mtime ladder,
  and no stall windows. On return, validate the artifact-based completion
  checks below before treating the phase as done.
- Spawn the independent leaves of one step — the Phase 6a lenses plus the
  iteration-1 Phase 6d test-design leaf, or assessed-disjoint Phase 4 units —
  as parallel Agent calls in a single message so they run concurrently.
- If a returned leaf fails its completion check, retry that disposable phase
  once in a fresh Agent with more specific instructions before stopping to
  ask the user.

### Codex: asynchronous spawn and wait

- Store the canonical target returned by `spawn_agent`.
- Poll with `wait_agent` for at most 60 seconds per call; use elapsed wall-clock windows for stall decisions. Use 30-60 second polls when a phase appears close to landing.
- `wait_agent` is mailbox-wide and may wake for another agent or user input. A timeout is not failure. After every wake, handle new user input if any, inspect the saved target with `list_agents`, and check the expected artifact and matching progress file.
- If the expected artifact exists and shows progress, wait again.
- If the expected artifact is not ready but the progress file mtime moved or its heartbeat counter increased since the previous check, wait again. Prefer mtime checks first and avoid rereading the file unless you need detail. Do not count that as a failed wait.
- If neither the expected artifact nor progress file moved for a full five-minute blocked-check window, use `send_message` while the target is running or `followup_task` when it is idle, asking it to refresh progress, finish the artifact, and return the compact block.
- If a second five-minute window after that follow-up still produces no usable artifact or movement, use `interrupt_agent` if needed, confirm the turn stopped, and retry the disposable phase once with a new unique name. There is no close-agent operation.

## Standard Progress File Contract

Append this verbatim to every delegated phase prompt:

```text
You are a leaf phase worker. Do not spawn or delegate to other agents.

Before deep work, create or update the matching progress file in `<WORK_DIR>/logs/`.

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

- Phase 1 is complete only when `context.md` exists and is non-empty, `plan.md`
  exists and contains a `## Status` section, and no unintended source edits
  were made. For a project task, `project.proposed.md` must also exist and be
  non-empty. For a `Visual: layout` task, `visual.md` must also satisfy the
  visual design completion check below.
- Phase 3 is complete only when `plan.md` contains both `Phases:` in the Status section and `Assessed: yes`, or records a rejection outcome (`Fast-Path: rejected` or `Approach: rejected`) that sends the performer back to a fresh Phase 1 leaf.
- Phase 4 is complete only when the target phase checkbox changed to checked and the touched-file list matches the owned write set, or the blocker explains any mismatch.
- Phase 5 is complete only when the build outcome is known and the build checkbox is updated on success.
- Phase 6a is complete only when every lens scheduled for iteration `R` wrote `review<R>-<lens>.md` with a `## Verdict:` line and a non-empty `## Checked` section. A lens report that records no checked surfaces is incomplete work: rerun that lens rather than accepting it.
- Phase 6s is complete only when `review<R>.md` exists with a `## Verdict:` line, a non-empty `## Coverage` section, and a `## Dropped` section.
- Phase 6b is complete only when the requested fixes were applied and the post-fix build outcome is known.
- Phase 6d is complete only when `test-design.md` exists, covers every surface
  the task's Observable result names (or marks one N/A with a reason), states
  a falsifiable oracle with its source for each check, and compresses the run
  plan to the fewest possible runs. It must not contain overlay code or filled
  Actual/Result fields.
- A perform-task visual design phase is complete only when `visual.md` cites its available
  design sources (images when supplied; otherwise request facts and repository/baseline anchors),
  records assumptions, and contains desktop anchors, an ordered derivation, tolerances, and
  falsifiable geometry checks. Missing mockups alone never make the phase incomplete.

## Phase 1: Context and Plan

One leaf gathers context and writes the implementation plan in the same
session: the agent that just read every relevant file is the best-informed
planner, and Phase 3 still verifies both artifacts independently. For a
`Visual: layout` task, insert the pipeline's visual design instructions
between the context and plan steps so the leaf writes `visual.md` before
`plan.md` and the plan consumes the derived contract.

Small-task fast path: the performer may run this phase as a same-session
checklist instead of a leaf, but only when the task spec itself names every
file to touch and the change is mechanical — roughly two source files or
fewer, no new APIs, strings, or style tokens, no layout derivation. When in
doubt, delegate. Phase 3 always runs as a fresh leaf and must reject the fast
path (`Fast-Path: rejected` under Status, no `Assessed: yes`) when the task
turns out larger than those criteria; the performer then reruns Phase 1 as a
proper leaf.

```text
You are a context-gathering and planning agent for a large C++ codebase (Telegram Desktop).

TASK: <TASK>

YOUR JOB: Read AGENTS.md, inspect the codebase, find all files and code relevant to this task, write self-contained implementation context, and then write a detailed implementation plan.

Steps:
1. Read AGENTS.md for project conventions and build instructions.
2. When `<PROJECT_FILE>` is not `none`, read it as the current durable project
   blueprint and preserve everything still accurate in the proposal.
3. Search the codebase for files, classes, functions, and patterns related to the task.
4. Read all potentially relevant files. Be thorough and prefer reading more rather than less.
5. For each relevant file, note:
   - file path
   - relevant line ranges
   - what the code does and how it relates to the task
   - key data structures, function signatures, and patterns used
6. Look for similar existing features that could serve as a reference implementation.
7. Check api.tl if the task involves Telegram API.
8. Check .style files if the task involves UI.
9. Check lang.strings if the task involves user-visible text.

Write `<WORK_DIR>/project.proposed.md` only when `<PROJECT_FILE>` is not
`none`. It is not used by the current task. Describe the project as if this
task is approved and fully working, so the performer can promote it only after
approval. Include:
- Project: What this project does (feature description, goals, scope)
- Architecture: High-level architectural decisions, which modules are involved, how they interact
- Key Design Decisions: Important choices made about the approach
- Relevant Codebase Areas: Which parts of the codebase this project touches, key types and APIs involved

Do not include temporal state like "Current State", "Pending Changes", "Not yet implemented", or "TODO". Describe the project as a complete, coherent whole.

Always write `<WORK_DIR>/context.md`.

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

After context.md is written, create a detailed plan in: <WORK_DIR>/plan.md

The plan.md should contain:

## Task
<one-line summary>

## Approach
<high-level description of the implementation approach. Name the closest
existing analogue in this repository for this kind of change and how the plan
follows its shape. One line per new file, class, switch, or abstraction the
plan introduces: the role boundary or constraint that requires it — and for a
new entry point, switch, or hook, which existing ones were checked and why
none fits. State where the change is contained: the fewest insertion points
into existing shared modules, with platform-specific work behind the
`Platform::` seam rather than inline conditional blocks.>

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

## Phase 1F: Context and plan for an existing project

```text
You are a context-gathering and planning agent for a follow-up task on an existing project in a large C++ codebase (Telegram Desktop).

NEW TASK: <TASK>

YOUR JOB: Read the existing project state, gather any additional context needed, produce fresh documents for the new task, and then write a detailed implementation plan.

Steps:
1. Read AGENTS.md for project conventions and build instructions.
2. Read <PROJECT_FILE>. This is the project-level blueprint describing everything done so far.
3. Read <PREVIOUS_CONTEXT>. This is the previous task's gathered context.
4. Understand what has already been implemented by reading the actual source files referenced in the project file and previous context.
5. Based on the new task description, search the codebase for any additional files, classes, functions, and patterns that are relevant to the new task but not already covered.
6. Read all newly relevant files thoroughly.

Write two files.

File 1: `<WORK_DIR>/project.proposed.md`

Write a single coherent proposed project document that describes everything,
including this task's changes, as fully implemented and working. Do not modify
`<PROJECT_FILE>` during this phase.

It should incorporate:
- everything from the existing project document that is still accurate and relevant
- the new task's functionality described as part of the project, not as a pending change
- any changed design decisions or architectural updates from the new task requirements

It should not contain:
- temporal state such as "Current State", "Pending Changes", or "TODO"
- history of how requirements changed between tasks
- references to "the old approach" versus "the new approach"
- task-by-task changelog or timeline
- information that contradicts the new task requirements

File 2: `<WORK_DIR>/context.md`

This is the primary document for the new task. It must be self-contained and should include:
- Task Description: The new task restated clearly, with enough project background that an implementation agent can understand it without reading other AI task files
- Relevant Files: Every file path with line ranges relevant to this task
- Key Code Patterns: How similar things are done in the codebase
- Data Structures: Relevant types, structs, classes
- API Methods: Any TL schema methods involved
- UI Styles: Any relevant style definitions
- Localization: Any relevant string keys
- Build Info: Build command and any special notes
- Reference Implementations: Similar features that can serve as templates

Be extremely thorough. Another agent with no prior context should be able to work from this file alone.

File 3: `<WORK_DIR>/plan.md`

After the two documents are written, create a detailed plan with the same
structure required by Phase 1: Task, Approach, Files to Modify, Files to
Create, numbered Implementation Steps grouped into phases when there are more
than about eight steps, Build Verification, and the Status checkbox section.

Do not implement code in this phase.
```

## Phase 1V: Context and Measurement Plan

For a `type: verify` task only. It replaces Phase 1, and Phase 3V replaces
Phase 3. Phases 4, 5, 6, and 7 do not run at all — there is no product diff to
implement, build, review, or normalize — so this plan and its assessment are the
entire front half of the run, and the test loop follows directly.

The small-task fast path never applies. A verification's difficulty is in its
oracle, not its size, and the one-check tasks are exactly the ones where a
plausible-looking oracle passes without touching the behavior.

```text
You are a measurement-planning agent for a large C++ codebase (Telegram Desktop).

TASK: <TASK>

This task VERIFIES behavior that already shipped. It has NO implementation of its own. You are not planning a change; you are planning a measurement that could come out either way.

YOUR JOB: Read AGENTS.md, find the shipped code under test, write self-contained context, and then write a measurement plan.

Steps:
1. Read AGENTS.md for project conventions and build instructions.
2. Read the task spec completely. It normally names the approved task that left this gap, quotes the shipped hunk, and says what was and was not observed. Treat those quotes as claims to re-derive, not as facts: open the cited files and confirm the code still reads that way on this branch.
3. Find every surface the claim touches: the function under test, its callers, the widget or API that exposes it, and the state that persists the result.
4. Establish where the truth lives. If the claim is about what the server stores, local editor or model state is not evidence. If it is about what a later session sees, in-process state right after the action is not evidence. Say explicitly, for each check, which side of that line it reads.
5. Find the fixture: the account, chat, message, document, or widget the measurement needs. Say whether it already exists (quote its identifiers from the task spec or the prior run's log) or must be created, and how you will tell.
6. Find a control — something that must come out DIFFERENT in the same run if the setup is sound. A sibling widget declared without the property under test, a context in which the behavior must not fire, a pre-change render recovered from git history, a negative case. A run with no control cannot distinguish "the behavior is correct" from "my probe never ran".

Always write `<WORK_DIR>/context.md`, self-contained, so an agent with no prior context can author the overlay from it plus the referenced sources. Include:
- Claim Under Test: the shipped behavior restated as one falsifiable proposition
- Relevant Files: every path with line ranges and what it does
- Where The Truth Lives: for each check, the surface that decides it and why local state does not
- Fixture: what the measurement needs and how it is obtained
- Reachability: how the surface is driven in a test process, with the entry point
- Prior Art: overlays, probes, and controls from related tasks that can be reused, with their tracked paths

Then write `<WORK_DIR>/plan.md`:

## Claim
<the single proposition under test, stated so that it can be false>

## Oracle
<what decides it, in literal terms: the values read, where they are read from, and the exact comparison>

## Checks
<numbered. For each: what is driven, what is read, the expected literal value, and THE FALSIFIER — the concrete observation that would make this check fail. A check whose falsifier you cannot name is not a check.>

## Controls
<what must come out different in the same run, and its expected value. State the failure reading: "if the control and the subject agree, the run has not reproduced the condition and is a test flaw, never a pass.">

## Fixture
<how it is obtained, and how the run proves it got the right one>

## Runs
<the fewest processes that can carry every check, and what forces a split — a fresh start to re-read persisted state, a different launch flag, a scale that must be set before the style pipeline runs>

## Scope Boundary
<the parent task and what its diff changed. Then, for each check above, one line: what reverting that diff would do to this check's outcome. A check nothing in the diff can affect does not belong in this plan.>

## Out Of Scope
<the neighbouring claims this task does not measure, named>

## Status
Phases: 1

Rules:
- Plan NO change to Telegram/SourceFiles or Telegram/Resources. The only code this task writes is the disposable test overlay. If satisfying the acceptance criteria seems to require a source change, stop and say so in the plan: the task is misrouted.
- You are measuring ONE change — the parent task's diff. Apply the revert test to every check you consider: if reverting that diff could not change the check's outcome, it is measuring pre-existing behavior and does not belong in this plan, however tempting it looks. A context that cannot reach the changed lines, a neighbouring feature the diff never touches, a pre-existing bug you spot while reading: name it under Out Of Scope and leave it. This codebase is far larger than the queue, and a verification that follows attention rather than the diff never finishes.
- Do not enumerate a parameter range the acceptance criteria did not name. Iterate fully over a range they DO name — every value of that enum, both halves of that branch — but do not add the other themes, the other wallpapers, the other scales or the sibling sections on your own initiative.
- Plan no repair. If you already suspect the behavior is wrong, plan the measurement that proves it, and say what you suspect under Out Of Scope. Fixing it is a separate task.
- Prefer reading a value over rendering a picture where both are available, and quote literal values rather than describing them. Where the acceptance asks for a visual judgement, plan the capture AND the numbers, because the judgement must be made against both.
```

## Phase 3V: Falsifiability Assessment

For a `type: verify` task only. It replaces Phase 3, and it also absorbs Step 6d:
the review loop does not run, so this is where the test-check design is written
and the only place the plan is independently checked.

```text
You are a measurement assessment agent. Review a verification plan before it is executed.

Read these files:
- <WORK_DIR>/context.md
- <WORK_DIR>/plan.md
- The task spec
- Then read the actual source files referenced, to verify the plan against the code rather than against its own prose.

This plan measures shipped behavior and carries no implementation. Assess it on one question above all others: WOULD THIS RUN HAVE DETECTED THE NEGATIVE? A verification that passes whether or not the behavior exists is worse than no verification, because it converts an open gap into a false record of coverage.

Assess:

1. Reachability: are the paths, functions, and entry points real on this branch? Does the plan's driving sequence actually reach the code under test, or does it reach a lookalike?
2. Oracle strength: for each check, would it still pass if the behavior under test were removed? Reject any check that would. Name the specific way each check can fail.
3. Truth surface: does each check read the surface that actually decides the claim? Reject reading local model or editor state where the claim is about persisted or server state, and reject reading in-process state where the claim is about what a later session sees.
4. Controls: is there something in the same run that must come out different? Verify the control's expected value independently from the plan's arithmetic. A plan whose control is derived from the same computation as the subject is not a control.
5. Fixture integrity: does the run prove it measured the intended subject, quoting its identifiers, rather than assuming it?
6. Coverage: does every acceptance criterion in the task spec map to a numbered check? List any that do not.
7. Scope: does the plan change any byte under Telegram/SourceFiles or Telegram/Resources outside the overlay, or repair anything? Both are rejections.
8. Scope boundary — the revert test. For every check, could reverting the parent task's diff change its outcome? Cut every check where the answer is no; it measures pre-existing behavior that this task does not own. Cut any parameter range the acceptance criteria never named, while keeping ranges they did name iterated in full. Report what you cut and why, so the boundary is on the record rather than silently redrawn. A plan that has grown past its parent's diff is the failure mode this check exists to catch.
9. Run count: can the checks share fewer processes than planned, and is each split justified by something that genuinely cannot share a process lifetime? Settle coverage first and pack second — never drop an in-scope check to save a run, and never defer one to a follow-up task that this checkout could take now.

Update plan.md with your refinements, keeping its structure. Strengthen weak oracles rather than deleting them. Where you reject a check, say what it would have missed.

Then write `<WORK_DIR>/test-design.md`: the checks as the test author will implement them, in run order, each with its markers, the literal expected values, and its falsifier. This replaces the Step 6d draft, which does not run for this task type.

Add to the Status section of plan.md:
- `Phases: 1`
- `Falsifier: named` only when every check has one
- `Assessed: yes` at the bottom, only when both hold

Do not author overlay code and do not implement anything in this phase.
```

## Phase 3: Plan Assessment

Assessment has two rejection outcomes besides refinement, and both withhold
`Assessed: yes` and send the performer back to a fresh Phase 1 leaf:
`Fast-Path: rejected` when the performer's same-session Phase 1 undersized the
task, and `Approach: rejected` when the plan is over-engineered or over-coupled
beyond step-level repair. On an approach rejection the performer appends the
assessor's named simpler direction to the Phase 1 rerun prompt.

```text
You are a plan assessment agent. Review and refine an implementation plan.

Read these files:
- <WORK_DIR>/context.md
- <WORK_DIR>/plan.md
- <WORK_DIR>/visual.md when it exists
- Then read the actual source files referenced to verify the plan makes sense.

Assess the plan:

1. Correctness: Are the file paths and line references accurate? Does the plan reference real functions and types?
2. Completeness: Are there missing steps? Edge cases not handled?
3. Code quality: Will the plan minimize code duplication? Does it follow existing codebase patterns from AGENTS.md?
4. Approach fit — judge this adversarially, as the reviewer who must later
   defend the diff. Anchor on precedent: find the closest existing feature of
   the same kind in this repository and compare shapes; when the plan's
   Approach names no analogue, find one yourself and record it. A plan several
   times the size or spread of its precedent carries the burden of proof.
   - Existing mechanism first: before the plan adds a new entry point, switch,
     flag, hook, or IPC path, search for an existing one whose semantics
     already fit. Riding an existing mechanism is the strongest simplification
     this assessment can produce, and it can carry benefits a new surface
     cannot — code already shipped may already invoke it.
   - Containment: count the plan's insertion points into existing shared
     modules. A feature woven through many functions of a global module —
     platform-specific `#ifdef` blocks or feature-mode conditionals scattered
     inline through cross-platform code — is the disaster shape even when
     every hunk is small. Platform work belongs behind the `Platform::` seam;
     a mode belongs in one contained flow, not in special cases threaded
     through everything the module already did.
   - Structure must be load-bearing: for each new file, class, abstraction, or
     indirection, the plan must name what it enables — a second caller, a
     second platform, a real layering constraint. A seam with one
     implementation and no second user, a state machine over a linear flow, a
     wrapper around a single call, and scaffolding for a testing style this
     repository does not practice serve no purpose and come out of the plan.
   - New files are not the problem: a focused file bounding a coherent role
     beats both growing a mega-module and scattering through one; judge
     whether the boundary does work, not whether it is new.
5. Phase sizing: Each phase should be implementable by a single agent in one session. If a phase has more than about 8-10 substantive code changes, split it further.
6. Visual contract (layout tasks): when visual.md exists, verify its anchors
   are real (the cited style tokens, fonts, and reference widgets exist),
   the ordered derivation is arithmetically consistent, and every quantity the
   plan uses comes from the contract rather than an invented number.
7. Fast-path sizing (when the performer wrote context.md and plan.md itself):
   confirm the task really matches the fast-path criteria — the spec names
   every file to touch, roughly two source files or fewer, no new APIs,
   strings, or style tokens, no layout derivation. If it does not, add
   `Fast-Path: rejected` to the Status section, do NOT add `Assessed: yes`,
   and state what was underestimated; the performer must rerun Phase 1 as a
   fresh leaf.
8. Approach rejection: when item 4 fails structurally — the approach is
   several times larger, more scattered, or more coupled than the task
   warrants and trimming individual steps would not fix it — do not refine
   the plan. Add `Approach: rejected` to the Status section, do NOT add
   `Assessed: yes`, and state in 2-3 lines the simpler direction: the
   precedent or existing mechanism to ride on, which existing files absorb
   the change, and where it stays contained. The performer reruns Phase 1 as
   a fresh leaf with those lines as input.

Update plan.md with your refinements. Keep the same structure but:
- fix any inaccuracies
- add missing steps
- remove files, abstractions, and steps the task does not need — deletion is
  as much a refinement as addition
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
- <WORK_DIR>/context.md
- <WORK_DIR>/plan.md

Then read the source files you will be modifying.

Your owned write set for this phase:
<OWNED_WRITE_SET>

YOUR TASK: Implement only Phase <N> from the plan:
<paste the specific phase steps here>

Rules:
- Follow the plan precisely.
- Follow AGENTS.md coding conventions.
- You are not alone in the codebase. Respect existing changes and do not revert unrelated work.
- Do not modify AI task files except the Status section in plan.md and the matching
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
- <WORK_DIR>/context.md
- <WORK_DIR>/plan.md
- .agents/shared/build-lock-recovery.md

The implementation is complete. Your job is to build the project and fix any build errors that block the planned work.

Steps:
1. On native Windows, run the recovery contract's exact-path proactive cleanup.
2. Run the resolved Debug build command from context.md (`<BUILD>`) at the repository root. On WSL
   this is the repository Docker entry point; do not run native Windows CMake against that tree.
3. If the build succeeds, update plan.md: change `- [ ] Build verification` to `- [x] Build verification`
4. If the build fails:
   a. Read the error messages carefully
   b. Read the relevant source files
   c. Fix the errors in accordance with the plan and AGENTS.md conventions
   d. Rebuild and repeat until the build passes
   e. Update plan.md status when done

Rules:
- Only fix build errors. Do not refactor or improve code beyond what is needed for a passing build.
- Follow AGENTS.md conventions.
- If the build fails with C1041, LNK1104, "cannot open output file", or a similar
  access-denied lock, follow the shared bounded recovery contract. Do not edit
  source to work around an environment lock.
- You are not alone in the codebase. Respect existing changes and do not revert unrelated work.

When finished, report the build result and which files, if any, you changed.
```

## Phase 6: Code Review Loop

After build verification passes, run up to 3 review-fix iterations. Set iteration counter `R = 1`.

Each iteration runs four independent review lenses over the same diff, then one
synthesis pass that produces the single `review<R>.md` the fix phase consumes. The
lenses never read each other's reports: independence is what makes their agreement
evidence rather than an echo. Their write sets are disjoint (one report file each),
so they may run in parallel; when delegation is unavailable, run them as sequential
checklists in the current session and keep the same four reports.

**Check the diff before spawning anything.** If `git status --porcelain` over
`Telegram/SourceFiles` and `Telegram/Resources` is empty, skip Phase 6 entirely:
record in progress that there was no product diff to review, run no lens, no
synthesis and no fix pass, and continue to the next phase. Four lenses over nothing
cost real time and produce no evidence, and a lens that goes looking for substitute
material reviews the plan and the fix pass then rewrites it, so the loop reviews its
own output and cannot converge. This applies to any task that reaches Phase 6 with
no product change — a `type: verify` task, or an ordinary task whose request turned
out to be already satisfied by shipped code. On iteration 1, still run the Phase 6d
test-design leaf: it reads the spec and plan, takes no part in the review verdict,
and is useful whether or not a diff exists.

Likewise, never schedule iteration `R+1` off a fix pass that touched no product
source. A fix that changed nothing under `Telegram/SourceFiles` or
`Telegram/Resources` leaves the reviewed surface identical, so another round can only
re-read the same code or drift onto AI artifacts. Close the loop and continue.

The lenses are:

| Lens | Report file | Angle |
| --- | --- | --- |
| `correctness` | `review<R>-correctness.md` | Does it do the specified thing, on every path it touches |
| `lifetime` | `review<R>-lifetime.md` | Ownership, object lifetime, re-entrancy, threading |
| `reuse` | `review<R>-reuse.md` | Duplication of what the repository already has |
| `structure` | `review<R>-structure.md` | Placement, minimality, dead code, conventions |

Review loop:

```text
LOOP:
  1. Run the scheduled Phase 6a lenses for iteration R.
     R = 1     -> all four lenses, plus the Phase 6d test-design leaf in the
                  same fan-out (it writes test-design.md and takes no part in
                  the review verdict).
     R > 1     -> every lens whose finding survived synthesis in iteration R-1,
                  plus `lifetime` unconditionally. A fix pass is the most likely
                  moment for a new ownership or lifetime error to be introduced,
                  so that lens never goes unrun over changed code.
  2. Run synthesis phase 6s with iteration R. It writes review<R>.md.
  3. Read review<R>.md verdict:
     - "APPROVED" -> go to FINISH
     - "NEEDS_CHANGES" -> run fix phase 6b
  4. After fix work completes and build passes:
     R = R + 1
     If R > 3 -> go to FINISH
     Otherwise -> go to step 1

FINISH:
  - Update plan.md: change `- [ ] Code review` to `- [x] Code review`
  - Proceed to Phase 7 on native, non-WSL Windows; otherwise proceed to Completion
```

### Step 6a: Review lenses

Every lens prompt is the shared preamble below, then its own Angle section, then the
shared report contract. Replace `<LENS>` with the lens name and `<R>` with the
iteration.

#### Shared lens preamble

```text
You are one of four independent code-review lenses for Telegram Desktop (C++ / Qt).
Your lens is <LENS>, iteration <R>.

Other lenses cover the other angles. Do not review outside your assigned angle, and
never soften or skip a finding because another lens might also catch it — you cannot
see their reports and they cannot see yours.

Read these files:
- <WORK_DIR>/context.md
- <WORK_DIR>/plan.md
- AGENTS.md
- REVIEW.md
- If R > 1, also read <WORK_DIR>/review<R-1>.md for what the previous iteration
  already required, so you do not re-file a fix that has since been applied.

Then run `git diff` to see the current uncommitted changes for this task, and read
every modified source file IN FULL. The diff hides the context a change lands in,
and most real defects are visible only in that context.

Review only what this task changed. A pre-existing problem outside this task's diff
is not a finding, however tempting.

STOP FIRST if the product diff is empty. Your review material is the product diff
and nothing else. If `git diff` and `git status --porcelain` over
`Telegram/SourceFiles` and `Telegram/Resources` show no change, write a report whose
Checked section states exactly that, give the verdict CLEAN, and stop. Do not
substitute other material: the plan, the context, the task text, the test design,
the source-history note, the test overlay, and already-shipped code are NOT your
review surface, and reviewing them is a defect in the review, not thoroughness. An
empty diff is a complete answer — it means this task changed no product code, which
is normal for a verification task and for a request that turned out to be already
satisfied. It is never a reason to go looking for something to review.

Default to NOT CLEAN whenever there IS a diff. A clean verdict is then a positive
claim that you looked at each surface and found nothing — not the absence of an
objection. You must report what you checked, and a report whose Checked section does
not account for the diff is incomplete work, not a fast approval. This default does
not apply to the empty-diff case above; there, CLEAN is the correct and only answer.

A finding is admissible only if you can state the concrete failure it produces: the
specific input, state, or call sequence that yields a crash, a wrong result, or a
maintenance cost a future reader pays. "Could be cleaner", "consider extracting",
and "might be a problem" are not findings. If you cannot name the failure, drop it.
Do not suggest comments or docstrings.
```

#### Angle: `correctness`

```text
ANGLE — behavior and correctness. Does the change produce the behavior the task and
plan specify, on every path it touches?

- Logic errors: inverted conditions, wrong operator, off-by-one, wrong variable.
- Missing branches: early return, empty or absent data, a failed or cancelled
  network reply, the not-found case, the zero and one-element cases.
- State left inconsistent when an operation fails partway through.
- Behavior in adjacent code paths the diff touches but the task did not intend to
  change.
- Values crossing an API boundary — network, settings, a peer or session lookup —
  used without checking what the boundary can actually return.
- Obviously pathological work in a hot path: per-frame paint, resize, scroll, or a
  loop over every message or dialog.
```

#### Angle: `lifetime`

```text
ANGLE — lifetime, ownership and safety. Will this crash, and who owns what?

- Use-after-free and dangling references: a reference or pointer to a temporary, or
  into a container that can reallocate or rehash before the next use.
- Ownership: for every object the change introduces or stores, name the owner and
  confirm the owner outlives every user. Watch widget parent/child ownership, raw
  pointers escaping the scope that owns them, and `not_null` invariants that the
  change can now violate.
- Reactive and callback lifetimes: every subscription bound to a lifetime that dies
  with everything its handler dereferences; captures that can be destroyed before
  the callback runs; guarded-callback and weak-pointer use where the target can go
  away.
- Re-entrancy and destruction order: can this run while something it dereferences is
  being destroyed; can a handler destroy the object that invoked it.
- Thread assumptions: main-thread-only APIs reached from another thread; data shared
  across threads without synchronization.
- Iterator or reference invalidation across a mutation of the container.
```

#### Angle: `reuse`

```text
ANGLE — duplication and reuse. Does the repository already have this?

This lens is not satisfied by reading the diff. Reuse lives OUTSIDE the diff, which
is exactly why a single generalist reviewer misses it. Search the repository before
you judge anything.

- For each helper, widget, algorithm, constant, string, style value, entry
  point, or command-line switch the change introduces: search for an existing
  equivalent, then either use it or state in your report why the existing one
  does not fit. Report the search you ran.
- Logic repeated within the change itself that should be shared.
- A reimplementation of an established repository pattern instead of following it —
  the strongest finding this lens produces, and the one that compounds worst if it
  ships.
- New style values that duplicate an existing token; new localization strings that
  duplicate an existing key.
```

#### Angle: `structure`

```text
ANGLE — structure, simplicity and conventions. Does it read like the rest of the
codebase, and is it no bigger than it needs to be?

- Placement: each piece lives in the module it logically belongs to. Flag module
  structure only when a large new chunk clearly belongs elsewhere.
- Decomposition: an extracted helper that would CLEARLY improve readability. Do not
  file marginal splits.
- Minimality: diff hunks with no functional effect; changes broader than the task
  requires; abstraction, indirection, validation, or error handling for cases that
  cannot happen; a compatibility shim or flag where the code can simply change.
- Scatter: task or platform logic threaded through many existing functions of a
  shared module. Platform-specific `#ifdef` blocks inline in cross-platform code
  where a `Platform::` seam exists, or a mode's special cases woven through a
  module's existing flow instead of one contained hook, are findings even when
  each hunk is small.
- Load-bearing structure: every new file, class, or seam needs a nameable
  enabling purpose — a second user, a second platform, a real layering
  constraint. An interface with one implementation, a state machine over a
  linear flow, a wrapper around a single call, or scaffolding for a testing
  style this repository does not practice is structure without purpose. A
  focused new file bounding a coherent role is not a finding — that beats
  growing a mega-module.
- Dead code: anything added or left behind that nothing reaches.
- Conventions: REVIEW.md mechanical rules and AGENTS.md coding conventions.
- Local idiom: comment density, naming, and construction match the surrounding code.
```

#### Shared lens report contract

```text
Write your report to <WORK_DIR>/review<R>-<LENS>.md. Do not write review<R>.md —
the synthesis phase owns that file.

## Lens: <LENS> — iteration <R>

## Checked
<One line per surface you examined and cleared: the file and function, and what you
verified about it under THIS angle. This section is the evidence behind a clean
verdict, and it is required even when you do have findings — list everything you
cleared alongside them. For the reuse lens, include the searches you ran.>

## Findings
<Omit the section entirely when you have none.>

### <short title>
- File(s): <paths, with line references>
- Failure: <the concrete input, state, or sequence -> the crash, wrong result, or
  maintenance cost it produces>
- Fix: <the specific change to make>
- Confidence: <high | medium | low>

## Verdict: <CLEAN or FINDINGS>

Reply in the compact block. Do not restate the diff or the report body in chat.
```

### Step 6d: Test-check design (iteration 1 only)

The checks a test must make derive from the task spec and the plan, not from
the last review fix — so their design does not have to wait for the review
loop to finish. Spawn this leaf together with the iteration-1 lenses. It
writes `test-design.md` only; the later test author owns `test.md` and the
overlay, and MUST reconcile every drafted check against the final retained
diff before authoring code — a review fix can change what a check must
observe, and an unreconciled draft is a TEST_FLAW waiting to happen.

```text
You are the test-check designer for one Telegram Desktop task. You design
falsifiable checks. You do not write overlay code, do not run anything, and
do not modify source files.

Read these files:
- the task spec at <TASK_DIR>/task.md and every referenced input image
- <WORK_DIR>/context.md
- <WORK_DIR>/plan.md
- <WORK_DIR>/visual.md when it exists
- .agents/shared/test-loop.md — the sections "Design the tests from THIS
  task", "Visual contract", and "Test report"
- the harness headers under Telegram/SourceFiles/test/ (test_runner.h,
  test_widgets.h, test_capture.h, test_log.h) — design checks that the
  harness's stage waits, typed finders, tight captures, and geometry logs can
  observe directly

Then run `git diff` to see the current uncommitted task changes.

Write <WORK_DIR>/test-design.md:
- the chosen test strategy (live-data / live-mutate / inject / mock-api) with
  one line of justification
- one `#### Test N — <aspect of THIS change>` block per concrete thing the
  diff changed and per surface the task's Observable result names, each with
  Expected / Oracle / Oracle source / Observed via fields in the test.md
  format, leaving Actual, Screenshots, and Result unfilled
- a surface explicitly marked N/A with a reason when it genuinely cannot be
  observed
- a run plan compressed to the fewest possible runs — normally exactly one —
  splitting only for checks that cannot share one process lifetime
- a fixture fallback plan naming at least two progressively more direct ways
  to reach the changed production seam if the preferred fixture does not
  materialize; do not make unrelated UI setup a single point of failure
- a `## Reconcile` line reminding the test author to re-verify every check
  against the final retained diff after the review loop

Every check needs an oracle that can come out FAIL. "The screen opened" is
not a check. Do not reuse a generic navigate-and-screenshot scenario.
```

## Test-Flaw Recovery And Directness

Use this prompt for every `TEST_FLAW` recovery. On a repeated signature, use a
fresh leaf and include every prior recovery plan. Do not ask it to "try again"
or merely repair the same fixture.

```text
You are a test-recovery agent for one Telegram Desktop task. The product
implementation is retained; repair only the disposable test overlay and its
test report. You are a leaf and must not delegate.

Read:
- the task spec and final retained task diff
- <WORK_DIR>/test-design.md
- <WORK_DIR>/test.md, including every prior Run and Recovery plan
- <WORK_DIR>/test-overlay.paths and the current saved overlay
- .agents/shared/test-loop.md, especially the repeated-failure directness ladder

The latest failure signature is:
<FAILURE_SIGNATURE>

The previous fixture/recovery techniques are forbidden:
<FORBIDDEN_TECHNIQUES>

Before editing, append a Recovery plan to the pending Run in test.md:
- Prior proof: the positive facts already established by earlier runs
- Failed assumption: the exact setup predicate or fixture behavior that failed
- Forbidden technique: the approach you will not repeat
- New directness strategy: the next unused ladder strategy
- Reachability: why this strategy reaches the task's changed production code
  even if the failed setup never succeeds

Then implement the recovery. Prefer fewer assumptions and more manual control:
- insert the production object by hand through an established data API;
- add a narrow inventoried _DEBUG seam and call the real changed
  collector/handler directly when unrelated UI setup is flaky;
- place that seam in any relevant tracked production file or initialized
  submodule when centralizing it in test_scenario.cpp adds indirection;
- inject the exact server response/error at the callback or transport seam for
  retry behavior;
- use a real disposable account fixture or exact Qt input only when those are
  part of the behavior under test.

Keep an independent falsifiable oracle. A direct seam may bypass setup outside
the task diff, but it must not reimplement or bypass the changed code itself.
Fix every test flaw visible in the latest run together, build Debug, and return
the compact phase block.

If no unused strategy can safely reach the changed code, do not cite the
repeated signature as the reason. Add `## Recovery exhaustion` to test.md with
one row per ladder strategy: attempted evidence, or the concrete reason it is
unsafe, unavailable, or would bypass the task diff. Return `BLOCKED` for the
performer to confirm independently.
```

### Step 6s: Review synthesis

```text
You are the review synthesizer for iteration <R>. Four independent lenses reviewed
the same task diff. Produce the single review<R>.md that the fix phase implements.

Read every <WORK_DIR>/review<R>-*.md that exists for this iteration, plus
<WORK_DIR>/context.md, <WORK_DIR>/plan.md, REVIEW.md, and the task diff itself.

You are the last filter before work is created, and you are adversarial toward the
findings, not toward the code. Apply in order:

1. Merge duplicates. Several lenses may describe one underlying defect from
   different angles. Keep one entry, with the clearest failure statement, and pick
   the category that matches the actual defect.
2. Confirm each finding against the code yourself. Do not take a lens at its word.
   A finding whose failure statement you cannot reproduce by reading the diff and
   the surrounding file is DROPPED, whatever confidence the lens claimed.
3. Drop findings that fall outside this task's diff.
4. Order what survives by impact: crashes and wrong behavior first, then duplication
   and placement, then minimality and conventions.

Verdict rule: NEEDS_CHANGES if any finding survives, otherwise APPROVED. An APPROVED
verdict must carry the merged Checked coverage from every lens that ran — that
coverage is the evidence for approval, and an APPROVED review without it is
incomplete work.

Write <WORK_DIR>/review<R>.md:

## Code Review - Iteration <R>

## Summary
<1-2 sentence overall assessment>

## Coverage
<Merged Checked lines from every lens that ran this iteration, grouped by lens.>

## Verdict: <APPROVED or NEEDS_CHANGES>

If the verdict is NEEDS_CHANGES, continue with:

## Changes Required

### <Issue 1 title>
- Category: <correctness | lifetime | duplication | wrong placement | function decomposition | module structure | dead code | minimality | style>
- File(s): <file paths>
- Problem: <clear description, including the concrete failure>
- Fix: <specific description of what to change>

## Dropped
<Each finding you dropped and why, or `none`. This is how the next iteration knows
not to re-file it.>

When finished, report your verdict clearly as: APPROVED or NEEDS_CHANGES.
```

### Step 6b: Review Fix

```text
You are a review fix agent. You implement improvements identified during code review.

Read these files:
- <WORK_DIR>/context.md
- <WORK_DIR>/plan.md
- <WORK_DIR>/review<R>.md
- .agents/shared/build-lock-recovery.md

Then read the source files mentioned in the review.

YOUR TASK: Implement all changes listed in review<R>.md.

Rules:
- Implement exactly the review changes, nothing more.
- Follow AGENTS.md coding conventions.
- You are not alone in the codebase. Respect existing changes and do not revert unrelated work.
- Your write set is product source under `Telegram/SourceFiles` and `Telegram/Resources`, and
  nothing else. Never edit `plan.md`, `context.md`, `test-design.md`, the task text, or any other
  AI artifact. Those are inputs you read, never outputs you write. Editing them makes the next
  review iteration read a document this loop just rewrote, which cannot converge.
- If a finding cannot be addressed by changing product source — because it is about the plan, the
  test design, the task's scope, or code this task did not touch — do not act on it. Report it
  back as out of scope, naming the finding and why, and let the performer decide.
- If review<R>.md contains no finding you can act on under those rules, change nothing, say so,
  and report. An empty fix is a valid and complete outcome.

After all changes are made:
1. On native Windows, run the recovery contract's exact-path proactive cleanup.
2. Run the resolved Debug build command from context.md (`<BUILD>`) at the repository root.
3. If the build fails, fix build errors and rebuild until it passes.
4. If the build fails with C1041, LNK1104, "cannot open output file", or a
   similar access-denied lock, follow the shared bounded recovery contract.

When finished, report what changes were made and which files you touched.
```

## Phase 7: Native-Windows Text Normalization

Run this phase only in a native, non-WSL Windows checkout and only after the review loop has
finished. Keep WSL/Linux text LF/no-BOM.

Use the current task's result logs as the source of truth for what Codex touched. Do not sweep the whole repo and do not rewrite unrelated files from a dirty worktree.

```text
You are performing the final native-Windows-only text normalization phase for perform-task.

Read these files:
- <WORK_DIR>/plan.md
- <WORK_DIR>/logs/phase-4*.result.md
- <WORK_DIR>/logs/phase-5*.result.md
- <WORK_DIR>/logs/phase-6*.result.md

Your job:
- Collect the union of repo file paths listed in the exact `TOUCHED:` fields in those result logs.
- Keep only files inside the repository that currently exist and are textual project files: source, headers, build/config files, localization files, style files, and similar text assets.
- Exclude `out/`, binary files, and unrelated user files that were not touched by Codex in this task.
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
1. Write `<WORK_DIR>/logs/phase-7-line-endings.result.md`
2. Include:
   - whether the phase completed
   - which files were normalized
   - which files were skipped and why
   - whether any UTF-8 BOMs were removed or verified absent
   - any failures that need to be mentioned in the final summary
```

## Completion

When all phases, including build verification, code review, and Windows line ending normalization when applicable, are done:
1. Read the final `plan.md` and prepare the compact performer result.
2. Show which files were modified or created.
3. Note any issues encountered during implementation.
4. Summarize the code review iterations: how many rounds, what was found and fixed, or whether it was approved on the first pass.
5. On native, non-WSL Windows, mention the text-normalization result briefly: which project files were normalized, whether any BOMs were removed, or whether nothing needed changes.
6. Calculate and display the total elapsed time since `$START_TIME` (format as `Xh Ym Zs`, omitting zero components).
7. Include the full task id and project slug, when any, so later follow-ups can
   be routed without relying on session memory.

## Error Handling

- If any phase fails or gets stuck, follow the host-specific retry rules above. On Codex, do not close an agent solely because the final artifact is missing while its progress file is still advancing. For Phase 1, Phase 3, Phase 4, and Phase 6, do not rerun locally after delegated retries fail; ask the user instead.
- If `context.md` or `plan.md` is not written properly by a phase, rerun that phase in a fresh subagent with more specific instructions.
- If build errors persist after the build phase's attempts, report the remaining errors to the user.
- If a review-fix phase introduces new build errors that it cannot resolve, report to the user.

## Prompt Delivery And Logs

For each phase:
1. Write the full prompt to `<WORK_DIR>/logs/phase-<phase-name>.prompt.md`
2. Delegate by sending that prompt text to a fresh subagent, or use it as a same-session checklist only for the designated main-session phases or when delegation was unavailable from the start
3. For delegated phases, expect a matching `<WORK_DIR>/logs/phase-<phase-name>.progress.md` heartbeat while work is in flight
4. Save `<WORK_DIR>/logs/phase-<phase-name>.result.md` with `STATUS:`, `ARTIFACTS:`,
   `TOUCHED:`, `BLOCKER:`, and `NOTES:` fields.

For review iterations, include the iteration and the lens in the file name, for example:
- `phase-1-context-plan.prompt.md`
- `phase-6a-review-1-correctness.prompt.md`
- `phase-6a-review-1-correctness.result.md`
- `phase-6a-review-1-lifetime.prompt.md`
- `phase-6a-review-1-reuse.prompt.md`
- `phase-6a-review-1-structure.prompt.md`
- `phase-6d-test-design-1.prompt.md`
- `phase-6d-test-design-1.result.md`
- `phase-6s-synthesis-1.prompt.md`
- `phase-6s-synthesis-1.result.md`
- `phase-6b-fix-1.prompt.md`
- `phase-6b-fix-1.result.md`

## Subagent Pattern (Claude Code)

1. Write the phase prompt file(s).
2. Make one synchronous foreground Agent call per leaf — parallel calls in a
   single message for independent leaves of the same step — with self-contained
   prompts.
3. When the calls return, validate the expected artifacts or code changes with
   small shell summaries and the completion checks above.
4. Write the result log from the validated outcome and the compact reply block.

## Subagent Pattern (Codex)

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
