# Improve lane

Some work has no exact end result to accept: make start-up faster, make chat
scrolling smoother, cut a stack's latency. Its `task.md` has an
`## Objective` section instead of pass/fail acceptance for the objective, and
the task runs this lane on top of the normal pipeline. Everything else in
`pipeline.md` holds: preflight, owned paths, reviews, the commit contract,
`result.md`, project sync and `finish`.

A goal that is pass/fail, such as "migrate X, no slower than before", is an
ordinary task with a guardrail criterion, not this lane.

## The objective section

`## Objective` states:

- **Metric**: what is measured and which direction is better, for example
  "95th percentile frame time while scrolling a chat with media, lower is
  better".
- **Protocol**: the scenario, the machine and build, the number of runs, the
  statistic compared, and the noise threshold below which a difference does
  not count.
- **Guardrails**: hard criteria that keep their pass/fail meaning, such as
  "no regression above the noise threshold in start-up time" or "every
  existing test passes". A guardrail that fails blocks a change like any
  acceptance criterion.
- **Target** (optional): a value that ends the task early when reached.

## The loop

1. Measure the baseline with the protocol before any change, and keep the
   raw numbers as evidence.
2. Profile, form one hypothesis, change the code, and measure again.
3. Keep a change only when it beats the noise threshold and every guardrail
   holds; otherwise revert it. Kept changes are ordinary commits with the
   task's `Task:` line.
4. Record every attempt in `work/improve-log.md`: hypothesis, change,
   measured result, kept or reverted, and why. Failed ideas are results too;
   they keep later tasks from trying them again.
5. Repeat until the target is reached, or until three consecutive attempts
   gain nothing above the noise threshold, or until no untried idea with a
   plausible gain remains.

Measurements must be comparable: same machine, build configuration and
scenario for every run in the task. When the host is too noisy for the
threshold, raise the run count rather than trusting one run.

## Review and evidence

The general review and the lenses run once over the retained diff when the
loop ends, with the improve log and the measurements. Each kept change must
show its gain in the evidence; a reviewer may ask to revert a change whose
gain is inside the noise or whose cost (complexity, memory, a guardrail near
its limit) outweighs it. Fixes follow the ordinary focused re-review rules.

## The result

The task always ends `approved`, with the measured before and after:

- some gain retained: `Outcome: changed`;
- nothing worth keeping: no source commit, `Outcome: already-satisfied`, and
  the improve log as evidence that the current code is the best found.

Missing the target is never a block. Write the before/after numbers and the
stop reason (target reached, diminishing returns, ideas exhausted) at the top
of `work/test.md`. Name promising ideas the task did not reach under
`Discovered:` so the project planner can route them as further `improve`
tasks.
