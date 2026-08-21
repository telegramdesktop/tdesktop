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

- When delegation is available, use a fresh subagent for Phase 1 (context and plan), Phase 3, each Phase 4 implementation unit, each Phase 6 general review pass, each surviving Phase 6 specialist, and each Phase 6 fix. Do not switch those phases to same-session midstream because of a timeout or missing artifact.
- The mandatory general reviewer runs first and alone on the complete diff, and emits the retirement list. Surviving specialist reviews are independent and write disjoint reports, so spawn them together when capacity allows, giving them the diff and the retirement decision but never the general reviewer's findings. The general reviewer then returns, confirms or drops their findings, reconciles the evidence design, and owns the overall verdict.
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
- Spawn the independent leaves of one step — the surviving Phase 6 specialists,
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
- Spawn the independent leaves of one step — the surviving Phase 6 specialists,
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
- A Phase 6 specialist is complete only when every scheduled specialist wrote `review<R>-<lens>.md` with a `## Verdict:` line and a non-empty `## Checked` section. A report that records no checked surfaces is incomplete work.
- Phase 6 general review is complete only when `review<R>-general.md` and `review<R>.md` exist with a `## Verdict:` line, a non-empty `## Coverage` section, every retired lens carrying its absence assertion, every surviving lens accounted for, and every evidence check reconciled against the actual diff.
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
5. Expected surfaces: record the surfaces this task is likely to touch and the
   escalations they would imply, as a note for the reviewer. Do not select or
   omit review lenses here — the general reviewer decides that with the diff in
   hand, and every optional lens runs unless it retires the lens by asserting
   the absence of its surface.
6. Evidence design: map every acceptance criterion and material shipped risk to
   the most direct practical instrument that can detect the negative. Allow
   static readings, commands/artifacts, unit tests, a standalone probe or
   component, Telegram logs/overlay, Computer Use and screenshots in any
   necessary combination. Apply the revert test and remove unrelated checks.
   Preserve deep app testing when behavior lives in Telegram, and require tight
   captures for visible claims. Do not require Telegram for an isolated probe or
   build-stage claim it cannot strengthen.
7. Phase sizing: Each phase should be implementable by a single agent in one session. If a phase has more than about 8-10 substantive code changes, split it further.
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

Update plan.md with your refinements. Keep the same structure but:
- fix any inaccuracies
- add missing steps
- remove files, abstractions, and steps the task does not need — deletion is
  as much a refinement as addition
- improve the approach if you found better patterns
- ensure phases are properly sized for single-agent execution
- finalize `## Expected Surfaces` as a note for the reviewer, selecting no lens
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

For iteration R:

1. Run the general reviewer first and alone, on the complete diff. Besides its
   own review it emits the retirement list: every optional lens runs unless it
   is retired, and a lens is retired only by asserting the absence of its
   surface in the terms `pipeline.md` gives. Record each retirement with its
   assertion. It writes review<R>-general.md.
2. Run the surviving specialists independently and in parallel. Give them the
   diff and the retirement decision, never the general reviewer's findings.
   They write review<R>-<lens>.md.
3. The general reviewer returns, confirms or drops each specialist finding
   against the code, and writes the actionable review<R>.md, accounting for
   every retired and every surviving lens.
4. APPROVED closes review. NEEDS_CHANGES runs the fix phase for blocking
   findings only.
5. After a substantive fix, increment R, rerun general with emphasis on the
   changed hunks so it re-decides retirement there, and rerun only the
   specialists whose surfaces the fix touched or whose prior blocking finding
   was repaired. A fix that changed no owned source closes the loop without
   another round.

There is no automatic five-lens replay, and no lens is skipped by omission:
silence retires nothing. Every review cycle must either add material coverage,
validate changed code, or close. A wording or style suggestion is non-blocking
unless it causes incorrect behavior, unsafe use, misleading build instructions,
a repository-rule violation, or a material maintenance defect.

### Shared specialist preamble

~~~text
You are an independent <LENS> specialist reviewing one Telegram Desktop task.
You are a leaf and must not delegate.

Read:
- <WORK_DIR>/context.md
- <WORK_DIR>/plan.md
- AGENTS.md and REVIEW.md
- the task specification
- the complete task diff and every changed file in full
- for R > 1, the preceding actionable review and fix result

Review only this task's diff under your assigned angle. Search outside the diff
when your angle requires repository or call-site context, but do not report
pre-existing problems as task findings.

