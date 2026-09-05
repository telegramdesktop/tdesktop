# Workflow phase effort

The parent selects effort before each phase or worker starts, using its actual
scope and current evidence. Keep the parent model unless a host adapter below
specifies otherwise. This policy applies to all three workflow skills and the
evidence loop; it does not create extra delegation or change phase ownership.

## Select effort

| Work | Effort |
| --- | --- |
| Routine scheduler bookkeeping, helper-driven setup/publication, running already specified build or test commands, collecting artifacts, and mechanical text normalization | `medium`, only while no design, diagnosis, source repair, or verdict is needed |
| Context and planning, visual design, independent assessment, implementation, all review lenses and synthesis, fixes, evidence design/authoring/assessment, failure diagnosis, and convergence or rescoping | Default to `xhigh` |
| Inbox planning, split/discovery routing, pending-task consolidation, and a stateful performer that owns the whole implementation/evidence campaign | Default to `xhigh` |

Use `high` instead of `xhigh` only when the parent is sure there is nothing
complex in that phase and can state the concrete reason from the inspected
scope. A short prompt, small diff, expected `NOT_APPLICABLE`, or time pressure
is not enough. Unknown scope, ambiguous behavior, interacting subsystems,
lifetime/concurrency/security concerns, or a failed earlier approach favor
`xhigh`. Do not select below `medium`.

Classify the whole assignment. A worker that includes planning, repair, or
judgment does not qualify for `medium` merely because it also runs commands.
In particular, the scheduler's routine work does not make its performer or
routing workers `medium`.

Include the selected effort and a brief scope reason in the existing worker
prompt; for phase logs, also record the applied setting or inheritance fallback
in the result's `NOTES:`. Give a concrete justification for every `high` choice.

If a `medium` execution phase encounters a failure needing diagnosis or repair,
preserve the output and return it to the parent before expanding the work.
The parent selects `xhigh`, or justified `high`, for that follow-up and resumes
the existing validation/review contract. Likewise, unexpected complexity in a
`high` phase calls for `xhigh`. Preserve completed artifacts and checks.

## Apply on the current host

- **Codex:** When the exposed spawn schema supports it, pass
  `reasoning_effort: "medium"`, `"high"`, or `"xhigh"` as selected above.
  Omit `model` to retain the parent model. Use `fork_turns: "none"` or the
  smallest necessary positive turn window; a full-history fork cannot carry
  these overrides. Put required context and exact paths in the prompt.
- **Claude Code:** For an assignment classified as `medium` or justified
  `high`, pass `model: "opus"` on the Agent call. The `high` choice requires
  the parent's concrete reason from the inspected scope that the phase
  contains nothing complex, as above. For `xhigh` assignments, omit `model`
  and inherit the parent model; this remains the default for non-routine
  work. Do not pass a reasoning field: the Agent tool does not expose one.
  The `opus` pin changes the model; actual reasoning effort remains inherited.
- **Grok Build:** Keep the adapter's inherited model and effort; do not invent
  unsupported effort arguments.
- **Same-session work or unavailable overrides:** Retain the actual session
  setting and record that limitation in existing notes. Prompt wording does
  not change effort. Do not start another CLI process, modify global settings,
  or add workers solely to tune effort.

Re-evaluate effort before reusing a child for a new scope. If a disposable
leaf needs an unavailable effort/model change, let its writes finish, then
launch a fresh leaf with the selected setting and the saved artifacts. Keep
the single stateful performer; do not replace it solely to change effort.
