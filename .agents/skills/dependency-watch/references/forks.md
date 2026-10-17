# Patched forks and copied source trees

Treat a fork as three distinct states: the revision pinned by Telegram Desktop,
the fork's current upstream branch, and the original project's upstream. New
commits in the fork can be a simple pin update; importing the original upstream
can require preserving a large patch set. Report these separately.

| Dependency | Fork | Original upstream | Import clues |
| --- | --- | --- | --- |
| tg_owt | https://github.com/desktop-app/tg_owt | https://webrtc.googlesource.com/src/ | Source import commits, Chromium M-number, DEPS, nested gitlinks, original revision or commit-position trailers |
| tg_angle | https://github.com/desktop-app/tg_angle | https://chromium.googlesource.com/angle/angle/ | Source import commits, Chromium M-number, version/commit metadata, DEPS, README.chromium |

These mappings seed discovery, not fixed baselines. The initial local inspection
found import subjects mentioning WebRTC M123 and ANGLE M115. Establish the
baseline again from the exact fork pins in each fetched Telegram snapshot.
`tg_angle` is selected by prepare's Windows Qt 5 path; inspect current Qt
selection and supported builds before deciding which users are exposed.

## Establish comparable revisions

Read the fork's history through the pinned revision, including source imports,
submodule updates and later fix commits. Find the original imported revision
from provenance files, import messages, commit-position/Change-Id trailers, or
matching source. Inspect the pinned `desktop-app/patches` revision separately
for each recipe; prepare, Docker and Snap need not use the same patch set.
Include patches already folded into the copied sources.

Compare original upstream's current stable release branch and development head
separately. Resolve Chromium release branches and their DEPS pins where that is
the available mapping. Report milestone/branch, revision, date and age with
evidence. Count commits behind only across demonstrably comparable history.
For flattened copies with no shared ancestry, use a verified import mapping;
never subtract fork and upstream commit counts or assume a failed merge-base
means there is no divergence. Mark an approximate M-number as approximate.

## Find and verify missing fixes

Review the unimported upstream range, upstream/fork advisories, Chromium stable
security notes (https://chromereleases.googleblog.com/search/label/Stable%20updates),
public issue references and nested dependency security updates. A Chrome CVE
mentioning WebRTC or ANGLE is a lead, not proof Telegram includes the vulnerable
path. Check introduced/affected versions, source presence, compiled backend,
build flags and reachable use in Telegram. Features added after our import may
not exist in our fork. Restricted issues or unavailable fixes remain unknown.

For each candidate, inspect the actual fixing diff and prerequisites. Check
ancestry when it exists, equivalent patches/patch IDs when applicable, and the
pinned implementation's semantics. Cherry-picks and rewritten local fixes may
have different hashes and paths; missing upstream ancestry is insufficient to
claim an unbackported vulnerability. Likewise, a matching subject or a patch
that applies is insufficient to claim the fix is present or correct.

Classify security candidates explicitly:

- Confirmed applicable and missing.
- Already present/backported, with the matching code or patch evidence.
- Not applicable, with the absent feature/backend/version evidence.
- Needs investigation: incomplete mapping, private issue, uncertain reachability
  or an unverified equivalent fix.

For a confirmed missing fix, recommend either updating to an existing fork
revision, importing a newer upstream, or backporting. A backport proposal names
the original fix and prerequisite commits, affected fork paths, patch destination
(fork or shared patches repository), likely conflicts with local changes,
platform scope and focused regression/Debug validation needed. If a small fix
depends on a broad refactor, state that; do not describe it as a trivial
cherry-pick. This monitor proposes backports but does not apply them.

Persist the mapped baseline, scanned ranges, stable/development heads,
unresolved candidates and per-fix evidence. Scan new upstream history daily and
revisit new advisories even when their fix commits predate the last scan. Keep
unexamined portions of the old tail visible until reviewed. An upstream-age
entry alone belongs in "Review someday"; identified missing applicable security
fixes belong in "Most pressing updates pending".
