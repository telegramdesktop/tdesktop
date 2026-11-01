# Designing one packed scenario

[Harness guide](../README.md) · [Helper index](helpers/README.md)

The permanent Debug-only harness supports `perform-task` overlays. Use the
[helper index](helpers/README.md) to select references for this scenario.
The overlay adds task-specific fixtures, production entry points, checks
and captures; do not recreate facilities already here.

The default is one packed scenario and one app launch. Put all compatible
checks, themes, values, interactions, and captures into that execution. Plan
another launch only when you can name an incompatible process-lifetime
requirement, such as a startup-only setting or persisted state that the first
flow necessarily contaminates. “The scenario is large” and “a smoke run is
simpler” are not reasons to split it. After a `TEST_FLAW`, repair every flaw
visible in the run together and rerun the packed scenario.

Cite code by symbol, never by line number, here and in harness comments:
`Runner::finish()` in `test_runner.cpp`, not `test_runner.cpp:NNN` or
`:NNN-MMM`. Line numbers drift with every edit, including the next harness
task's, and nothing updates them. A failure-diagnosis row names the
functions, constants and macros on its causal path, in product, submodule
and Qt sources alike. Quoted log or crash text stays verbatim.

## Building one reliable packed scenario

1. Derive every falsifiable check from the task, visual contract, and retained
   diff before writing overlay code.
2. Choose the most direct fixture that still executes the changed production
   code. Publish exact objects/actions at construction and callback seams when
   unrelated navigation would be the fragile part.
3. Arrange nondestructive states first and destructive/closing actions last.
   Reuse one live fixture through its empty, rich, long, error, retry, light,
   and dark states when those transitions are production behavior.
4. Put readiness and generation checks in `until`; put actual-versus-expected
   values in `then` or `captureAndInspect`.
5. Log every value needed to diagnose a failure in the first run. A timeout
   should name the last object generation, visibility, geometry, state, and
   callback count instead of forcing a speculative second overlay edit.
6. Save decisive tight captures as the scenario reaches each state. Do not
   postpone all visuals to the end, after later actions have replaced them.
7. Finish with guards: no undeclared OS launch, no real payment/network call
   when mocked, expected callbacks exactly once, and no leftover expectation.

Read the [runner contract](helpers/test_runner.md) and [teardown](teardown.md)
for the selected scenario. A [minimal overlay example](overlay-example.md) is
available when needed; it is not a mandatory fixture preamble.
