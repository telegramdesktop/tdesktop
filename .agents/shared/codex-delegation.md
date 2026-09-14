# Codex child completion and recovery

Use this contract for `continue` workers and `perform-task` phase agents on
Codex. Claude and Grok use their foreground-call adapters. This contract owns
waiting and agent recovery; the calling workflow owns artifact validation,
publication, and its task boundary.

## Dispatch and wait

- Keep the canonical target from `spawn_agent`, its role, expected result, and
  whether its current assignment is outstanding. Use `fork_turns: "none"`
  and explicit input paths. A saved agent reused with `followup_task` has a
  new outstanding assignment; its previous completion does not satisfy it.
- Give disposable phase leaves the final-reply contract from the phase
  prompts. Publication-owning workers, including non-delegating split,
  routing, and consolidation workers, keep their caller's commit permission
  and result contract. Tell every publication-owning or orchestrating child
  to read this contract and stay in its turn until its assigned boundary or a
  reported hard stop. A final progress-only reply stops that turn; it does
  not arrange continuation when a child finishes.
- While a child owns the next step, wait for its result with `wait_agent`.
  Request a 30-minute wait when the current tool and higher-priority
  instructions allow it; otherwise use the longest permitted wait. Native
  completion results arrive automatically. Do not end the parent turn to
  wait for them, and do not build shell, sleep, transcript, or app-task
  monitors around native subagents.
- The mailbox is shared: process each completion or failure for its saved
  target once, handle user input, and continue waiting for outstanding
  assignments. Another agent's message is not this child's completion.
  A timeout, commentary, or quiet period is not a failed assignment.
- Ordinary wakes and short timeouts only renew the wait. Do not list agents,
  read phase files, check mtimes, request progress, or narrate unchanged
  status on each wake. Workers owe no heartbeat, progress file, or periodic
  report. Preserve existing recovery artifacts without maintaining them as
  liveness signals.
- After a child returns, validate its required artifacts and repository state
  once. A compact final reply is a notification, not proof of success. Start
  the next dependent phase or performer only after that validation. Retain
  the general reviewer's target for its later synthesis assignment.

## Detect a missing result

Completion notifications are the normal path. To catch a stopped child whose
notification was missed, check runtime state once after each 30 minutes of
unresolved waiting, measured from dispatch or the last runtime state check.
Short wait timeouts and messages from other agents do not restart that
interval. This is a liveness check only: use `list_agents` with the narrowest
saved `path_prefix`; do not inspect files or request a status report.

Also inspect runtime state when a tool reports an agent error, a stop or
interruption, or an incomplete return whose status is ambiguous. Use only
states and controls actually exposed by the current tool:

- **Still running:** wait again. Silence, a missing artifact, and old file
  timestamps cannot establish that a running agent has died. Never interrupt
  or replace it merely for exceeding a waiting interval.
- **Completed:** recover its result from the delivered notification or saved
  target's completion state, then apply the caller's completion checks. If
  the result is missing or incomplete, use the recovery rules below instead
  of waiting for another response from a finished turn.
- **Idle, interrupted, or failed:** that turn will not finish the outstanding
  assignment by itself. Record the reported state/error and recover below.
- **Missing target or unavailable/ambiguous status:** do not assume death or
  start a replacement. Report a recoverable orchestration hard stop with the
  saved target and observed absence or tool error, leaving owned state intact.

These tools can detect only the runtime states they expose. A runtime outage
or an agent permanently reported as running cannot be proved dead through
silence. Report that uncertainty when established; never promise guaranteed
crash detection or infer it from a heartbeat deadline.

## Recover a stopped assignment

Before recovery, establish that the old turn has stopped and inspect its
saved descendants and exact owned command handles. Interruption of a parent
is not proof that its children or commands stopped. A resumed orchestrator
may reattach to those existing workers and wait; pass their targets and
handles and forbid redispatch or overlapping writes. Before retrying work
that could write the same paths, establish that all old writers have stopped.
Wait for them or safely stop only verified owned work when the caller allows
it. If writer ownership or state cannot be established, hard-stop without
starting overlapping work.

- A stateful performer or publication-owning worker is never replaced during
  the invocation. If its saved target is available, use `followup_task` once
  to resume from the first incomplete validated boundary, supplying any
  finished child results it missed. Then wait in the same parent turn.
  If that recovery fails or the target cannot be resumed, report a recoverable
  hard stop; a later invocation uses the durable task or transaction state.
- A disposable phase with an incomplete result or a failed turn may be retried
  once in a fresh leaf with a unique name and the saved prompt plus the exact
  failure and existing owned changes. Validate any already landed artifacts
  first; do not repeat completed work. Do not run the phase locally as a
  fallback after a delegated failure. A second failure is a hard stop.
- Explicit user interruption or cancellation is not an automatic retry;
  honor the user's direction. Agent/tool interruption alone does not publish
  a task `Block` or `Approve`. Leave interrupted task state `in-progress`
  unless the caller validates an already completed canonical boundary.

On a hard stop, report the target, observed state/error, attempted recovery,
and durable result or transaction paths. Do not invent missing results or
continue the queue past its unfinished stateful owner.
