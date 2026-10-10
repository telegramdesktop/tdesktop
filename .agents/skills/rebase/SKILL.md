---
name: rebase
description: Drive an intent-aware rebase of the current checkout, resolving every conflict by reading the history behind both sides instead of by making the markers disappear. Use when the user invokes $rebase or /rebase, asks to rebase a branch onto another ref, to move the tail of a branch onto a new base, or to continue or abort a rebase that is already in progress.
---

# Intent-Aware Rebase

Run one rebase in the current checkout and resolve every conflict by
understanding what each side was trying to do. A conflict is two authors
disagreeing about code, not a syntax error: the resolution is the code they
would have written together, expressed in the shape the upstream side has now.

The request is whatever the user typed with the invocation. When running in
Claude Code, read `.claude/commands/rebase.md` for that host's substitutions;
when running in Grok Build, read `.grok/commands/rebase.md`.

## Hard rules

- Operate only in the current worktree. Never write into a sibling checkout
  slot, and never rebase a branch that `git worktree list` shows checked out
  elsewhere — say where it is instead.
- Never push, never force-push, never delete the backup ref, never touch
  `origin`/`private` refs beyond an explicitly confirmed fetch.
- Never run `git rebase -i`, `git add -i`, or anything else that opens an
  editor; these hosts have no interactive terminal. Use
  `git -c core.editor=true` for every `rebase --continue`. If commits must be
  dropped or reordered, do it with a non-interactive `GIT_SEQUENCE_EDITOR` and
  show the todo list you are producing.
- Never resolve with `git checkout --ours/--theirs <file>`, never delete a hunk
  to make the build pass, never leave a conflict marker, never `--skip` a
  commit to escape a hard conflict.
- This is a branch-integration operation. It is never an `ai-tdesktop` task:
  do not create, claim, or publish task records for it, and do not route it
  through `perform-task`, `continue`, or `process-inbox`.
- Resolve conflicts in this session, with both sides in context. Delegation is
  for gathering, never for deciding: a leaf may summarize history you name, but
  it must not write a resolution.

## Host mechanics

- **Asking the user.** Where this document says to ask, use the host's
  structured question mechanism if it has one, and otherwise end the turn with
  a short numbered question and stop. Never continue a rebase past a question
  you have not had answered.
- **Checkout kind.** In a native Windows checkout run git directly. In the
  WSL checkout reached through `\\wsl.localhost\...`, route every git command
  through `wsl.exe -d {distro} --cd /home/{user}/Telegram/tdesktop -- git ...`
  per `AGENTS.md`; native Windows git over that UNC path fails with
  `detected dubious ownership`.
- **Line endings.** Keep each file's existing convention — CRLF in a native
  Windows checkout, LF in the WSL checkout — and never add a BOM. A
  `git diff --stat` that shows a whole-file rewrite means you broke them.

## 1. Read the request

Accept free prose. The shapes to recognize:

| Request | Command |
| --- | --- |
| `onto <ref>` | `git rebase <ref>` for the current branch |
| `<branch> onto <ref>` | `git rebase <ref> <branch>` |
| `tail of <branch> on top of <ref>` | `git rebase --onto <ref> <base> <branch>` — `<base>` is the last commit that stays behind, exclusive |
| bare `<ref>` | treat as `onto <ref>` |
| empty | current branch onto its upstream; if there is none, ask |
| `continue` / `resume` | jump to §7 |
| `abort` | `git rebase --abort`, report the restored tip, stop |

