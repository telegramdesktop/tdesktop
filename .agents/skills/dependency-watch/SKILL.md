---
name: dependency-watch
description: Audit Telegram Desktop dependencies on freshly fetched origin/dev for releases and security fixes, including upstream lag and backport candidates in patched forks. Use for the daily dependency monitor or an explicitly requested dependency report.
---

# Dependency watch

Produce an advisory report about dependencies consumed by `origin/dev`. This
workflow does not bump dependencies, build Telegram, create queue records, commit,
push, open PRs, or apply backports. Those are separate implementation requests.

## Snapshot and scope

Run from the repository root:

```bash
python3 .agents/skills/dependency-watch/scripts/watch.py snapshot
```

The helper fetches only `origin/dev`, resolves it once, and reads Git blobs at
that exact commit without switching branches or touching working files. It
prints the snapshot directory and report storage root. A failed fetch is a
failed current check; do not present an older remote ref as freshly checked.
`--no-fetch` is for offline testing only and marks its snapshot as unverified.

Read `snapshot.json`, `candidates.json`, and the relevant files in `sources/`.
Candidates are navigation aids, not a complete parsed dependency inventory.
Inspect the full union of prepare, Docker, Snap, Qt selection, workflow and
package/lock files. Snapshot gitlinks and `.gitmodules` identify exact submodule
revisions; inspect their dependency declarations recursively, especially CMake,
WebRTC, codecs, and bundled image/font/compression/crypto libraries. Add newly
discovered dependencies on every run; never restrict coverage to yesterday's
list. Distinguish shipped libraries, system-provided components and build tools.

Read repository history at the snapshot commit to explain divergent pins and
holds. Derive platform conditions from the recipes. A library disabled in one
configuration can still be relevant on another platform. A source manifest
does not establish what is installed or what already shipped to users.

Store research caches and reports in the helper's `storage_root`, inside Git's
common directory. Read its previous `latest.json` and `latest.md` when present.
Inspect external Git repositories through APIs or isolated caches there; do not
fetch, checkout, or reset the user's prepared libraries or live submodules.
Do not execute downloaded build scripts or import the prepare script to read it.

## Release and advisory checks

For every dependency record its identity, upstream, current version/revision,
manifest locations, platform/configuration, release scheme and supported series.
Fetch official release/tag metadata and release notes; check security advisories
independently, including advisories published since a release. Search vendor
security pages and GHSA/OSV/CVE sources as appropriate; native C/C++ projects may
not have package identifiers in advisory databases. Absence from a database is
not evidence of safety. Cite primary sources and dates for actionable claims.

Use `watch.py compare CURRENT CANDIDATE` for ordinary three-component stable
versions. It rejects prereleases and unsupported schemes as `unknown`; inspect
upstream version policy for those instead of forcing SemVer on commit hashes,
dates, Chromium milestones, OpenSSL legacy letters, or four-component versions.
For 0.x, also assess compatibility from upstream notes. Compare all maintained
release series we consume, not just the upstream API's single latest release.

Read [release-trust.md](references/release-trust.md) before recommending any
candidate. Track update urgency separately from the decision to update,
backport, hold, skip, or track. A patch number sets a review priority; it does
not establish release trust, compatibility, or readiness to adopt.

Rank findings as follows, subject to that assessment:

- **Strong suggestion:** a newer stable patch in a consumed series that passes
  the release assessment, or a confirmed applicable security fix missing from
  our pinned sources/patches with an assessed update/backport route.
  Known exploitation or exposed memory-safety flaws rank above routine patches.
- **Weaker suggestion:** a minor release, with concrete benefits and migration
  costs where known.
- **Track only:** a major release or upstream milestone gap without an identified
  applicable security fix. Track end-of-support dates; an unsupported consumed
  branch or security fix available only in a newer series can require urgent
  migration despite its version classification. A suspect replacement does
  not become acceptable merely because the current branch is unsupported.

An intentional hold does not hide patch releases or security findings. Show the
reason for the hold beside the recommendation. Deduplicate the same dependency
across recipes, preserving different platform series and patch revisions.
For distro-provided packages, check the distribution's full package revision,
security tracker and backports. Record artifact/package resolution as unknown
when unavailable; an old upstream version alone does not prove vulnerability.
Track Docker base images, mutable package installs and Snap runtime/content snaps
as separate update/rebuild concerns even when no source pin changed.

Read [forks.md](references/forks.md) when checking patched or copied upstreams;
always apply it to `tg_owt` and `tg_angle` when present in the snapshot.

## Incremental work and evidence

On the first audit establish a baseline across all discovered dependencies.
On subsequent runs reuse verified source mappings, imported revisions and
backport evidence, but refresh release/advisory metadata daily even if `origin/dev`
did not move. Recheck evidence when a pin, patch set, relevant code, upstream fix
or advisory changes. First inspect high-exposure libraries and existing urgent
findings, then finish the rest. Follow API pagination; conditional requests and
cached Git objects can reduce work without skipping unresolved history.

Maintain `latest.json` with the snapshot commit/time, per-dependency check times,
coverage (`checked`, `partial`, `unavailable`, or `not-checked`), current and
candidate versions, findings, source links, and fork evidence. Give each finding
a stable identity based on dependency, consumed series/platform and advisory or
target version. Record first/last seen and whether it remains pending. Preserve
pending findings across failures; only resolve them with evidence from the new
snapshot. A revision change alone does not prove that a fix was incorporated.
Persist each candidate's decision and reason, release date, assessed source
identity, trust/regression evidence, unknown checks, and next review date or
condition. A hold or skip does not resolve an outstanding security problem.

If access or time prevents full coverage, save a partial report naming the gaps
and last successful checks. Never write "all up to date" after an incomplete
scan. Record stale security coverage prominently when it affects pressing items.
A first run may need substantial fork archaeology; keep its evidence and resume
unresolved ranges next time instead of repeatedly starting over. A keyword scan
of recent commit subjects does not complete a security review of an older tail.

## Report contract

Save `report.md` and `findings.json` beside that run's snapshot, then replace
`latest.md` and `latest.json` in the storage root using temporary files and atomic
rename. Preserve prior run reports. Date reports in Asia/Dubai and give the exact
`origin/dev` commit, fetch time, and whether the audit was complete or partial.

Use two main blocks, in this order:

1. **Most pressing updates pending.** All unresolved strong suggestions, highest
   urgency first, including previously reported items. For each: dependency,
   affected platforms, current → target version or fix, why it matters, evidence,
   existing patch/backport status, and the recommended update/backport action.
   Distinguish confirmed missing fixes from urgent investigations whose
   applicability or backport status remains unknown. Keep urgent holds or
   rejected replacements here when the existing dependency still needs action;
   state the safer alternative and what would unblock the decision.
2. **Review someday.** Minor upgrades as weaker suggestions; major upgrades as
   tracking entries; fork milestone/revision lag; routine holds and skipped
   candidates with reasons; unavailable checks, unclassified versions, and
   other coverage limitations. Clearly mark
   newly discovered, changed and resolved findings and link the full inventory.

Both blocks must be present, even when empty. End with a compact coverage line
(checked/total, unavailable checks and oldest outstanding coverage). A scan of
source declarations alone must not claim that distributed binaries are fixed.
The scheduler decides when to post this report in the chat; the saved report
always retains the complete pending list.
