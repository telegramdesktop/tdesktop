---
description: Review uncommitted changes or the last N commits with independent Codex reviewers, then arbitrate, fix and re-review until it converges
allowed-tools: Read, Write, Edit, Glob, Grep, Bash, Agent, AskUserQuestion, TodoWrite
---

# Cross-Provider Review

Read `.agents/shared/cross-review/protocol.md`,
`.agents/shared/cross-review/prompts.md` and
`.agents/shared/codex-cli-leaves.md` completely. You are the runner and the
final arbiter; every reviewer is a Codex CLI leaf. Do not review with Claude
`Agent` leaves instead: a cross-provider review is the point of this command.

## Arguments

Read the arguments below as natural language:

- nothing: the uncommitted changes;
- "N commits", "last N commits" or `HEAD~N`: the last N commits;
- "max K rounds": a round cap other than ten;
- "review only" or "no fixes": one round, arbitration and a report, with no
  edits;
- "use codex-default for review": run every Codex command below with
  `--home default` instead of the `work` home;
- "resume" with a session path, or "resume last": continue an earlier session
  from its ledger and its first incomplete step;
- anything else: extra focus passed to every reviewer as `<FOCUS>`.

## Run

1. Run `codex_leaf.py check`. If it fails, report the reason and stop.
2. Freeze the scope with `cross_review_scope.py init`, run the round-1
   `snapshot`, and tell the human the session directory and the scope's file
   count and size in one line. An empty scope ends the command.
3. For each round, fill the prompts from `prompts.md` into
   `SESSION/roundR/prompts/` and start every reviewer of the round at once:

   ```bash
   python3 .agents/shared/scripts/codex_leaf.py start \
     --run-dir SESSION/runs --name rR-<role> --cwd SOURCE_ROOT \
     --prompt-file SESSION/roundR/prompts/<role>.prompt.md \
     --sandbox read-only --effort xhigh
   ```

   Then call `codex_leaf.py wait --run-dir SESSION/runs` in the foreground
   until it exits 0. Reviews at this effort can take a long time; keep
   waiting in this turn without commentary, and never end the turn while a
   reviewer runs.
4. Copy each `SESSION/runs/rR-<role>.last.md` to `SESSION/roundR/<role>.md`.
   A report without its `## Verdict:` line, or a `failed` leaf, is rerun once
   with `--replace`. If a lens fails twice, continue without it and list the
   gap in the report; if the general reviewer fails twice, stop and report.
5. When lens reviewers ran, start the synthesis leaf `rR-synthesis` with
   `--resume rR-general` and the synthesis prompt, wait for it, and save it as
   `SESSION/roundR/review.md`. Without lens reviewers, the focused general
   report is the round's review.
6. Arbitrate every finding under the protocol. Read the code yourself, check
   the ledger for reversals, and write each decision to `SESSION/ledger.md`
   before editing anything.
7. Apply the FIX decisions unless the arguments asked for review only. Follow
   AGENTS.md and REVIEW.md. When C++, CMake, style or resource files changed,
   build Debug as AGENTS.md describes and prove the build per its rules; fix a
   broken build before the next round. Snapshot the next round, then check
   the comment rules over that snapshot with
   `GIT_INDEX_FILE=SESSION/index python3 tools/rules/hook.py --staged`.
8. Apply the protocol's stop conditions, then start the next round's focused
   reviewers or finish.

Never commit, amend, rebase or stash. When the loop ends, give the protocol's
report in a few short paragraphs and offer to commit the fixes.

Arguments:

```text
$ARGUMENTS
```