When the user proposes a literal command ("I believe command should be
`git rebase --onto private/alpha abcdefg alpha`"), take it as a strong hint,
not as gospel: resolve its refs and print the commit list it would actually
replay. If that list disagrees with the prose — wrong `<base>`, a commit that
should stay behind, a `<base>` that is not an ancestor of `<branch>` — say so
plainly with the evidence and propose the corrected command.

Remote-tracking targets (`private/*`, `origin/*`) are local snapshots. Do not
fetch on your own: print the target's sha, subject and commit date so staleness
is visible, and ask before fetching if it looks old or the user mentions
freshness.

Record these once and reuse them everywhere below:

- `ONTO` — the new base, resolved to a sha.
- `REPLAY_BASE` — exclusive start of the replayed range (`<base>` for the
  `--onto` form, otherwise `git merge-base <ONTO> <branch>`).
- `OLD_TIP` — the branch tip before the rebase.

Print the plan: the exact command, `ONTO` (sha, subject, date), the replay list
`git log --oneline REPLAY_BASE..OLD_TIP`, and the likely conflict surface —

```bash
comm -12 \
  <(git diff --name-only REPLAY_BASE OLD_TIP | sort) \
  <(git diff --name-only REPLAY_BASE ONTO | sort)
```

Proceed without asking when the reading is unambiguous. Ask when two readings
would replay different commits, or when the user's proposed command and prose
disagree.

## 2. Preflight

- In-progress rebase — `git rev-parse --git-path rebase-merge` /
  `rebase-apply` exists: do not start a new one. Go to §7.
- Dirty tree — `git status --porcelain` non-empty: stop and ask (commit,
  stash, or cancel). Never stash silently.
- Backup, always, before the first replay:

```bash
git branch backup/<branch>-$(date +%Y%m%d-%H%M%S) <branch>
```

  matching this repo's existing `backup/wallet-engine-20260903` convention.
  Report the ref name and `OLD_TIP` in the final summary as the undo path.
- `rerere.enabled` is true in this repository, so past resolutions get replayed
  automatically. That is a hazard, not a convenience — see §6.

## 3. Start

```bash
git -c core.editor=true rebase [--onto ONTO] [REPLAY_BASE] <branch>
```

If the replayed range contains merge commits, say so and ask whether to flatten
them (default) or pass `--rebase-merges`.

## 4. How much to read

Keep git's auto-merge. Hunks that do not overlap are merged deterministically
and correctly; re-deciding them by hand costs enormously and is *less* reliable
than git, because the failure mode is silently dropping something. Read a whole
file only when it is small, or when the conflict cannot be judged from its
region alone.

Tier the effort per conflict, judged from one pass over the three stages:

- **Mechanical** — both sides append to the same include block, list, enum,
  `.style` block or switch; whitespace; adjacent unrelated additions. Read the
  region, keep both, move on. No history dig.
- **Overlapping logic** — both sides changed the same function, the same
  condition, or the same behavior. Full §5 protocol.

The gap auto-merge leaves is the *semantic* conflict: hunks that merge cleanly
but are wrong together — upstream renamed a symbol this commit calls, changed a
signature it passes to, or moved the call site it hooks into. Git never reports
these. Cover them cheaply rather than by reading everything:

- Before `--continue`, record `OLD=$(git rev-parse REBASE_HEAD)`; after the
  commit lands, `git range-diff $OLD^..$OLD HEAD^..HEAD` and read whether the
  change arrived intact.
- For a commit that had any conflict, compile the translation units it touched
  (§5.7). In the native Windows checkout that is ~11s per file and catches
  nearly all rename and signature drift.
- The whole-rebase `git range-diff` in §8 is the net under all of it.

Do not read files that merged cleanly, do not re-review commits that replayed
without conflict, and do not build after every commit.

## 5. For every conflict: read before you write

Orient once per rebase, because the sides are counter-intuitive during a
rebase and getting them backwards silently reverts work:

- stage 2 = `HEAD` = `--ours` = **the `ONTO` side**: the new base plus the
  commits already replayed.
- stage 3 = `REBASE_HEAD` = `--theirs` = **the commit being replayed**, i.e.
  the work being moved.

Confirm it once with `git show :2:<file> | head` against known upstream content
before trusting it for the rest of the run.

Then, per conflicted file:

1. **Get the three sides clean.** The default marker style hides the base, so
   read the stages directly rather than parsing markers:

```bash
git show :1:<file>   # merge base
git show :2:<file>   # ONTO side
git show :3:<file>   # replayed commit's side
```

   For large files, dump the stages to a temporary location and diff them
   pairwise. Before you have edited anything you may also re-materialize the
   conflict with the base included:
   `git checkout --merge --conflict=zdiff3 -- <file>`.

2. **Learn the replayed commit's intent.**

```bash
git log -1 --format='%h %s%n%n%b' REBASE_HEAD
git show REBASE_HEAD -- <file>
git log --oneline REPLAY_BASE..OLD_TIP            # the whole series it belongs to
git log -p REPLAY_BASE..OLD_TIP -- <file>         # its neighbours in that file
```

   A commit is usually one step of a multi-commit design. Resolve for the
   design, and read the commits that come *after* this one in the series: if a
   later commit rewrites the same lines, do not resolve into something it is
   about to contradict.

3. **Learn the `ONTO` side's intent.**

```bash
git log --oneline REPLAY_BASE..ONTO -- <file>
git show <sha> -- <file>                          # for each relevant one, subject + body
git log -L <start>,<end>:<file> REPLAY_BASE..ONTO # history of the exact region
```

   If `-L` rejects the range, fall back to
   `git log -p REPLAY_BASE..ONTO -- <file>`. If the code the commit touched has
   moved or vanished upstream, find where it went:
   `git log --oneline --diff-filter=RD REPLAY_BASE..ONTO -- <file>` and
   `git log -S<symbol> REPLAY_BASE..ONTO`. `git blame` the surviving region.

4. **State both intents in one sentence each**, in the running report, before
   editing. If you cannot state them, you have not read enough — keep reading.

5. **Resolve by re-applying intent, not text.** The result must be what the
   replayed commit's author would have written had they started from today's
   upstream code:

   - Upstream refactored, renamed, or moved the code → apply the commit's
     change in the new shape and the new location.
   - Upstream already does the same thing differently → keep upstream's
     version and add only what is genuinely still missing.
   - Both sides add to the same list, switch, enum or `.style` block → keep
     both entries in a sensible order.
   - Upstream deleted what the commit modified → apply the behavior where it
     lives now; never resurrect deleted code to host a patch.
   - New glue code you have to write follows `AGENTS.md` and `REVIEW.md` —
     no narrating comments, `auto`, `u"..."_q`, existing naming.

6. **Genuine contradiction → ask.** A contradiction is not "this is hard"; it
   is two goals that cannot both hold: upstream removed the feature the commit
   extends, upstream changed the semantics the commit depends on, or both sides
   changed the same user-visible behavior in opposite directions. Then ask,
   giving the file and region, one sentence per intent naming the commits that
   establish them, and 2–4 concrete directions (keep upstream's behavior and
   drop this hunk / port the change onto the new semantics / keep both under a
   condition / …). Never guess when the guess would silently change product
   behavior. Equally, never ask about a mechanical conflict you could have
   resolved by reading.

7. **Verify, then stage.**

```bash
grep -nE '^(<{7}|={7}|>{7})' <file>            # must print nothing
git diff -- <file>                              # read the final region
git diff REBASE_HEAD^ REBASE_HEAD -- <file>     # every part of the original change accounted for?
git add <file>
```

   In the native Windows checkout, a non-trivial `.cpp` resolution is worth the
   ~11s single translation unit compile:

```bash
"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" \
  "C:\Telegram\tdesktop\out\Telegram\Telegram.vcxproj" \
  -t:ClCompile -p:SelectedFiles="C:\Telegram\tdesktop\Telegram\SourceFiles\<rel\path>.cpp" \
  -p:Configuration=Debug -m -nologo -v:minimal 2>&1 | grep -iE "error|warning C" | head
```

   The WSL checkout has no cheap per-file equivalent — its Docker build is the
   whole app — so there, lean on reading and on §8 instead of compiling.

   Then `git -c core.editor=true rebase --continue`.

## 6. Two traps

**rerere.** When git prints `Resolved '<path>' using previous resolution`, that
file was resolved from a recorded past resolution and staged without anyone
reading it. Treat it as a conflict that arrived pre-filled: run §5 steps 2–4 on
it and read `git diff --cached -- <path>`. If it is wrong,
`git rerere forget <path>` drops the recording and restores the conflicted
state; resolve it by hand from there.

**Empty commits.** If a commit's change is already upstream, git reports the
patch as empty. Confirm it by comparing the commit's intended end state against
`ONTO` — not by the fact that the diff is empty — then `git rebase --skip` and
record the skip with its evidence in the report. A hard conflict is never a
reason to skip.

## 7. Resume

Entered on `continue`, or when §2 found a rebase already in progress. Rebuild
the picture before touching anything: `git status`, `git log -1 REBASE_HEAD`,
`git log --oneline ONTO..HEAD`, and `.git/rebase-merge/` for the todo list and
the original head. Recover the running report if this session wrote one;
otherwise reconstruct it from the log. Then continue at §5.

If the state is one you do not understand, stop and report it as it stands. Do
not abort to tidy up — the user may want to inspect it, and the backup ref
makes recovery cheap either way.

## 8. Verify the whole rebase

```bash
git range-diff REPLAY_BASE..OLD_TIP ONTO..HEAD
```

Read it commit by commit. Every commit should map 1:1; investigate each one
marked with a changed diff and every commit that disappeared. This is the check
that the rebase preserved intent, so do not skip it and do not report success
before reading it.

Then `git log --oneline ONTO..HEAD` and `git status`. Build only when the user
asks or when the resolutions were substantial — the checkout's Debug build per
`AGENTS.md`, in the background — and report the real result, failures included.

## 9. Report

- The command that ran, and the backup ref plus `OLD_TIP` sha as the undo path
  (`git reset --hard <backup-ref>`).
- Commits replayed / skipped, with the reason for each skip.
- One line per conflict: file, the two intents, the resolution chosen.
- Every question asked and the answer that decided it.
- Anything resolved with lower confidence, named explicitly so the user can
  look at it.
- If the branch was published, state that it now needs a force-push — and do
  not perform it.

Do not commit or push anything beyond what the rebase itself writes.
