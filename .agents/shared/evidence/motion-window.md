# Motion-window assessment

`motion_window.py` judges timed motion windows (a widget at rest, moving
while pressed, gliding after the release) from per-sample records and their
replay rows. It decides whether each declared window covers its declared
states and phases with exactly identified records, whether every record's
own cost stays within a declared budget, and whether a declared motion law
holds at handler-time origins. It never decides by a minimum record count.

The QR gift campaign (`2026/10/15/choose-a-unique-gift-for-the-qr-box`)
shows why. Its Run 3 judge failed a naturally ending glide of nineteen
records (eighteen moving, one stopped) against a ">= 20 records" gate the
instrument author had added, and its exact x-curve check on the Run 2 data
left a 1.812 px residual at its 1.5 px bound because the stored pointer
stamps preceded handler dispatch: a caller-recorded 79 ms velocity span that
a one-millisecond endpoint shift makes 80 ms (`work/test.md`, Recovery 2
and Run 3; `work/evidence-assessment.md`, "Why 19 glide recordings decide
the check").
Here coverage, not a count, decides; exact laws use handler time; and a
corrected judgement keeps the original failing result byte-intact.

The pieces:

- [test_paint_stream.h](../../../Telegram/SourceFiles/test/docs/helpers/test_paint_stream.md)
  writes the records: every timed `RecordPaint` row carries `window`, `at`,
  `pressed`, `moving` and `position` fields beside its stream identity and
  `recordMs`.
- [paint_replay.py](paint-replay.md) qualifies the replay of those streams
  and owns the shared `account_streams` and `quantile` helpers this tool
  imports. Its approval is assumed here, not repeated.
- [test_animation_clock](../../../Telegram/SourceFiles/test/docs/helpers/test_animation_clock.md)
  and [test_probe](../../../Telegram/SourceFiles/test/docs/helpers/test_probe.md)
  supply the action brackets and handler-time telemetry (see Time origins).

Nothing here builds or launches anything; the assessor reads JSON and, when
a laws file is given, runs that file's Python.

## What a decision claims

A window passes when its own records, in record order, start and end in the
states its kind requires, place at least one moving record in every declared
phase, are each replayed exactly once under the same identity, and, when
declared, keep each record's envelope within the budget and the law within
its tolerance at handler time. `allPassed` is every window passing plus clean
identity accounting over every manifest row.

A pass does not claim every presented frame, anything between two records,
compositor or presentation cadence, a Release Telegram frame time, or a
population, tail or confidence bound. Records of a window the spec does not
declare are listed in `unassessedRecords` and judged by nothing.

The result: `schema`, `tool` (`motion_window`), `allPassed`, `reasons`
(failing codes in check order), `checks`, `windows`, `diagnoses`,
`unassessedRecords`, `scope`, `limits`, from the CLI `inputs` (`path`,
`bytes`, `sha256` of each input) and, when reassessing, `reassessment`. Each
`checks` entry is `{check, window, pass, reason, message, facts}` (`window`
only on window checks); a check with several problems emits one failing
entry per code with the affected records in `facts.details`. Checks, in
order: `spec`, `accounting`, then per window `records`, `states`, `phases`
(pressed and release), `budget` (when declared), `origin` (when declared) and
`law` (when declared). A window whose records are missing or malformed stops
after `records`.

Each `windows` entry: `name`, `kind`, `pass`, `reasons`, `records` (names),
`states` (`records`, `pressed`, `moving`, `stopped`), `firstAt`, `lastAt`,
`spanMs`, `gapsMs`, `maxGapMs`, `firstObservationDelay` and `terminalDelay`
from the origin action, `phases`, `budget`, `law` and `scope`.

## Inputs

**Spec** (`--spec`), schema 1; unknown keys are refused so that a misspelt
budget or law is never skipped silently:

```json
{
  "schema": 1,
  "actions": [
    {"name": "release", "before": 2100, "after": 2101, "handlerAt": 2100}
  ],
  "windows": [
    {"name": "rest", "kind": "rest"},
    {"name": "glide", "kind": "release", "origin": "release", "phases": 3,
     "budgetMs": 6.5, "replayQuantile": 0.95,
     "law": {"name": "glide", "origins": ["release"], "tolerance": 0.5}}
  ]
}
```

| Key | Meaning |
|---|---|
| `actions[].name` | Unique action name. |
| `actions[].before` | Caller stamp taken before the action was dispatched. |
| `actions[].after` | Optional caller stamp after the action and its postponed calls; `before <= after`. |
| `actions[].handlerAt` | Optional time stamped inside the handler path: the exact origin. |
| `windows[].name` | The `window` field of its records. |
| `windows[].kind` | `rest`, `pressed` or `release`. |
| `windows[].origin` | Optional action the delays are measured from. |
| `windows[].phases` | Pressed and release only, required: an integer >= 1. |
| `windows[].budgetMs` | Optional per-record envelope budget, > 0; needs `replayQuantile` in (0, 1]. |
| `windows[].law` | Optional `{name, origins, tolerance}`: a law from the laws file, its origin actions and the declared tolerance in position units. |

Every number in the spec (times, budget, quantile, phases, tolerance) is the
campaign's declaration. The tool has no defaults for them, and an omitted
`after` or `handlerAt` (or `null`) is absent.

**Manifest** (`--manifest`): a JSON object whose `streams` rows carry `name`
and optional `qpic.sha256` / `tables.sha256`. Rows with a `window` key are
records: `window`, `at` (ms, on the actions' clock), `pressed`, `moving`,
`position` (a list of numbers, one dimension per window) and `recordMs`. A
`manifest.json` written by `WritePaintManifest()` is that shape unchanged;
its absent, narrow and empty rows are accounted but are not records.

**Replay** (`--replay`): a JSON object whose `rows` carry `name`, `timesNs`
and optional `qpic` / `tables` SHA-256s. A `paint_replay --out` result is that
shape; a fatal replay without rows leaves every record unreplayed. Budgets
assume a replay that `paint_replay.py` approved: this tool does not re-check
the gate, the Qt build, fidelity or sample accounting.

**Laws** (`--laws`, optional): a Python file defining
`LAWS = {name: callable(origins, at) -> [x, y, ...]}`. `origins` maps each
declared origin action to the time it is evaluated at, `at` is a record's
time, and the result is the predicted position. The file runs in the
assessor's process: pass only a laws file written for the campaign.

Command, with `RUN_DIR` the run's ignored directory:

```bash
python3 .agents/shared/evidence/motion_window.py \
  --spec RUN_DIR/window-spec.json \
  --manifest RECORD_DIR/manifest.json \
  --replay RUN_DIR/paint-replay.json \
  --laws RUN_DIR/laws.py \
  --out RUN_DIR/window-assessment.json
```

Exit 0 when every check passes, 1 on a refusal (including `invalid-spec`,
`invalid-original` and `would-overwrite-input`), 2 on a usage or input error
(an unreadable or non-JSON input, a laws file that does not load or defines
no `LAWS`, a reassessment option without the other two).

## Window kinds and coverage

- **rest**: no record is pressed or moving and the position never changes;
  else `motion-in-rest-window`.
- **pressed**: the first record is moving (`not-started-in-motion`), every
  record is pressed (`released-in-pressed-window`) and moving, and the
  position changes (`static-substitution`).
- **release**: no record is pressed (`pressed-in-release-window`), the first
  record is moving (`not-started-in-motion`), the last is stopped
  (`missing-stopped-terminal`): record until the first stopped observation.
  No stopped record comes before it and the position changes
  (`static-substitution`).

Every window needs a record (`empty-window`), well-formed records
(`invalid-record`) and strictly increasing `at` in record order
(`unordered-records`).

**Phases** are declared, never inferred from a count. For a pressed or
release window, a moving record's progress is the projection of
`position - first` on `last - first` divided by its squared length, where
`first` and `last` are the window's first and last records (the stopped
terminal for `release`), computed exactly over the recorded numbers. Phase
`k` of `n` holds progress in `[(k - 1) / n, k / n)`, the last phase also 1.
Each phase needs at least one moving record, else `phase-uncovered` names
the empty phases; a record outside `[0, 1]` is listed in `outside` and covers
none. With no moving record or no position change at all the window is
`static-substitution` instead; with a changing position whose first and last
records coincide progress is undefined and every phase is uncovered. The
report lists `counts`, `records` and `progress` per moving record.

A natural glide shows why no minimum exists: the self-test's 472 px/s,
200 ms exponential glide, first observed 117 ms after the release and
sampled every 55-164 ms until its first stopped observation, yields
nineteen records whose moving ones fall into thirds as `[2, 1, 15]` and
passes, while thirty-one records clustered in the first third with a distant
stopped terminal fail `phase-uncovered`.

## Budget and statistical scope

With `budgetMs`, each record's envelope is its own `recordMs` plus the
`replayQuantile` nearest-rank order statistic of its own stream's `timesNs`
(`paint_replay.quantile`: rank `ceil(q * n)` of `n`, reported per record with
`valueMs`, `rank` and `n`). A record over the budget is `over-budget`, one
without `recordMs` or usable `timesNs` is `missing-cost`, one without a
replay row is `missing-replay`. The window reports `envelopeMaxMs`.

The claim is per record: this record's emission cost plus an order statistic
of this stream's kept replays in one replay run. It is not a frame time of
every presentation and not a population or tail bound; with 25 kept replays
the 0.99 quantile is the maximum, with 400 it is order statistic 396.
`recordMs` measured in Debug Telegram is Debug-Qt emission, an
over-estimate (see [paint-replay.md](paint-replay.md)).

## Time origins

An action has two kinds of stamp, all on one `crl::now()` clock:

- **caller stamps**: `before`, and optionally `after`. With clocked input
  these are `Test::ClockedRun::actionStarted` and `actionFinished` from
  `RunClockedFrames`; otherwise a stamp taken right before dispatching the
  event, and one after it was handled.
- **handler time**: `handlerAt`, the time the handler path saw the action,
  for example the `at` of a `Test::Probe` timed row (`timedRowsSince`) that
  the overlay records inside the handler.

Delays (`firstObservationDelay`, `terminalDelay`) are `{claim: "exact", ms}`
from a consistent `handlerAt`, else `{claim: "conservative-bound", bounds:
[record - after, record - before]}` with a `null` lower bound when `after`
is absent: the reached-bounds model of `RunClockedFrames`. A `handlerAt`
outside `[before, after]` is `inconsistent-clock` and yields only the bound.

A law is judged only at handler time. Every law origin needs `handlerAt`;
one without it is `missing-exact-origin`, no law claim is made, and only the
conservative delay bounds remain. The residual is the largest Euclidean
distance between a record's `position` and `law(origins, at)` over the
window's records; above the declared `tolerance` it is `law-residual`. The
same law is also evaluated at the caller `before` stamps, and `offsetsMs`
reports `handlerAt - before` per origin. When the caller evaluation misses
the tolerance while the handler one meets it, the window still passes and
carries the diagnosis `time-origin-discrepancy`: the caller stamps are not
the handler's time origin. In the self-test a 1 ms caller/handler difference
on the second of two velocity moves (a 79 ms caller span against the
handler's 80 ms) moves the predicted glide end by about 1.19 px against a
0.5 px tolerance. Both evaluations report the same declared tolerance;
nothing widens it.

## Reassessing a failed instrument

When a judgement failed because of the instrument, not the subject (an
invented count gate, caller stamps used as origins), assess again and embed
the original:

```bash
python3 .agents/shared/evidence/motion_window.py \
  --spec RUN_DIR/window-spec.json \
  --manifest RECORD_DIR/manifest.json \
  --replay RUN_DIR/paint-replay.json \
  --reassess RUN_DIR/original-judge-result.json \
  --correction "what was wrong with the instrument" \
  --claim "the bounded claim the new judgement supports" \
  --out RUN_DIR/window-reassessment.json
```

`--reassess`, `--correction` and `--claim` go together. The original must be
JSON with a top-level `checks` list of `{check, pass, ...}` entries, the
shape of these tools and of the QR campaign's judge, else
`invalid-original`. The tool reads its bytes and SHA-256 first and embeds
`reassessment: {original: {path, sha256, bytes, failedChecks}, correction,
claim}`, with every failing entry verbatim, beside the new assessment. After
writing the result it re-reads the original and refuses `original-modified`
if its SHA-256 changed. An `--out` naming any input, by path or as the same
file through a hard link, is `would-overwrite-input` and writes nothing.
Keep the original where it was produced; the new result is a separate file.

## Refusals

Codes in check order. `time-origin-discrepancy` is a diagnosis, not a
refusal.

| Code | Meaning | Action |
|---|---|---|
| `invalid-spec` | The spec is not schema 1: an unknown key or kind, a pressed or release window without an integer `phases >= 1`, `phases` on a rest window, `budgetMs` without a `replayQuantile` in (0, 1] or the reverse, an origin or law origin that names no action, a law the laws file does not define, a duplicate name, or `before > after`. | Fix the spec; nothing was assessed. |
| `invalid-original` | The `--reassess` file is not UTF-8 JSON with a top-level `checks` list of `{check, pass}` entries. | Pass the original judge result, unchanged. |
| `would-overwrite-input` | `--out` names an input, by path or as the same file. | Write the result to a new path. |
| `original-modified` | The original's SHA-256 changed during the reassessment. | Restore the original and reassess again; never edit it. |
| `duplicate-record` | Two manifest rows carry one stream name. | Give every recording its own name and record again. |
| `duplicate-replay` | Two replay rows carry one stream name. | Replay the manifest once with the unmodified replayer. |
| `missing-replay` | A recorded stream has no replay row (also in a budget check). | Replay the complete manifest. |
| `extra-replay` | A replay row has no recorded stream. | Replay exactly the manifest assessed. |
| `mismatched-identity` | A `.qpic` or `.tables` SHA-256 differs between the manifest and the replay row. | Replay the files the recorder wrote, unchanged. |
| `invalid-record` | A record lacks a name, a numeric `at`, boolean `pressed` and `moving`, or a numeric `position` of the window's dimension. | Record every sample with the five window fields. |
| `empty-window` | A declared window has no record. | Record the window and name it in the records' `window` field. |
| `unordered-records` | `at` is not strictly increasing in record order. | Record in time order on one clock; never sort records afterwards. |
| `motion-in-rest-window` | A rest record is pressed or moving, or the position changes. | Record rest only at rest, or declare another kind. |
| `not-started-in-motion` | A pressed or release window's first record is not moving. | Start the window at the first moving observation. |
| `released-in-pressed-window` | A pressed window holds an unpressed record. | End the pressed window at the release. |
| `static-substitution` | A pressed record is not moving, a release window stops before its terminal, the position never changes, or no moving record exists. | Record the real motion; a static state cannot stand in for it. |
| `pressed-in-release-window` | A release window holds a pressed record. | Start the release window after the release. |
| `missing-stopped-terminal` | A release window's last record is still moving. | Record until the first stopped observation and keep it. |
| `phase-uncovered` | A declared phase holds no moving record, or progress is undefined. | Record densely enough to observe every phase; more records elsewhere do not cover it. |
| `over-budget` | A record's envelope exceeds `budgetMs`. | Report it; the declared budget is never widened. |
| `missing-cost` | A budgeted record has no `recordMs`, or its replay row no usable `timesNs`. | Record with `RecordPaint()` and replay every timed stream. |
| `inconsistent-clock` | An origin's `handlerAt` lies outside its `before`/`after` bracket. | Stamp all three on one `crl::now()` clock around and inside one dispatch. |
| `missing-exact-origin` | A law origin has no `handlerAt`. | Record handler-time telemetry for every law origin. |
| `law-residual` | The handler-time law misses a position by more than the tolerance. | Report the residual; never widen the tolerance or switch to caller stamps. |
| `law-error` | The law raised or returned no finite point of the records' dimension. | Fix the laws file. |

## Self-tests

```bash
python3 .agents/shared/evidence/motion_window_test.py -v
```

The suite is pure Python and needs no compiler, component, QR or gift code.
`natural_glide()` builds the release window from the glide law and the
recorder cadence, not from a count, and asserts its premise: nineteen
records, eighteen moving, the last stopped.

- `CoverageTests`: the natural window passes with gaps cycling 55, 164, 55
  and 109 ms, an exact 117 ms first-observation delay (bounds `[116, 117]`
  without the release's handler time, `[null, 117]` without `after` either,
  116.5 ms with a `handlerAt` half a millisecond after `before`), phases
  `[2, 1, 15]`, per-record ranks 23 of 25, `scope` and `limits`; the rest and
  pressed windows pass; four records that cover every phase pass.
- `RejectionTests`: deep-copy mutations of the natural set, each asserting
  the exact set of failing codes: an empty window, the stopped terminal removed,
  records marked pressed or not moving, frozen positions, two `at` values
  swapped, a replay renamed, a replay digest changed, a duplicate replay or
  record, one record over the budget, missing costs, a moving rest record, a
  released or late-starting pressed window, malformed records, thirty-one
  clustered records (`phase-uncovered`), a moving record behind the start
  that covers no phase, and a pressed motion that returns to its start.
  Frozen positions fail both the `states` and the `phases` check.
- `InvalidSpecTests`: every spec rule above is refused `invalid-spec` with
  no window assessed.
- `TimeOriginTests`: the handler-time law passes with a residual of about 0
  while the caller evaluation's residual of about 1.19 px misses the 0.5 px
  tolerance and is diagnosed `time-origin-discrepancy`, with `offsetsMs`
  showing the 1 ms and the tolerance unchanged; another `tau` is `law-residual`; a
  `handlerAt` outside its bracket is `inconsistent-clock`; a move without
  `handlerAt` is `missing-exact-origin` with no exact law claim; a raising or
  malformed law is `law-error`.
- `ReassessmentTests` (CLI): a synthetic original whose only failing check
  is a ">= 20 records" gate stays byte-identical while its failing check is
  embedded verbatim with the correction and claim; a missing or blank
  `--correction` is a usage error; an `--out` naming the original, a hard
  link to it, the manifest or the laws file is `would-overwrite-input`; an
  original without a valid `checks` list is `invalid-original`; a writer
  that touches the original is `original-modified`.
- `CliTests`: the laws-file path, exit 0, 1 and 2, and `invalid-spec`.
- `DocumentationTests`: the refusal table above lists exactly the tool's
  codes.

`paint_replay_test.py` `ComponentTests` also runs this tool on the real
fixture manifest and gated replay of its positive run (rest, a pressed drag
and a release glide, three phases each, with test-declared actions and
budget): the rows `test_paint_stream.h` and the replayer wrote feed it
unchanged and pass.

## Limits

- Declared windows and their records only: what happens between two records
  is not observed, and a window the spec does not declare is not judged.
- Phases measure progress along the first-to-last displacement; a motion
  that reverses or curves away from that segment is covered only where its
  projection falls.
- Budget envelopes are per record and per run; no population, tail or
  confidence bound, and the replay's own qualification is assumed.
- Times are the recorder's clock relative to the declared actions; the tool
  cannot tell whether a stamp was taken where the spec says.
- Laws are caller callables run in this process; the tool compares their
  positions with the records and does not validate the physics.
