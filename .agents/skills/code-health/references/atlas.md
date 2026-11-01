# Source atlas

The atlas divides the whole source tree into named areas, each with one clear
responsibility. It lives in `Telegram/SourceFiles/AGENTS.md`, so it is the
nearest map for any code without an area map of its own. `health.py` takes
area names from it, so metrics, agent cost, proposals and surveys follow
real responsibilities instead of directory depth. The owner approves it in
words, once, and approves amendments as the code changes.

## Format

```markdown
# Source atlas

<Two or three sentences: what the atlas is and how to use it.>

## <Layer>

### <area> — <responsibility in a few words>
- Paths: `SourceFiles/data/`, `SourceFiles/history/history_item*`
- <One to three sentences: what the area owns, in product terms, and what it
  must not own.>
```

- **Area names** are short path-like slugs (`history/view`, `wallet`,
  `ui-kit/widgets`), unique, and stable: renaming one resets its history in
  the ledger.
- **Paths** are relative to `Telegram/`. A pattern ending in `/` is a
  directory prefix; one with `*` is a glob; anything else is one file. The
  most specific pattern wins: a file, then a glob, then the longest prefix.
  Every source file must match exactly one area.
- **Layers** order the areas from foundation to features, so a reader sees
  which way dependencies should point: libraries, core, model, network and
  storage, UI kit, UI libraries (the libraries that link lib_ui), shared UI,
  features in product groups, platform, tooling and tests. A known break of
  an area's boundary is named in a "Today it also …" sentence instead of
  widening the responsibility.
- About 60–90 areas and at most ~400 lines. An area larger than ~40K lines
  or with two unrelated responsibilities is split; a directory under ~2K
  lines joins the area it works with most.

## Draft

1. Run `health.py atlas --ref origin/dev --fetch --state <health_state>`
   (see the skill's state section). It writes
   `atlas/<stamp>-<commit>/skeleton.md` and `skeleton.json` there: candidate
   areas from the
   directory tree (directories over 30K lines split into their large
   subdirectories, ones under 3K join their parent), each with its size,
   largest files, file-name groups, CMake target, the areas it includes and
   is included by, the areas it changes together with, churn, agent cost
   and test coverage.
2. **Describe**, with parallel agents of about fifteen areas each. Each
   agent reads the skeleton block, the file list, every header's top-level
   declarations and the opening of the largest files, then writes for each
   area: its responsibility in product terms, its main entry points, and
   any mixed responsibility or misplaced files, with the globs a split or
   move would use. Agents read; they do not run builds or edit source.
3. **Consolidate**, with one agent: accept or reject each proposed merge,
   split and move against the data (an area that changes together with
   another more than with itself belongs with it; a directory grouping
   unrelated widgets is split by responsibility), assign layers, and write
   `atlas.md` in the format above.
4. **Verify**, with the other provider when the host can run one (on Claude
   Code a Codex CLI leaf, `--cwd <scan worktree> --sandbox read-only`): it
   samples each area's responsibility against the code and reports claims
   the code contradicts. `health.py atlas --check <draft>` must report every
   file matched by exactly one area and no pattern that matches nothing.
5. Save the draft, the verifier report and a short list of the judgment
   calls the owner should look at first (splits, merges, layer choices)
   beside the skeleton, and publish them with `health-publish`. The owner
   approves in words or asks for changes.
   Only the approved text is committed, as
   `Telegram/SourceFiles/AGENTS.md`.

## Upkeep

The scan reports source files no area matches and patterns that match
nothing. A new directory or a reshaped area gets an amendment: the scan's
run proposes the narrow edit for the owner's word, never a rewrite. Area
maps under the atlas describe one area in depth and must agree with it.
