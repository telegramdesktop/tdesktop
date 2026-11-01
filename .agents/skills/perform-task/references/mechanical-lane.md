# Mechanical lane

A code-health task of class Q0 (delete), Q1 (move) or Q2 (merge identical
copies) changes no behavior, and tools prove it. Its `task.md` has a
`## Code health` section naming the class. Such a task runs this lane instead
of Phases 1–7 and the evidence loop: the performer edits, builds and proves
the change in its own session, then one fresh reviewer checks it. Planning
leaves, the lens fan-out and test campaigns cost far more than a deletion or
a move can go wrong.

This lane is the code-health exception to the AI repository's review fan-out
and evidence-campaign rules. Everything else in `pipeline.md` holds:
preflight, owned paths, the commit contract, `result.md` and `finish`. The
classes and their proofs are defined in
`.agents/skills/code-health/references/classes.md`.

## Bounds

An item stays in the lane only while it is exactly its class:

- **Q0** removes code that no build, platform file or generated input
  references, together with its declarations, includes, CMake entries, and
  the functions, fields, classes, styles and language keys only it used. A
  style or language key goes only when nothing else in the tree mentions it.
  A field that other code still writes stays unless every writer is in the
  files the task names.
- **Q1** moves code without editing it. The only new lines are includes,
  declarations, namespace scaffolding and CMake source-list entries.
- **Q2** replaces copies that are identical apart from names and literal
  values with one shared function in a file both callers' targets already
  link; each call site passes its own values. What only the removed copy
  used goes with it. The kept copy may move within its own file, for example
  out of an anonymous namespace, and drops `[[nodiscard]]` or `static` from
  its definition once a header declares it.
- Nothing else changes: no new strings, styles, settings or log lines, no
  reordered side effects, and no new templates, header-defined functions or
  `std::variant` dispatch, so neither build work nor binary size can grow.
- Edits stay within the files the task names, the file that receives moved
  code or the shared copy, their CMake source lists, the include lines of
  their direct users, the Modules lines of the area map, and the entries in
  `.style` files and `Telegram/Resources/langs/lang.strings` that only
  removed code used.

An item that cannot stay within these bounds is dropped, not forced: restore
its files and record it under `Dropped:` with the reason. Dropping is the
normal outcome for a false candidate, such as a function a platform file still
calls, copies that differ in logic, or a move that needs edits. Never
continue a dropped item in the full pipeline inside this task; the scan
proposes it again in a class that fits. A defect noticed on the way is never
fixed here: write it as a discovered task. Other dead or duplicated code
noticed on the way, such as a copy below the clone threshold, is named in
the result prose and left alone.

## Steps

Run every command from `SOURCE_ROOT`. `BASE_REF` is
`refs/ai-tasks/<TASK_ID>/base`, `PROOF_DIR` is `TASK_DIR/.local/proof`, and
`HEALTH` is `python3 .agents/skills/code-health/scripts/health.py`. On
resume, continue after the last checkpoint and rerun every proof after any
edit.

