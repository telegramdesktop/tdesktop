---
description: Triage the crash dumps for the newest published tag, fix what is simple, and record the rest
allowed-tools: Bash, Read, Write, Edit, Glob, Grep, Agent, Workflow, AskUserQuestion, TodoWrite
---

# Process Crash Reports

Triage `~/Telegram/Crashes/all` for the newest published tag, one distinct
crash at a time, and land the fixes that are simple enough to land.

A run can face several hundred reports of which most are duplicates or belong
to old versions. The context budget is the scarce resource, so the split of
labour is fixed:

- **Bash does the mechanical work.** `.claude/scripts/crash_triage.py` rotates
  the inbox, drops the uninteresting reports, and clusters the rest. Never read
  a `log_*.txt` in the main session to decide any of that.
- **The main session only ever sees summaries** — the funnel counts, one line
  per crash group, and each investigation's verdict. It orchestrates, applies
  fixes, and commits.
- **Subagents see one crash each, and nothing else.** Every investigation
  starts from a clean context and loads its own report. This is what keeps
  investigation quality flat from the first group to the last, so never hand an
  agent two groups, and never carry one group's findings into another's prompt.

Arguments, when present: a version (`7.1.2` / `7001002`), a target branch,
`--no-claim` to re-triage the previous run folder instead of rotating in new
reports, `--limit N` to stop after N groups, `--dry-run` to investigate without
committing anything.

```text
$ARGUMENTS
```

## Phase 0 — open the run

1. Decide the target branch. Crash fixes belong on `dev`. If `$ARGUMENTS` names
   a branch, use it. Otherwise, if `git rev-parse --abbrev-ref HEAD` is not
   `dev`, ask once with `AskUserQuestion` which branch this run should commit
   to, and do it *before* any investigation — it is the only blocking question
   in this command. Never switch branches yourself.
2. Rotate the inbox and cluster it:

   ```bash
   python3 .claude/scripts/crash_triage.py scan --claim --against <branch>
   ```

   With no `--version`, the target is the newest `v<x>.<y>.<z>` tag and its
   `AppVersion` from `Telegram/build/version` at that tag, plus the matching
   closed-alpha encoding (`AppVersion * 1000`). Pass `--version` only if
   `$ARGUMENTS` named one.

   `--claim` moves every complete report triple out of `all/` into
   `~/Telegram/Crashes/backup/<stamp>-crashes/` and scans that folder, so the
   next run sees only reports that arrived after this one. Half-written triples
   stay behind. With `--no-claim`, pass `--crashes <previous run folder>` and
   drop `--claim`.

   The scan prints the run folder and a funnel: `usable`, `no-frames`,
   `unsymbolized`, `old-version`, and the group count split into new versus
   already in the ledger. It writes `<run>/triage/` with `groups.md`,
   `groups.json` and `index.tsv`.
3. Clear the reports that need no reading at all:

   ```bash
   python3 .claude/scripts/crash_triage.py sweep --out <run>/triage
   ```

   This *moves* the old-version, `<no frames>` and unsymbolized triples into
   `<run>/triage/_dropped/`; it does not delete them. Only add `--delete` if the
   user asks for it in this run.
4. Report the funnel in one short paragraph, then create a todo list with one
   item per wave (below), so progress stays visible across a long run.

## Phase 1 — plan the waves

Read the group summaries and nothing else:

```bash
python3 - <<'PY'
import json
d = json.load(open("<run>/triage/groups.json"))
for g in d["groups"]:
    if g["known"]: continue
    print("%s  n=%-3d m=%-3d %-28s %s" % (g["gid"], g["count"], g["machines"],
          ",".join(g["tags"]) or "-", (g["assertion"] or g["signature"])[:100]))
PY
```

Groups already in the ledger were decided by an earlier run: skip them, and say
how many you skipped. Order the rest by report count and split them into
**waves of at most 10 groups**. Prefer several small waves over one large one:
each wave is a checkpoint where verdicts reach the ledger, so an interrupted
run never loses finished work. Honour `--limit N` by stopping after N groups
and naming the untriaged groups in the final report.

