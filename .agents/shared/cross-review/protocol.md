# Cross-provider review loop

One stateful **runner** reviews a change through independent **reviewers**
from another model provider, decides every finding, fixes what it accepts and
reviews again until the loop converges. Today the runner is Claude Code
(`.claude/commands/cross-review.md`) and the reviewers are Codex CLI leaves
under [Codex CLI leaves](../codex-cli-leaves.md). The protocol does not depend
on which provider plays which part.

The reviewers are fresh and blind in every round. The runner alone remembers
what was found, fixed, rejected or deliberately traded off, and it is the final
arbiter of how the code ends up. That split is what makes the loop converge:
independent reviewers keep finding real problems, and the runner refuses to be
walked back and forth between two defensible designs.

## Scope

- **Uncommitted** (the default): the working tree against `HEAD`, untracked
  files included.
- **Last N commits**: the working tree against `HEAD~N`. Uncommitted changes
  present at the start are part of the scope; the runner says so in its
  report.

Freeze the scope once:

```bash
python3 .agents/shared/scripts/cross_review_scope.py init \
  --source-root SOURCE_ROOT [--commits N]
```

It creates the session directory, by default `<git common dir>/cross-review/
<timestamp>/`, outside the working tree. Before each round, snapshot it:

```bash
python3 .agents/shared/scripts/cross_review_scope.py snapshot \
  --session SESSION --round R
```

The snapshot writes `roundR/scope.patch` (everything under review),
`roundR/files.txt` and, from round 2, `roundR/fix.patch` (only what changed
since the previous round). It never touches the real index. Exit code 3 means
the scope is empty: report that and stop.

## Round 1: full review

Start six reviewers at once, each with only the change and the repository:

- the **general** reviewer, which reads every changed file in full and owns
  correctness, completeness, edge and error paths, regressions, integration,
  proportionality and repository conventions;
- one reviewer per standard lens — **lifetime**, **reuse**, **structure**,
  **performance**, **security** — with the lens charters perform-task uses.

No reviewer sees another's report. When they have all finished, the general
reviewer continues its own session for **synthesis**: it reads the five lens
reports, confirms or drops each finding against the code, merges duplicates
and writes one numbered list with a verdict. Save every report and the
synthesis in `roundR/`.

## Arbitration

The runner decides every synthesized finding, after reading the code itself,
and records the decision in `SESSION/ledger.md` before changing anything:

- **FIX** — the failure is real and worth the change.
- **REJECT** — the failure does not hold, or the cost of the change exceeds
  its benefit; say which.
- **DEFER** — real but outside this change; name where it belongs.
- **COMPROMISE** — a deliberate trade-off between approaches that each have a
  cost; name what was given up and why.

`NON_BLOCKING` findings are recorded but never start a fix round by
themselves.

A finding that asks to undo an earlier decision is a **reversal**. Dismiss it
with a reference to the ledger entry unless it brings a concrete failure
scenario that entry did not weigh. Only then reconsider: record a new decision
that supersedes the old one and keeps both rationales. A decision may be
superseded once; if a third round tries to flip it again, stop and give the
choice to the human with both sides.

The runner may overrule every reviewer, but it writes down why.

## Fixing

The runner applies FIX decisions to the working tree under the repository's
normal rules, builds what the change requires, and runs the repository's own
pre-commit checks. Fixes stay uncommitted unless the human asked for commits;
the runner never rewrites history on its own.

## Later rounds: focused review

After a round with fixes, snapshot the next round and start:

- one **focused general** reviewer over `fix.patch`, the functions and callers
  it touches and the complete current scope, with the list of findings the
  previous round fixed so it can confirm each one; and
- a fresh reviewer for every lens that originated a finding fixed in the
  previous round, over the complete current scope.

Reviewers never see rejected, deferred or compromise decisions. When lenses
were rerun, the focused general reviewer continues its session for synthesis
as in round 1. Arbitrate and fix as before.

## Stopping

The loop runs until the review consolidates: every finding is fixed and
reviewed again, or decided without a fix because it does not hold or points at
a problem this change neither introduced nor touched. A real defect in the
changed code is never left open to save a round.

Stop when any of these holds and report it:

- **converged**: a round produced no FIX decision, after reversals were
  dismissed;
- **round cap**: the cap, ten rounds unless the human set another, is
  reached;
- **not converging**: fixes keep reopening earlier decisions instead of
  settling new findings, or a fix would grow the change into another
  subsystem;
- **human decision needed**: a decision would be superseded a second time, or
  a fix needs a product choice the change does not settle.

## Ledger

`SESSION/ledger.md` is the runner's memory and the session's record:

```text
# Cross-review ledger
Scope: <uncommitted | last N commits> from <base short hash> <subject>

## Decisions
D<n>. <path> <symbol> — <one-line claim>
  Round <R>, from <synthesis id> (<reviewers>)
  Decision: FIX | REJECT | DEFER | COMPROMISE
  Why: <reason, including what a COMPROMISE gives up>
  Change: <what changed, or none>
  Supersedes: <D<m> or none>

## Rounds
Round <R>: <reviewers>; <findings> findings, <blocking> blocking;
  fix <a>, reject <b>, defer <c>, compromise <d>, reversals dismissed <e>
```

## Report

End with the stop reason, the rounds run, every FIX with its files, every
REJECT, DEFER and COMPROMISE with its reason, any open items, and the session
directory. The ledger and the round reports stay in the session directory.
