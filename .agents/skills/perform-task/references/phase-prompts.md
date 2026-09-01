# Telegram Task Phase Prompts

## Contents

- [Orchestration rules](#orchestration-rules)
- [Completion checks](#artifact-based-completion-checks)
- [Context and plan](#phase-1-context-and-plan)
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

- When delegation is available, use a fresh subagent for Phase 1 (context and plan), Phase 3, each Phase 4 implementation unit, the initial Phase 6 general review, each of the five standard lens reviews, each Phase 6 fix, each focused re-review, and any convergence assessment. Do not switch those phases to same-session midstream because of a timeout or missing artifact.
- Start the initial mandatory general reviewer and all five lens reviewers together when capacity permits. Under a slot limit, queue the complete set and start the next lens as a slot opens; do not let general review select or prune it. Each lens independently reads the task and complete diff, then either proves `NOT_APPLICABLE` compactly or performs the relevant full review. No reviewer sees another's findings. When the host supports continuing the saved general-review agent, send synthesis back to that agent; otherwise use one fresh synthesis agent with the saved general report and all five lens reports instead of making it rediscover the whole review.
- After a fix, use the focused re-review prompt: one mandatory general reviewer over the fix and affected invariants, plus only lenses whose blocker or prior `NOT_APPLICABLE`/`CLEAN` proof the fix invalidated. Carry every other approval forward.
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
- Spawn the independent leaves of one step — the five initial Phase 6 lenses,
  or assessed-disjoint Phase 4 units —
  as parallel Agent calls in a single message so they run concurrently.
- If a returned leaf fails its completion check, retry that disposable phase
  once in a fresh Agent with more specific instructions before stopping to
  ask the user.

### Grok Build: blocking spawn, depth one

- Follow `.grok/ai-workflow-adapter.md`. Its substitutions win over the
  Codex wait ladder and over any prompt that assumes nested delegation.
- When this session is a top-level `/perform-task`, run each leaf as one
  blocking `spawn_subagent` (`background: false`). The call returning is
  the completion signal; validate the artifact checks below on return.
- Spawn the independent leaves of one step — the five initial Phase 6 lenses,
  or assessed-disjoint Phase 4
  units — as parallel `spawn_subagent` calls in a single message.
- When this session is a `/continue` child, do not call `spawn_subagent`.
  Run every phase as a same-session checklist. That is the supported
  depth-1 fallback, not a retry.
- If a returned leaf fails its completion check, retry that disposable
  phase once in a fresh `spawn_subagent` with more specific instructions
  before stopping to ask the user.

### Codex: asynchronous spawn and wait

- Store the canonical target returned by `spawn_agent`.
- After the initial general reviewer finishes pass 1, keep its canonical target.
  When specialists finish, use `followup_task` on that target with the pass-2
  synthesis prompt instead of spawning a second complete-diff reviewer.
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
- Phase 3 is complete only when `plan.md` contains both `Phases:` in the Status section and `Assessed: yes`, records a rejection outcome (`Fast-Path: rejected` or `Approach: rejected`) that sends the performer back to a fresh Phase 1 leaf, or records `Scope: split-required` and has a complete `split-proposal.md` that stops source work for queue rescoping.
- Phase 4 is complete only when the target phase checkbox changed to checked and the touched-file list matches the owned write set, or the blocker explains any mismatch.
- Phase 5 is complete only when the build outcome is known and the build checkbox is updated on success.
- An initial Phase 6 lens is complete only when all five `review1-<lens>.md` reports exist with `## Verdict: NOT_APPLICABLE | CLEAN | FINDINGS` and a non-empty `## Checked` section. `NOT_APPLICABLE` must tie its proof to the complete diff; `CLEAN`/`FINDINGS` must name the relevant full files and adjacent surfaces reviewed.
- Initial Phase 6 general review is complete only when `review1-general.md` and `review1.md` exist with a `## Verdict:` line, a non-empty `## Coverage` section, all five lens reports are accepted or an unsupported bailout has been rerun, and every evidence check is reconciled against the actual diff.
- A focused Phase 6 review is complete only when `review<R>-focused.md` and `review<R>.md` name the fix paths and invariants, account for every carried-forward and invalidated approval, account for every rerun specialist, and reconcile only invalidated evidence checks. A convergence assessment is complete only with one exact disposition from the pipeline and its required bounded next action or split proposal.
- Phase 6b is complete only when the requested fixes were applied and the post-fix build outcome is known.
- Phase 3 is additionally incomplete until `test-design.md` exists, covers
  every acceptance surface, assigns each check a direct instrument, oracle,
  control or negative, and durable evidence, and records why a Telegram build,
  launch, UI driver, or screenshot is selected or omitted. It must not contain
  implementation or filled Actual/Result fields.
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

## Expected Surfaces
- each surface this task is likely to touch, and the escalation it implies
- any task-specific domain risk that fits none of the standard lenses

## Evidence Plan
- one preliminary check per acceptance criterion: changed surface, instrument,
  oracle, control or negative, and durable evidence
- which checks require a Telegram build, app launch, portable account,
  Computer Use, or screenshot, and why
- escalation triggers that would add a specialist or stronger instrument

## Status
- [ ] Phase 1: <name>
- [ ] Phase 2: <name> (if applicable)
- [ ] Pre-review validation
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

## Phase 3: Plan Assessment

Assessment has two approach rejection outcomes besides refinement, and both
withhold `Assessed: yes` and send the performer back to a fresh Phase 1 leaf:
`Fast-Path: rejected` when the performer's same-session Phase 1 undersized the
task, and `Approach: rejected` when the plan is over-engineered or over-coupled
beyond step-level repair. On an approach rejection the performer appends the
assessor's named simpler direction to the Phase 1 rerun prompt. A third outcome,
`Scope: split-required`, means the request itself contains several independently
useful and testable product boundaries; it writes `split-proposal.md` and stops
before source edits instead of trying another plan for the same task. This
independent assessor has veto authority because it is the first phase with the
exact implementation and evidence plan. It does not create, retire, supersede,
or rewrite queue tasks; the performer preserves the proposal and returns it to
the scheduler, which owns any rescope transaction.
The performer does not leave this as an unpublished `in-progress` marker: after
validating the proposal it writes the split result and calls
`finish --status split-required`, preserving any owned source state.

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
5. Expected surfaces: record the surfaces this task is likely to touch and the
   escalations they would imply, as a recall note for the reviewers. Do not
   select or omit standard lenses here — all five independently scan the task
   and complete diff, then decide their own applicability.
6. Evidence design: map every acceptance criterion and material shipped risk to
   the most direct practical instrument that can detect the negative. Allow
   static readings, commands/artifacts, unit tests, a standalone probe or
   component, Telegram logs/overlay, Computer Use and screenshots in any
   necessary combination. Apply the revert test and remove unrelated checks.
   Preserve deep app testing when behavior lives in Telegram, and require tight
   captures for visible claims. Do not require Telegram for an isolated probe or
   build-stage claim it cannot strengthen.
7. Phase sizing: Each phase should be implementable by a single agent in one
   session. If a phase has more than about 8-10 substantive code changes, split
   the phase. Then assess the task as a whole: several well-sized phases do not
   make an intrinsically broad task cohesive.
8. Visual contract (layout tasks): when visual.md exists, verify its anchors
   are real (the cited style tokens, fonts, and reference widgets exist),
   the ordered derivation is arithmetically consistent, and every quantity the
   plan uses comes from the contract rather than an invented number.
9. Fast-path sizing (when the performer wrote context.md and plan.md itself):
   confirm the task really matches the fast-path criteria — the spec names
   every file to touch, roughly two source files or fewer, no new APIs,
   strings, or style tokens, no layout derivation. If it does not, add
   `Fast-Path: rejected` to the Status section, do NOT add `Assessed: yes`,
   and state what was underestimated; the performer must rerun Phase 1 as a
   fresh leaf.
10. Approach rejection: when item 4 fails structurally — the approach is
   several times larger, more scattered, or more coupled than the task
   warrants and trimming individual steps would not fix it — do not refine
   the plan. Add `Approach: rejected` to the Status section, do NOT add
   `Assessed: yes`, and state in 2-3 lines the simpler direction: the
   precedent or existing mechanism to ride on, which existing files absorb
   the change, and where it stays contained. The performer reruns Phase 1 as
   a fresh leaf with those lines as input.
11. Intrinsic scope: decide whether one fresh reviewer and one coherent
   evidence campaign can judge the final retained result. Strong split signals
   are multiple parts with their own useful outcome and oracle; a stable
   dependency order where a later part can consume an earlier approved part;
   separate network, persistence, concurrency/ownership, engine, lifecycle, or
   UI boundaries; materially different fixtures or platforms; or a diff too
   broad for one reviewer to reason about as one invariant set. Phase count,
   changed-file count, and acceptance count are warning signals, not automatic
   thresholds. Keep inseparable API-plus-only-caller changes together.

   When this fails because of the request rather than the proposed approach,
   add `Scope: split-required` to Status, do NOT add `Assessed: yes`, and write
   `<WORK_DIR>/split-proposal.md` with:
   - why one review/test campaign is not coherent;
   - the smallest independently buildable and testable replacement tasks;
   - each task's shipped boundary, acceptance oracle, and dependencies;
   - which current source or validated artifacts, if any, can be salvaged;
   - a final integration task only when integration itself has behavior not
     already proved by the component tasks.
   Do not edit source and do not create or mutate queue tasks. Return
   `RESCOPE_REQUIRED` to the performer.

If you selected any rejection outcome, write only its required status, reason,
direction or split proposal and stop. Otherwise update plan.md with your
refinements. Keep the same structure but:
- fix any inaccuracies
- add missing steps
- remove files, abstractions, and steps the task does not need — deletion is
  as much a refinement as addition
- improve the approach if you found better patterns
- ensure phases are properly sized for single-agent execution
- finalize `## Expected Surfaces` as a recall note for the reviewer, selecting no lens
- finalize `## Evidence Plan`, then write `<WORK_DIR>/test-design.md` with one
  check per acceptance surface: Claim, Changed surface, Instrument, Oracle,
  Window, Control, Falsifier, and Evidence. State which prerequisites are
  gated only if that instrument runs. Name `Outcome: already-satisfied` as a
  candidate when current code appears to meet the request without a change.
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
4. If all phases are done, proceed to pre-review validation.

## Phase 5: Pre-Review Validation

Run the pre-review check selected in the assessed plan. For app source this is
normally the configured Debug build. For an isolated script, generator, CMake
fragment, library or harness it may be a syntax check, configure, focused
target, unit suite, or component probe. Documentation may have only direct
command/link validation in the later evidence loop. Do not build Telegram as
ceremony when it cannot exercise the changed surface, and do not replace a
necessary Telegram build with a cheaper check that bypasses integration.

Prefer running critical-path validation in the main session. If delegated, use
a worker subagent and wait immediately for the result.

```text
You are a pre-review validation agent.

Read these files:
- <WORK_DIR>/context.md
- <WORK_DIR>/plan.md
- .agents/shared/build-lock-recovery.md

The implementation is complete. Run the exact pre-review validation selected
in the assessed plan and fix only task-owned failures that prevent review.

Steps:
1. On native Windows, run the recovery contract's exact-path proactive cleanup
   only when this command writes the configured build tree.
2. Run the assessed command from plan.md at the repository root. When it is the
   Telegram Debug build, use `<BUILD>`; on WSL that is the repository Docker
   entry point and native Windows CMake must not touch that tree.
3. If validation succeeds, update plan.md: change `- [ ] Pre-review validation` to `- [x] Pre-review validation`.
4. If validation fails:
   a. Read the error messages carefully
   b. Read the relevant source files
   c. Fix the errors in accordance with the plan and AGENTS.md conventions
   d. Rebuild and repeat until the build passes
   e. Update plan.md status when done

Rules:
- Only fix task-owned validation failures. Do not refactor or improve code beyond what is needed for a passing check.
- Follow AGENTS.md conventions.
- If the build fails with C1041, LNK1104, "cannot open output file", or a similar
  access-denied lock, follow the shared bounded recovery contract. Do not edit
  source to work around an environment lock.
- You are not alone in the codebase. Respect existing changes and do not revert unrelated work.

When finished, report the build result and which files, if any, you changed.
```

## Phase 6: Adaptive Review Loop

Selection happens with the diff in hand. Assessment records expected surfaces
and escalation triggers but does not choose reviewers. The mandatory general
review runs for every task. The optional library is lifetime, reuse, structure,
performance, and security; the general reviewer may add a specialist-<domain>
review for another material risk.

For the initial implementation:

1. Launch the general reviewer and all five standard lens reviewers together
   when capacity permits. Under a slot limit, queue every lens and start the
   next as a slot opens; none is selected away.
2. Every lens independently reads the task and complete diff. It writes
   `review1-<lens>.md` with `NOT_APPLICABLE` and a compact proof, or continues
   through relevant full files/adjacent code and returns `CLEAN` or `FINDINGS`.
   It never reads the general reviewer's findings or another lens report.
3. Continue the same general-review agent for synthesis when the host supports
   it. Otherwise use a fresh synthesis agent that starts from
   `review1-general.md`, reads all five lens reports, and opens only the code
   needed to confirm findings or reject an unsupported bailout. It writes
   actionable `review1.md`.
4. `APPROVED` closes review. `NEEDS_CHANGES` runs the fix phase for blocking
   findings only.

For a fix, increment R and do not repeat the initial shape. Run one focused
general reviewer over the fix result, changed paths/functions, affected callers,
prior blockers, and invalidated evidence checks. Rerun only a specialist that
originated a repaired blocker or whose prior `NOT_APPLICABLE`/`CLEAN` proof the
fix invalidated.
The focused general reviewer confirms those reports and writes `review<R>.md`,
explicitly carrying all other approvals forward.

After two `NEEDS_CHANGES` verdicts, non-shrinking findings, architectural/scope
expansion, or loss of a reliable carry-forward boundary, run the convergence
prompt instead of another ordinary fix round. It permits at most one final
focused repair before a stop, replan, or rescope. The bound never converts an
unresolved finding into approval.

A wording or style suggestion is non-blocking unless it causes incorrect
behavior, unsafe use, misleading build instructions, a repository-rule
violation, or a material maintenance defect.

### Shared specialist preamble

~~~text
You are an independent <LENS> specialist reviewing one Telegram Desktop task.
You are a leaf and must not delegate.

Read:
- the task specification
- the changed-path manifest and complete task diff, including every hunk

First decide whether this diff affects any mechanism owned by your lens. When
it does not, write a compact `NOT_APPLICABLE` report tied to exact changed
paths/hunks and stop. Do not read every changed file in full or search broadly
just to prove an absent surface. Uncertainty means applicable; small size,
`documentation only`, time pressure, or low estimated severity do not prove
non-applicability.

When applicable, also read:
- <WORK_DIR>/context.md and plan.md
- AGENTS.md and REVIEW.md
- every relevant changed file in full
- adjacent callers, owners, consumers, or repository precedents needed for this
  lens
- for R > 1, only the preceding actionable finding and fix result assigned to
  this lens

Do not read another reviewer's findings. Do not search or read
`<WORK_DIR>/review*` or phase-review logs beyond the exact files listed above;
root repository searches at the source checkout instead. If another review
report is exposed accidentally, disclose it and stop so only this specialist
can be rerun cleanly.

Review only this task's diff under your assigned angle. Search outside the diff
only after the lens is applicable and when call-site or repository context is
needed. Do not report pre-existing problems as task findings.

A finding is BLOCKING only when it names a concrete wrong result, crash, race,
security or data-safety failure, material performance regression, violated
repository rule, or maintenance defect worth changing the retained
implementation. Optional wording, naming preference, speculative cleanup, and
"could be nicer" are NON_BLOCKING and never request a fix.

Write <WORK_DIR>/review<R>-<LENS>.md:

## Lens: <LENS> — iteration <R>

## Checked
<For NOT_APPLICABLE: the complete-diff proof that no owned mechanism changed.
For CLEAN/FINDINGS: relevant full files and adjacent surfaces examined.>

## Findings
<For each: title, file/line, Severity: BLOCKING | NON_BLOCKING, concrete
failure, and specific fix. Omit when empty.>

## Verdict: NOT_APPLICABLE | CLEAN | FINDINGS
~~~

### Specialist: lifetime

Apply when the diff introduces or changes object/resource ownership, callbacks,
reactive subscriptions, async work, threads, cancellation, or shutdown.

~~~text
ANGLE — lifetime, concurrency and races.

- Name every owner and prove it outlives every user.
- Check callback/subscription captures, guards, cancellation and destruction.
- Check re-entrancy, iterator/reference invalidation, and destruction order.
- Check thread affinity, shared-state synchronization, ordering assumptions,
  duplicate completion, cancel-versus-complete, shutdown-versus-work races, and
  visibility of cross-thread state.
- Check external process/resource lifetime where scripts or build tooling spawn,
  wait, cancel, replace, or clean artifacts.
~~~

### Specialist: reuse

Apply when the diff introduces a helper, API, algorithm, style/string, switch,
hook, command, or repeated mechanism.

~~~text
ANGLE — repository reuse and duplication.

Search the repository for each introduced mechanism and record the searches.
Report a blocking finding when an established equivalent fits and the duplicate
would create divergent behavior or maintenance, or when the diff duplicates
logic internally. Do not demand abstraction for one simple local use.
~~~

### Specialist: structure

Apply to cross-module changes, broad moves or deletion, build graphs, generated
sources, platform branches, or new abstractions.

~~~text
ANGLE — containment, placement and load-bearing structure.

Check that code lives in the owning module, platform work stays behind the
established seam, generated/build dependencies are complete, deletion leaves no
reachable or listed remnants, and every new abstraction has a real second user
or layering constraint. Flag scatter, dead code, needless compatibility paths,
and conventions only with a concrete maintenance or behavior cost.
~~~

### Specialist: performance

Apply to hot/repeated paths, main-thread work, startup, I/O, memory, scale, or
material build-time changes.

~~~text
ANGLE — cost, frequency and scale.

Walk callers until the trigger and cardinality are known. A blocking finding
states trigger frequency, multiplier, unit cost, and symptom. Check per-frame
allocation/layout, broad repaint or relayout, per-item timers/subscriptions,
unbounded main-thread I/O or parsing, heavyweight copies, startup/session-load
work, and build steps that unnecessarily invalidate or rebuild broad outputs.
One-shot cold code is not a finding without a material symptom.
~~~

### Specialist: security

Apply to trust boundaries, secrets, authentication, permissions, privacy,
cryptography, untrusted input, command execution, filesystem operations,
downloads, network validation, or destructive behavior.

~~~text
ANGLE — security, privacy and destructive safety.

Trace every untrusted value to its use. Check validation and canonicalization,
shell/argument construction, path traversal and target containment, archive or
download integrity, permissions, credential/secret exposure in files or logs,
authentication and authorization boundaries, cryptographic API use, unsafe
fallbacks, and whether destructive actions resolve exact owned targets. Require
a concrete exploit, exposure, privilege mistake, unsafe deletion, or broken
security invariant for a blocking finding.
~~~

### Mandatory general review

The general reviewer owns the result and cannot assume a specialist covered an
angle. Its initial pass is the one complete-diff safety review. Synthesis should
continue that reviewer when possible; a replacement synthesis reviewer starts
from its saved work rather than duplicating it.

~~~text
You are the mandatory general reviewer for one Telegram Desktop task,
initial review, pass <PASS> of 2. You are a leaf and must not delegate.

Read:
- the task specification and every referenced input
- <WORK_DIR>/context.md, plan.md, visual.md when present, and test-design.md
- AGENTS.md and REVIEW.md
- on pass 1, the complete task diff and every changed file in full, plus
  adjacent callers, consumers, generated/build declarations, and repository
  precedents needed to judge integration
- on pass 2, review1-general.md, all five review1-<lens>.md reports, and only
  the code needed to confirm/drop a finding, validate a NOT_APPLICABLE proof,
  or resolve a contradiction

Independently review correctness, completeness, edge/error paths, unintended
regressions, integration, proportionality, repository conventions, and the
evidence design. Do not defer anything to a specialist.

On pass 1 the five standard lenses are running independently. Complete your own
review without predicting or selecting their verdicts. Name any extra domain
specialist a material risk needs. Write `review1-general.md` with your complete
independent Checked, evidence reconciliation, findings, and pass-1 status, but
do not write `review1.md` yet. Then return so the performer can call you back for
synthesis after every standard lens report exists.

On pass 2, account for all five lenses. Confirm every finding against the code
and drop it when the concrete failure does not hold. Accept `NOT_APPLICABLE`
only when its complete-diff proof establishes that no owned mechanism changed.
If that proof contradicts a hunk or is merely a severity estimate, write
`Incomplete lens: <name> — <owned mechanism>` and return without an
implementation verdict; the performer reruns only that lens as applicable and
calls you back. Do not redo the complete general review; carry pass 1 findings
and coverage forward and open only code needed for synthesis.

Reconcile test-design.md against the actual diff:
- every acceptance criterion and material new risk has a check;
- each instrument executes the changed surface and its oracle can fail;
- controls prove absence/path checks and fixture reachability;
- reverting the diff could change every check's outcome, except a candidate
  already-satisfied task which directly proves the requested proposition;
- app-runtime changes retain a Debug Telegram build and instrumented execution
  when the behavior lives there;
- visible claims retain tight screenshots and numeric or exact-text assertions;
- isolated build/library/harness work is not forced through Telegram when a
  command, artifact, unit or small probe is more direct;
- selected instrument prerequisites are explicit, and unavailable unselected
  instruments are not treated as blockers.

If pass 2 exposes a material domain outside the five standard lenses, write
`Missing specialist: <domain> — <question>` and return without an implementation
verdict. The performer runs only that domain specialist, then calls you back.

Classify findings:
- BLOCKING: concrete wrong behavior, crash, race, security/data-safety failure,
  material performance regression, repository-rule violation, or material
  maintenance defect;
- NON_BLOCKING: optional wording, preference, speculative cleanup, or polish.
  Preserve it in Dropped/Notes, but do not ask the fix agent to implement it.

On pass 2, update <WORK_DIR>/review1-general.md with lens confirmation and the
final general verdict. Then write <WORK_DIR>/review1.md:

## Code Review — Initial
## Coverage
<general coverage plus all five NOT_APPLICABLE/CLEAN/FINDINGS results and any
domain specialist>
## Evidence reconciliation
<each planned check confirmed or changed>
## Verdict: APPROVED | NEEDS_CHANGES | MISSING_SPECIALIST
## Changes Required
<blocking findings only; omit when approved>
## Dropped
<non-blocking or rejected specialist findings, with reason>

An APPROVED verdict requires no blocking finding and complete evidence
reconciliation. NEEDS_CHANGES requires at least one blocking finding.
~~~

### Focused general re-review

~~~text
You are the mandatory focused general reviewer for one Telegram Desktop task,
review round <R> after a blocking fix. You are a leaf and must not delegate.

Read:
- the task specification, AGENTS.md, and REVIEW.md;
- <WORK_DIR>/review<R-1>.md and the fix result;
- the fixed paths, containing functions/types, and affected callers;
- test-design.md entries the fix reports invalidated;
- only the lens or domain-specialist reports explicitly rerun for this fix.

Do not restart the complete-diff review. Verify each repaired blocker, inspect
the fix for regressions in its actual data/control/lifetime boundary, and check
that no undeclared path changed. For every prior general and specialist approval,
record `CARRIED` with why the fix did not touch its exact code or invariant, or
`INVALIDATED` with the concrete changed question. Presence of a broad surface is
not invalidation.

Rerun a lens only when it originated a repaired blocker, the fix invalidated
its prior `NOT_APPLICABLE`/`CLEAN` proof, or the fix introduced a new mechanism
owned by that lens. If one is needed and has not run, write
`Missing lens: <name> — <invalidated proof or new mechanism>` and return; the
performer runs only that lens and calls you back.

Reconcile only evidence checks whose changed surface, oracle, fixture, or
expected result the fix invalidated. Carry all other checks forward.

Write <WORK_DIR>/review<R>-focused.md and <WORK_DIR>/review<R>.md with:

## Code Review — Focused round <R>
## Fix boundary
<touched paths, repaired blockers, affected functions/invariants>
## Carried approvals
<general, specialist, validation, and evidence approvals with reason>
## Invalidated approvals
<only those actually changed, plus rerun result>
## Verdict: APPROVED | NEEDS_CHANGES | MISSING_SPECIALIST | CONVERGENCE_REQUIRED
## Changes Required
<blocking findings only>

Return `CONVERGENCE_REQUIRED` rather than another ordinary fix when this is the
second NEEDS_CHANGES verdict, findings did not shrink, the fix expanded owned
paths/architecture, or the carry-forward boundary is unreliable.
~~~

### Review convergence assessment

~~~text
You are an independent review-convergence assessor for one Telegram Desktop
task. You are a leaf and must not delegate or edit source.

Read the task, context.md, assessed plan.md, every canonical review<R>.md, every
fix result, current owned paths, the current complete task diff, and the exact
validation/evidence status. Read source only as needed to decide disposition.

Diagnose why review is not converging: repeated discovery in stable code,
regressions introduced by fixes, architectural coupling, oversized intrinsic
scope, or an external unsafe boundary. Preserve findings already resolved and
do not run another broad review.

Write <WORK_DIR>/review-convergence<C>.md with exactly one verdict:

## Verdict: CONTINUE_FOCUSED | REPLAN_CURRENT | RESCOPE_REQUIRED | HARD_STOP

- CONTINUE_FOCUSED: one bounded list of remaining blockers, one owned write
  set, the approvals carried forward, and at most one final focused review.
- REPLAN_CURRENT: why the task remains cohesive, the exact validated source
  boundary to preserve or restore, and the replacement approach. Do not use
  this to disguise multiple independent product outcomes as phases.
- RESCOPE_REQUIRED: why one review/evidence campaign is incoherent and a
  split-proposal.md containing independently buildable/testable replacement
  tasks, acceptance oracles, dependencies, and salvageable work.
- HARD_STOP: the exact unsafe or unavailable condition and required human
  action.

The verdict cannot approve code. If a permitted final focused round still has
blocking findings, stop with its unapproved artifacts; do not begin another
campaign automatically.
~~~

On `RESCOPE_REQUIRED`, the performer validates `split-proposal.md`, inventories
all retained owned source paths, writes the canonical split result, and calls
`finish --status split-required`. It does not clean or checkpoint source work;
the scheduler's later split worker assigns the sealed implementation carrier.

### Review fix

~~~text
You are a review-fix agent for one Telegram Desktop task. You are a leaf and
must not delegate.

Read context.md, plan.md, review<R>.md, AGENTS.md, REVIEW.md, and every source
file named by a blocking finding. Implement only "Changes Required".
Do not implement Dropped or non-blocking notes. Stay inside owned-paths.txt and
do not edit AI artifacts.

After editing, run the cheapest validation that can catch breakage in the fixed
surface: a focused compile/target, syntax/configure check, unit, probe, or
component command when sufficient. Rerun the complete Telegram Debug build
immediately only when the fix changes the build graph/ABI or no focused command
can validate compilation; otherwise the performer runs the complete selected
pre-review validation once after review approval. Use build-lock recovery when
applicable.

Report exact touched paths, repaired findings, changed functions/invariants,
which prior lens `NOT_APPLICABLE`/`CLEAN` result (if any) the fix invalidated
and why, and which validation/evidence checks it invalidated. Surface presence
alone is not invalidation.
If no blocking finding can be acted on inside the owned write set, change
nothing and report that boundary.
~~~


## Evidence-Flaw Recovery And Directness

Use a fresh recovery leaf after a repeated evidence failure. The failed
instrument is not automatically the instrument to repair; choose the next more
direct practical way to execute the changed surface.

~~~text
You are an evidence-recovery agent for one Telegram Desktop task. The retained
implementation stays unchanged unless a sound check proves IMPL_BUG. You are a
leaf and must not delegate.

Read the task, final diff, plan.md, test-design.md, test.md, every prior recovery
plan, the raw evidence for the latest run, and the universal evidence-loop
directness rules. When an app overlay is involved, also read its saved inventory
and Telegram/SourceFiles/test/README.md.

The latest failure signature is:
<FAILURE_SIGNATURE>

Forbidden repeated techniques:
<FORBIDDEN_TECHNIQUES>

Before editing a check, append a Recovery plan to test.md:
- Prior proof
- Failed assumption
- Forbidden technique
- New instrument or directness strategy
- Reachability: why it executes the changed surface
- Oracle independence: why it can detect the negative

Then repair only the evidence path:
- correct a command, environment, path, control, or artifact inspection;
- replace a summary with direct artifact reading;
- replace a broad build/app run with a focused unit, probe or component when it
  reaches the changed code more directly;
- replace an isolated probe with the real consumer or Telegram runtime when the
  integration itself is the claim;
- for app setup failure, use an established data insertion API, narrow
  inventoried debug seam, exact callback injection, or physical input only when
  that path is the subject;
- for visual evidence, resolve and capture the exact painted owner with numeric
  or exact-text assertions instead of repeating an ambiguous screenshot.

Keep prior passing checks and do not rerun them unless the recovery invalidates
their state. Do not reimplement the changed behavior inside the test. If no safe
instrument can reach the subject, write Recovery exhaustion with every
applicable strategy and the concrete evidence or reason, then return BLOCKED
for independent confirmation.
~~~

## Evidence-Campaign-Cap Assessment

At MAX_TEST_RUNS with an evidence flaw still open, use a fresh assessor. The
first campaign cap may start one focused recovery campaign. A focused campaign
cap or repeated non-converging focused signature is the final automatic
checkpoint; it cannot start a third campaign.

~~~text
Read the task, final diff, plan.md, test-design.md, test.md, every run artifact,
and any saved probe, script or overlay.

Write <WORK_DIR>/test-cap-assessment-<C>.md:

## Prior proof
<passing checks and exact decisive evidence; these are not rerun>

## Unmet checks
<only acceptance checks still lacking decisive evidence>

## Directness audit
<each instrument or setup already attempted and forbidden, followed by the next
safe strategy that executes the changed surface>

## Verdict: FOCUSED_RECOVERY | RECOVERY_EXHAUSTED | HARD_STOP

Choose FOCUSED_RECOVERY only after the normal campaign and whenever a safer or
more direct reading, command,
artifact inspection, unit, probe, component, Telegram log/overlay, physical
interaction or visual capture can still decide an unmet check. The next
campaign runs only unmet checks and their controls.

Choose RECOVERY_EXHAUSTED only when every applicable instrument is unsafe,
unavailable, or would bypass the task's changed surface. Time spent, a run cap,
overlay complexity, a blank screenshot, or repeated identical failure is not
exhaustion.

At the focused campaign boundary, choose HARD_STOP when a plausible safe direct
strategy remains but automatic recovery did not converge. Name that strategy,
the exact prior proof retained, and the human/environment decision needed to
continue. Leave the task in-progress; do not publish approval or Block and do
not begin another campaign automatically.
~~~


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

When all phases, including pre-review validation, code review, evidence, and Windows line ending normalization when applicable, are done:
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

For review iterations, include the iteration and lens in the file name, for example:
- `phase-1-context-plan.prompt.md`
- `phase-6a-review-1-general.prompt.md`
- `phase-6a-review-1-general.result.md`
- `phase-6a-review-1-lifetime.prompt.md`
- `phase-6a-review-1-security.prompt.md`
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

## Subagent Pattern (Grok Build)

1. Write the phase prompt file(s).
2. From a top-level `/perform-task` session, make one blocking
   `spawn_subagent` call per leaf — parallel calls in a single message
   for independent leaves of the same step — with self-contained
   prompts and `background: false`. From a `/continue` child, use the
   same prompt files as same-session checklists and do not spawn.
3. When a spawn returns, or when a same-session checklist finishes,
   validate the expected artifacts or code changes with small shell
   summaries and the completion checks above.
4. Write the result log from the validated outcome and the compact
   reply block.

Do not replace this pattern with a shell-launched `grok` process, a
workflow script, or the Codex wait ladder.

## Subagent Pattern (Codex)

Use this pattern conceptually for delegated phases:

1. Write the phase prompt file.
2. Spawn a fresh leaf subagent with a unique tool-valid task name and `fork_turns: "none"` unless a small recent-turn fork is required.
3. Require the agent to create the matching progress file early and refresh it sparingly: at natural milestones when possible, otherwise only after a longer quiet stretch such as roughly 5-10 minutes.
4. Poll for at most 60 seconds at a time. After any mailbox wake, inspect the saved target with `list_agents`; use elapsed five-minute windows rather than poll count for stall checks.
5. Prefer filesystem mtime checks on the progress file first. If its mtime moved or the heartbeat counter increased, keep waiting; do not treat that as a stall.
6. After a full blocked-check window with no movement, use `send_message` for a running target or `followup_task` for an idle one. After a second unchanged window, interrupt if needed and retry the disposable phase once with a unique task name.
7. Validate the expected artifact or code changes with small shell summaries and the completion checks above.
8. Write the result log from the validated outcome and the compact reply block.

Do not replace this pattern with shell-launched `codex exec`.
