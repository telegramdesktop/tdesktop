# Adding or changing harness helpers

[Harness guide](../README.md) · [Helper index](helpers/README.md)

A permanent helper needs discoverable documentation without making every
product task read its implementation history. Apply this format when adding a
helper or changing its public contract. Existing reference prose was moved
from the old README; improve the affected section when needed, without
rewriting unrelated references.

## Where each kind of information belongs

| Location | Contents |
| --- | --- |
| `test/README.md` | Short mandatory rules and links chosen by task need; keep under 8 KiB. |
| `test/docs/helpers/README.md` | One row per module: link, kind and one-line purpose. |
| `test/docs/helpers/test_<name>.md` | The module's usage, requirements, observations, refusals, cleanup and verification. |
| `test/docs/<topic>.md` | A cross-module procedure such as account setup, scenario teardown or launch behavior. |
| `test_<name>.h` | Signatures and concise caller contracts: units, ownership, lifetime and return/refusal semantics. Link the reference for examples and longer explanations. |
| Task evidence/history | Full discovery chronology, repeated run logs and implementation deliberations. |

Use the existing module reference when extending a helper; do not add another
README paragraph for each new function. A separate module gets its own page.
Group related operations with headings. Link a shared procedure instead of
copying it into every helper page. Keep a brief historical example only when
it explains a non-obvious constraint; move the full chronology to task evidence.

Keep the mandatory README small by replacing detail with a precise routing
link, not by making an essential prerequisite optional. Its topic links tell
a reader when account, media, input, capture or teardown guidance is required.
Do not add a rule requiring all helper pages or the diagnostic history to be
read. A helper-specific platform caveat belongs with that helper.

## Module reference template

Use the module stem for the filename so a source/header search finds its page.
Fill the relevant sections concretely; omit a section only when it is truly
inapplicable, and say when an operation has no cleanup or platform restriction.
Keep the opening purpose and selection guidance short enough to choose a
helper before reading its complete contract.

```markdown
# test_<name>

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API / Harness self-test / Internal dependency.
Source contract: [test_<name>.h](../../test_<name>.h).

## Use when

What observable need this answers; when a nearby existing helper is preferable.
For an internal dependency, name its real callers instead of suggesting overlay use.

## Preconditions and limits

Required process/session/widget state, platform support, coordinate/time units,
identity or generation requirements, and what this helper does not establish.

## Minimal use

One small example against the real API. Include its readiness gate, assertion
and required cleanup; keep fixture values synthetic and secrets out of text.

## Readings and refusals

Return values and log markers; what an accepted reading proves; named refusals
and which ones make a measurement undecidable. Never equate a refusal with PASS.

## Lifetime and cleanup

What is owned or retained, cancellation, restoration, and onFinish obligations.
Name any lasting side effect, including clipboard changes or account writes.

## Verification

The self-test entry or focused command, required environment, a negative control
or deliberate mutation it catches, and known platform/evidence limitations.
Distinguish an executed check from a proposed check.

## Related guidance

Links to prerequisite topic guides and related helpers, without copying them.
```

A pure self-test page must name the production harness behavior it protects,
its fixture, expected verdict and teardown. It is not a general-purpose helper
and need not be used by product overlays. An internal dependency page names
its actual callers; an absence of direct overlay includes is not non-use.

## Before finishing the change

- Update the module page and its one-line index entry in the same change as the
  API. Update callers' topic links when a prerequisite or contract moves.
- Read the header and the example together: signatures, units, ownership and
  refusal behavior must agree. Do not invent an example API or promise a
  negative control that was never run.
- Link to the reference from the header when detailed guidance exists. Keep
  comments about the callable contract; do not paste the reference into the
  header or the mandatory README.
- Preserve useful constraints when moving old prose. Search existing references
  and diagnostic rows before creating a duplicate explanation.
- Check relative links and heading anchors, the index's coverage of public
  helper modules, and the mandatory README's byte size. Keep source citations
  symbolic rather than line-number based.

Documentation reorganization alone needs link/content checks, not a Telegram
build. Changes to helper behavior still need their normal focused evidence
and review; this format does not replace that workflow.