1. **Preflight.** Read `SOURCE_ROOT/AGENTS.md`, `REVIEW.md`, `TASK_SPEC` and
   the nearest area map (`AGENTS.md` in the area's directory), and
   `AI_SLOT/AGENTS.md`. Run the pipeline's source-lineage gate,
   `source-prepare` unless the scheduler just ran it, `source-preflight`
   without `--exe`, and `source-begin`. The lane needs no executable,
   account, overlay or UI driver.
2. **Recheck.** The findings were measured on an older `origin/dev`. Run
   `HEALTH verify --base BASE_REF <finding ids>`; it exits 1 while items are
   present, which is expected here. A `present` item is still to do. An `unknown` item was not found under its id; read the code at the
   places the task names and drop it only if the code is gone or no longer
   fits the class. If every item is dropped here, skip to step 8.
3. **Edit.** Write `work/owned-paths.txt` first, then edit without leaves.
   Register each new file with `git add -N <path>` so every diff shows it.
   Checkpoint `mechanical-edit`.
4. **Prove.** Save each command's complete output in `PROOF_DIR`:
   - the Debug `Telegram` build as `AGENTS.md` describes, plus any test
     target that compiles a changed file;
   - `HEALTH check --base BASE_REF`: no errors, and no warning the change
     caused. Two warnings matter most here. "Definition removed but still
     mentioned" scans platform files this host never compiles. "The change
     removed its last use" names a function, field, class, style or key the
     change left dead: remove it within bounds and check again until neither
     appears, because the Debug build hides unused functions;
   - Q1: `HEALTH moves --base BASE_REF` reports `MOVE_ONLY`, or every line
     it lists is a declaration or re-wrapped signature you explain;
   - `HEALTH verify --base BASE_REF <kept ids>` reports each item
     `resolved`, or, for one step of a larger split, the metric reaches the
     task's target. `changed` means a smaller copy of the clone is left.

   Fix a failing proof within bounds or drop its item. After a drop, rerun
   every proof on what remains; the incremental build recompiles what the
   restore touched, and that is enough. If nothing is left, restore the
   checkout to `BASE_REF` and skip to step 8. Checkpoint
   `mechanical-proved`.
5. **Review.** One fresh reviewer with the prompt below writes
   `work/review1-mechanical.md`; keep the prompt and its result log at
   `work/logs/phase-mechanical-review-1.prompt.md` and `.result.md`, and
   checkpoint `mechanical-reviewed`. On Claude Code it is a Codex CLI leaf under
   the adapter's review provider and
   [Codex CLI leaves](../../../shared/codex-cli-leaves.md) (`--cwd WORK_DIR
   --sandbox workspace-write --no-project-doc`); when Codex is unavailable,
   and on other hosts, it is a fresh native leaf. Effort is `high` for Q0
   and Q1, because the bounds forbid any logic change and the proofs are
   attached, and `xhigh` for Q2.
6. **Fix once.** On `NEEDS_CHANGES`, fix each finding within bounds or drop
   its item, rerun the affected proofs, and resume the same reviewer once
   (`--resume review1-mechanical`, writing `work/review2-mechanical.md`) with
   a prompt that lists each finding, what changed for it, and the new proof
   outputs, and asks it to judge only those fixes and what they affect. If
   that verdict still needs changes, drop the items it names and rerun the
   proofs. There is no third review: the approval covers the items the last
   review did not name.
7. **Commit.** Commit what remains with `source-commit --mark-green` and a
   repository-style subject (no `[ai] ` prefix: this is product code).
8. **Publish.** Write `work/test.md` and `work/result.md` as below, then
   `finish --status approved`.

## Review prompt

```text
You review one mechanical code-health change. You are a leaf: do not
delegate, edit source or commit. Write your report to <REVIEW_PATH>.

Read AGENTS.md, REVIEW.md, <TASK_SPEC>, the area map <MAP_PATH or "none">,
the complete diff (`git -C <SOURCE_ROOT> diff <BASE_REF>`), every changed
file in full, and the proof outputs in <PROOF_DIR>.

The class contract is in
.agents/skills/code-health/references/classes.md and the bounds in
.agents/skills/perform-task/references/mechanical-lane.md. Decide:

1. Behavior: could any user-visible behavior, stored format, network
   traffic, log line or timing change? For Q2, compare the former copies
   line by line and confirm every difference became an argument.
2. Completeness: for Q0, no remaining reference on any platform (search
   platform/, *_win, *_mac, *_linux, .mm and generated inputs) and no
   orphaned declaration, include, field, string or style; for Q1, nothing
   edited in transit; for Q2, every copy the task names now calls the
   shared one and nothing only the removed copy used is left behind.
3. Bounds: no edit outside the files the bounds allow.
4. Proofs: each was run on this diff and supports its claim.
5. Placement: new homes follow the area map, or the surrounding structure
   when there is no map.

Do not ask for improvements beyond the task's items. A finding blocks only
when it names a concrete behavior change, a broken build on some platform, a
missed reference, an edit out of bounds, or an unsupported proof. Name the
item each finding concerns.

Report format:
# Mechanical review: <TASK_ID>
## Checked
<one line per item>
## Findings
<numbered, each with its item, file:line, the failure, and the fix or "drop item">
## Verdict: APPROVED | NEEDS_CHANGES
```

## Result

`work/test.md` records each proof as a check:

```markdown
# Proof: <TASK_ID>

Lane: mechanical, class Q<n>

## <check>
- Command: `<command>`
- Oracle: <what passing means>
- Actual: <exit code and the decisive output lines>
- Result: PASS
```

`work/result.md` uses the pipeline's fields with `STATUS: DONE`,
`Verdict: APPROVED`, `Blocker-Type: none`, `Split-Proposal: none`,
`Attempts: 1`, `Test-Runs: 0`, `UI-Driver: not-applicable`,
`Test-Report: work/test.md`, `Unverified: none` and
`Checkout: clean-buildable`. With a commit, use `Outcome: changed`,
`Implementation: retained`, the touched paths, and
`Evidence: work/test.md, work/review<R>-mechanical.md`. When every item was
dropped, use `Outcome: already-satisfied`, `Implementation: none`,
`Touched: none` and `Evidence: work/test.md`, with the recheck or proof
output that justified each drop; the acceptance of a mechanical task allows
this, and no review runs because nothing changed. Then add:

```markdown
## Code health
Resolved: <finding ids, or none>
Dropped: <finding id> — <reason>, one line each, or none
Metric: <before> -> <after>
```