A finding is BLOCKING only when it names a concrete wrong result, crash, race,
security or data-safety failure, material performance regression, violated
repository rule, or maintenance defect worth changing the retained
implementation. Optional wording, naming preference, speculative cleanup, and
"could be nicer" are NON_BLOCKING and never request a fix.

Write <WORK_DIR>/review<R>-<LENS>.md:

## Lens: <LENS> — iteration <R>

## Checked
<Every changed surface examined under this angle and what was established.>

## Findings
<For each: title, file/line, Severity: BLOCKING | NON_BLOCKING, concrete
failure, and specific fix. Omit when empty.>

## Verdict: CLEAN | FINDINGS
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
angle. It runs twice per iteration: pass 1 alone on the diff, emitting the
retirement list, and pass 2 after the surviving specialists report.

~~~text
You are the mandatory general reviewer for one Telegram Desktop task,
iteration <R>, pass <PASS> of 2. You are a leaf and must not delegate.

Read:
- the task specification and every referenced input
- <WORK_DIR>/context.md, plan.md, visual.md when present, and test-design.md
- AGENTS.md and REVIEW.md
- on pass 2 only, every review<R>-<lens>.md the surviving specialists wrote
- the complete task diff and every changed file in full
- adjacent callers, consumers, generated/build declarations, and repository
  precedents needed to judge integration

Independently review correctness, completeness, edge/error paths, unintended
regressions, integration, proportionality, repository conventions, and the
evidence design. Do not defer anything to a specialist.

On pass 1 no specialist has run yet. Besides your own review, emit the
retirement list. Every optional lens — lifetime, reuse, structure, performance,
security — runs unless you retire it, so a lens you do not mention still runs.
Retire one only by asserting the absence of its surface in the exact terms
pipeline.md gives, and write that assertion beside it; a failing clause keeps
the lens, and "documentation only" is not an assertion of absence. Name any
extra domain specialist a material risk needs. Then return: the performer runs
the surviving specialists and calls you back for pass 2.

On pass 2, confirm every specialist finding against the code; drop it when the
concrete failure does not hold.

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

If pass 2 exposes a material risk no surviving lens covered — including one you
retired on pass 1 — write "Missing specialist: <name> — <reason>" and return
without an implementation verdict. The performer runs that specialist, then
calls you back again in iteration R. Retiring a lens you then have to recall is
a worse outcome than keeping it, so retire only on the absence clauses.

Classify findings:
- BLOCKING: concrete wrong behavior, crash, race, security/data-safety failure,
  material performance regression, repository-rule violation, or material
  maintenance defect;
- NON_BLOCKING: optional wording, preference, speculative cleanup, or polish.
  Preserve it in Dropped/Notes, but do not ask the fix agent to implement it.

Write <WORK_DIR>/review<R>-general.md with Checked, specialist confirmation,
evidence reconciliation, findings and verdict. Then write
<WORK_DIR>/review<R>.md:

## Code Review — Iteration <R>
## Coverage
<general coverage plus selected/omitted specialist reasons>
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

### Review fix

~~~text
You are a review-fix agent for one Telegram Desktop task. You are a leaf and
must not delegate.

Read context.md, plan.md, review<R>.md, AGENTS.md, REVIEW.md, and every source
file named by a blocking finding. Implement only "Changes Required".
Do not implement Dropped or non-blocking notes. Stay inside owned-paths.txt and
do not edit AI artifacts.

After editing, run the cheapest pre-review validation affected by the fix. For
an app-source fix this is normally the configured Debug build; for an isolated
script, generated artifact, or probe it may be its focused syntax/configure or
component check. Use build-lock recovery when applicable.

Report exact touched paths and which review specialists the fix invalidated.
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

At MAX_TEST_RUNS with an evidence flaw still open, use a fresh assessor. The cap
is a convergence checkpoint, never a terminal verdict.

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

## Verdict: FOCUSED_RECOVERY | RECOVERY_EXHAUSTED

Choose FOCUSED_RECOVERY whenever a safer or more direct reading, command,
artifact inspection, unit, probe, component, Telegram log/overlay, physical
interaction or visual capture can still decide an unmet check. The next
campaign runs only unmet checks and their controls.

Choose RECOVERY_EXHAUSTED only when every applicable instrument is unsafe,
unavailable, or would bypass the task's changed surface. Time spent, a run cap,
overlay complexity, a blank screenshot, or repeated identical failure is not
exhaustion.
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

For review iterations, include the iteration and selected lens in the file name, for example:
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