You may close a group yourself, without an agent, only when its hint is
unambiguous noise documented below — `interceptor`. Everything else gets an
agent, including single-report groups.

## Phase 2 — investigate each wave

Run one `Workflow` per wave. Fan out one investigation agent per group, then
adversarially verify every proposed fix before it is allowed near the tree.

Structure each wave's script as a `pipeline()` over the wave's groups:

- **stage 1 — investigate.** One agent per group, `phase: 'Investigate'`.
- **stage 2 — verify.** For any stage-1 result whose verdict is `fixed`, one
  agent that tries to *refute* the diagnosis and the proposed change,
  `phase: 'Verify'`. Groups that came back `not-actionable` or
  `needs-discussion` skip this stage.

Return the collected verdicts from the script. Both stages are **read-only on
the working tree** — agents run in parallel and must never edit tracked files
or run `git commit`, `git add`, `git checkout`, or `git stash`. They write only
under `<run>/triage/results/`.

Give each investigation agent a self-contained prompt containing the run
folder, the triage folder, its single `<gid>`, the repo path, the tag, and the
target branch, plus these rules:

- Load your own report: `python3 .claude/scripts/crash_triage.py show <gid>
  --out <run>/triage`. Add `--registers` for fill patterns and vtable
  addresses, `--all-threads` for lock-ordering questions, and read
  `<run>/log_<id>.txt` directly for a member you want in full.
- **Dump line numbers are the tag's line numbers.** Read the crash site with
  `git show <tag>:<path>`, never the working tree, or you will reason about the
  wrong lines. `<run>/triage/groups.md` already maps each frame's file name to
  its repo path and flags the ones that moved since the tag.
- Check whether it is already fixed: `git log <tag>..<branch> -- <path>`. If a
  commit on the branch already covers this crash, the verdict is
  `not-actionable` with that commit named.
- Frames marked `Found by: stack scanning` are frequently bogus; `show` already
  hides them. Trust `call frame info` and the instruction pointer frame.
- Prove the path. Name the object whose lifetime or state is wrong, and the
  sequence that reaches the faulting line. A plausible story that you cannot
  trace to the code is `needs-discussion`, not `fixed`.

The verdict is one of exactly three values:

- **`not-actionable`** — nothing to do in tdesktop. Client-side OOM, a graphics
  or audio driver fault, `createDIBSection`/GDI exhaustion, a failing system
  library, the `Deadlock found!` detector firing on a client-side stall rather
  than a real deadlock, corruption from a third-party interceptor, or a crash
  already fixed on the branch. Say which, in one sentence.
- **`needs-discussion`** — real, but the fix is large, spans subsystems, or
  cannot be shown not to regress something. Do not half-implement it. Write the
  diagnosis and what the options are.
- **`fixed`** — the path is confirmed, the fix is small and obviously safe (a
  guard, a lifetime prune, a missing handler), **and it costs nothing
  measurable on any path that was not already crashing**. These commits land
  without review, so a fix that adds work to a healthy path — an extra
  allocation or copy, work moved into a hotter loop, a cheap step reordered
  after an expensive one, a cache made less effective — is `needs-discussion`
  however correct it is. Judge that against the *common* input, not the one in
  the dump: the crashing case is by definition the rare one, and a change that
  is a huge win there can still be a loss everywhere else. If the change alters
  how much work is done or when, say so explicitly, with the sizes involved.
  Describe the change precisely enough for someone else to make it: file,
  function, what to change, why it is safe. Also write it as a patch hint to
  `<run>/triage/results/<gid>.patch` (`git diff` format against the target
  branch) — the main session treats it as a hint, not as something to apply
  blindly.

Every agent writes its full reasoning to `<run>/triage/results/<gid>.md` and
returns a compact structured object: `gid`, `verdict`, `confidence`
(`high`/`medium`/`low`), `title` (one line), `cause` (two sentences at most),
`files` (repo paths), and `note`. Keep the returned object small — the long
form lives in the file.

