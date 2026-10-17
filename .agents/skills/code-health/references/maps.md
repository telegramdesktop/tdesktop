# Area maps

An area map records what one area is for and where its code goes, so agents
stop re-deriving structure from code that has already drifted. Reviews judge
placement against the map rather than the surrounding code.

## Location and size

One map per atlas area, in the `AGENTS.md` of the directory named by the
area's first `Paths:` pattern: `iv/editor` maps to
`Telegram/SourceFiles/iv/editor/AGENTS.md`. Areas whose first pattern is in
the same directory share that file, one `# <Area> map` part each. Areas in
`lib_*` submodules keep only their atlas entry. At most about 150 lines of
plain language per area: the owner approves maps by reading them, never by
reading code.

Name source files in backticks (`` `iv_editor_state.cpp` ``) so the scan can
report a map that names files which no longer exist.

## Sections

```markdown
# <Area> map

<Two sentences: what the area does for the product, and its boundary.>

## Modules

- `file.cpp`, `file.h` — what it owns, in one line.

## Placement

- New <kind of code> goes in <module>; never in <module>.

## Invariants

- <Checkable rule.> (approved YYYY-MM-DD)

## Leave alone

- <Thing that looks odd but is deliberate, and why.>

## Direction

<Where the area is heading, in words; empty when stable.>
```

- **Modules**: one line per file or tight file group. A file that needs a
  paragraph is a split candidate; say so under Direction.
- **Placement**: answers "I am adding X, where does it go?" for the kinds of
  change the area actually receives. This is the section feature tasks use.
- **Invariants**: only rules a tool or test can check — include direction,
  which module may touch secrets or disk formats, which operations are pure.
  Each carries its approval date. Unapproved invariants are proposals.
- **Leave alone**: protects deliberate choices from cleanup churn.
- **Direction**: the target shape. Q5 work needs its invariants approved here.

## Lifecycle

- A survey drafts the map. The owner approves it in words, or adjusts it.
- A map is source, so a task that needs a different shape edits the map in
  its own diff, where its reviewers judge the edit with the code. Keep such
  edits narrow. Invariants and Direction change only with the owner's word;
  a Q0–Q2 task touches only Modules lines, as moved or deleted files
  require.
- A map whose area keeps generating review findings about placement gets a
  re-survey instead of more amendments.
