# Area survey

A survey is a deep, agent-driven review of one area. It produces the area map,
verified findings the scanner cannot see, and ranked proposals. It costs
roughly ten agent tokens per source line, so it targets areas where structure
costs the most: high hotspot, high agent hours per task, or a large feature
about to land.

## Inputs

1. `health.py area <area>`: files, sizes, churn, classes, long functions,
   clones, layering, unused candidates, build reach, agent cost. Do not
   re-measure what it already reports.
2. The previous survey and the current map, when present.
3. The open ledger findings for the area (`ledger.json` in the state).
4. The code at the scanned commit: every lens reads the checkout that
   `health.py worktree` prints, never the source checkout's working tree.

## Lenses

Run these as independent parallel reviewers, each reading code directly and
returning file:line evidence. None sees another's report.

- **Model and responsibilities**: what each large class and file actually
  owns, broken down by line range; state machines encoded in flags; where
  domain logic lives inside widgets or boxes.
- **Duplication**: repeated logic within the area and against existing
  tdesktop and lib_ui mechanisms, with both locations.
- **Coupling and build**: dependency direction, includes that cross targets,
  headers that reach far more units than they need, global and static state.
- **Testability**: which logic is pure today, what blocks a headless test, and
  the cheapest first test target.
- **Defects in passing**: anything that looks wrong while reading. These are
  reported separately and become ordinary tasks, never code-health work.

## Verification

Before anything is written down, a separate verifier re-checks every claim the
proposals or the map rely on: re-run the count, re-read the lines, confirm
"unused" has no callers. Use the other provider for the verifier, because
same-model reviewers share blind spots: on Claude Code a Codex CLI leaf under
[Codex CLI leaves](../../../shared/codex-cli-leaves.md) (`--cwd <scan worktree>
--sandbox read-only`), elsewhere a fresh native agent. Claims that were not
re-checked are marked `(inferred)`.

## Outputs

Saved in `health_state` as `surveys/<area>/<date>/` and published with
`health-publish`:

- `survey.md`: module map by responsibility, hotspots, duplication,
  coupling, testability, what to leave alone, and a ranked plan in classes
  Q0–Q5 with the dependency order (tests before reshapes).
- `map.md`: the draft or amended area map (see `maps.md`), for approval.
- `proposals.md`: task proposals in the format of `SKILL.md`, at most ten.
- `defects.md`: defects found in passing, each with evidence, as ordinary
  task requests.

A survey changes no source code and creates no queue records itself.