The verify agent is told the diagnosis and the proposed change, and is asked to
break them: can the null actually occur on that path, does the guard change
behaviour anyone depends on, does the code still look like that on the target
branch, does the patch contradict the surrounding invariants, and **does it
make the common case slower** — an added allocation or copy, more work per
iteration, an expensive call now reached on a path that used to skip it? Cost
the *typical* input rather than the crashing one, and refute on a real
regression there even when the fix is correct. It returns `refuted` (bool)
with a reason. A `fixed` verdict that is refuted becomes
`needs-discussion`, carrying the refutation as its note.

## Phase 3 — apply and commit, serially

Back in the main session, after each wave:

1. For each confirmed `fixed`, make the edit yourself in the working tree,
   reading `<run>/triage/results/<gid>.md` for the detail. Re-ask the
   performance question before you commit — you are the last check, and the
   agent costed the change against the crashing input, which is the rare one.
   If the fix turns out to slow a healthy path, either find the shape that
   does not (usually: apply the cheap operation to whichever side is smaller)
   or drop the group to `needs-discussion`. Apply the change to
   the target branch's current code, not to the tag's — check the surrounding
   code first, since the file may have moved on. Use the `.patch` as a hint
   only.
2. Review the diff and commit it, one commit per crash, per the AGENTS.md
   "Commits" section: a plain ~50-60 character subject, **no** `[ai] ` prefix
   (these are product fixes), no `Co-Authored-By:` or any other trailer. For a
   fix inside `lib_base`/`lib_ui`, commit in the submodule first with the same
   subject as the pointer-bump commit, and `git branch -f master HEAD` there.
3. Record every group of the wave, whatever its verdict:

   ```bash
   python3 .claude/scripts/crash_triage.py record <gid> --out <run>/triage \
     --verdict <fixed|not-actionable|needs-discussion> --note "<one line>" \
     [--commit <sha>]
   ```

   The ledger keys on a version-independent signature, so a crash decided today
   is skipped automatically in every later run. Recording is what makes waves
   resumable — never defer it to the end of the run.
4. **Reconcile before starting the next wave.** Applying and recording are
   separate steps done per group, so a group can silently fall through between
   them. Assert that every gid the wave returned now has a ledger entry:

   ```bash
   python3 - <<'PY'
   import json
   led = json.load(open("<ledger path>"))
   groups = json.load(open("<run>/triage/groups.json"))["groups"]
   todo = [g["gid"] for g in groups
           if g["gid"] in {<this wave's gids>} and g["key"] not in led]
   print("unrecorded:", todo or "none")
   PY
   ```

   Anything listed is a verdict you dropped — go back and finish it. Before
   Phase 4, run it once more over every group the run actually selected —
   never the full group list, since under `--limit` the groups left for a
   later run have no ledger entry by design and would all read as dropped.

Under `--dry-run`, do phases 1–2 and record nothing, commit nothing.

## Phase 4 — build and report

Build **once**, after the last wave, not per fix:

```bash
cmake --build out --config Debug --target Telegram
```

If it fails, fix the fallout and amend the commit that caused it. Then write
`<run>/triage/report.md` and summarise in the chat:

- the funnel (claimed, dropped, groups, of which already known)
- one line per group: gid, report count, verdict, and title
- the commits made, by subject
- everything left as `needs-discussion`, since that is the queue for the user
- anything skipped because of `--limit`, and the run folder path so a later run
  can pick it up with `--no-claim --crashes <run>`

Keep the chat summary short; the detail is already on disk.

## Known noise, worth checking before investigating

- `TelegramCommon.dll` with `ti_x64.dll`/`ti_x32.dll` (PDB
  `TelegramInterceptor.pdb`) is a third-party product that detours the MTProto
  receive path and produces write faults at code addresses. The `interceptor`
  hint flags it. Not actionable.
- A `Binary:` other than `Telegram`/`Telegram.exe`/`Telegram.app` is a clone
  build (`clone-binary` hint). Clones over-represent forced-logout paths, but
  the crash is still real if an official-build dump shares the signature — so
  check the group's other members before dismissing it.
- One `UserTag` behind many reports means one broken machine, not a fleet bug;
  the group header prints the distinct machine count.
- Win32 `0x80808080`-ish register fill means use-after-free; a vtable pointer
  with the low bit set, or a non-canonical `0xb000...`, means corruption.
