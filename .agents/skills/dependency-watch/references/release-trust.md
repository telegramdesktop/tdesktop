# Assess whether to adopt a release

Review priority and adoption readiness are separate. Newer code can close a
vulnerability, introduce a regression, or carry a compromised release. Assess
both the risk of keeping the current pin and the risk of the proposed change.
This monitor recommends actions; it does not install or approve deployment.

## Evidence proportional to the change

For each actionable candidate, record its publication date, canonical source,
exact tag object and peeled commit (or package revision / artifact digest), and
the source form our recipe consumes: Git, release archive, binary, or distro
package. Resolve mutable tags to immutable identities in the evidence. Recheck
changed tags/assets against the previous run; explain legitimate replacements
instead of silently accepting them.

Check upstream advisories and release/issue discussions for withdrawals,
compromise reports and regressions affecting our platforms or enabled features.
For high-exposure dependencies, cross-check a distribution security tracker or
another independent authoritative source. Reposts of one announcement are not
independent validation. Record unavailable checks instead of implying clearance.

Inspect the relevant source and packaging/build changes, including new install
hooks, generated scripts, binary/test blobs, downloads and unexpected dependency
changes. A much larger diff than the announced fix or a change in publisher,
signer, repository ownership or release process merits investigation, not an
automatic accusation. Expand this review for a new maintainer or known incident.

Use signatures, attestations and reproducibility evidence where available, and
compare signer identity with established project history or separately published
keys. Never call a signature verified unless verification actually succeeded.
A signature proves attribution/integrity under its trust assumptions, not benign
code; an authorized or compromised maintainer can sign malware. An unsigned
project is not automatically compromised. A checksum on the same compromised
release page is not independent proof either.

When archives include generated files absent from Git, assess those differences
if the recipe consumes the archive. Git builds still require source/build-input
review. Do not download or execute known malicious releases as part of this
monitor. Build/test success cannot rule out a targeted backdoor.

## Decisions

- **Update:** expected benefit warrants adoption; relevant release trust and
  regression checks found no unresolved concern. Cite what was checked and
  propose focused implementation validation. Do not claim the release is proven
  safe or that a build was tested by this advisory monitor.
- **Backport:** an applicable fix can be taken from a reviewed source with less
  disruption than the full release/import. Inspect the diff and prerequisites;
  backports receive the same trust assessment as releases.
- **Hold:** a release is very fresh, essential evidence is missing, a relevant
  regression is unresolved, or a trust change needs investigation. State the
  missing check or release condition and revisit it daily. Do not describe
  missing evidence as proof of compromise.
- **Skip:** a specific candidate is withdrawn, known compromised, incompatible
  with a required platform, or superseded by a suitable release. Name the exact
  candidate, evidence and alternative; do not blacklist the whole project.
- **Track:** no adoption case yet, such as a major upgrade or milestone gap
  without a verified applicable benefit.

For routine bugfix releases, use seven days after publication as a default
observation window before recommending adoption. This is a configurable caution
against very fresh regressions, not a safety guarantee. Elapsed time alone never
clears a trust or compatibility concern. A confirmed applicable security fix,
especially active exploitation, can justify immediate review and adoption or a
minimal backport without waiting seven days; explain that tradeoff. An unverified
CVE/version match does not establish urgency. If both staying and upgrading are
risky, keep the problem pressing and recommend a reviewed fix, mitigation or
alternative rather than indefinite silence.

Persist `decision`, `decision_reason`, `release_published_at`,
`source_identity`, `trust_checks`, `regression_checks`, `unknown_checks`, and
`review_after` or `review_condition`. Older findings without these fields need
assessment; do not inherit an unconditional update recommendation. Skipped or
held targets remain recorded and do not erase unresolved vulnerabilities.

## XZ incident: a standing case to check, not a permanent ban

The [maintainer's incident account](https://tukaani.org/xz-backdoor/) identifies
the compromised 5.6.0 and 5.6.1 release tarballs, and the recovery releases of
29 May 2024. The [original disclosure](https://www.openwall.com/lists/oss-security/2024/03/29/4)
explains the tarball-only build trigger and malicious payload files in Git.
The [review notes](https://tukaani.org/xz-backdoor/review.html) document recovery
work. Reject those compromised candidates; assess later versions from current
evidence rather than assuming all XZ releases are bad or all later ones safe.

The initial Telegram snapshot uses Git plus CMake for XZ: 5.4.5 in prepare and
5.8.1 in Docker. Those pins are outside the named compromised release pair, and
the recipes differ from the tarball/Autotools activation path. Re-establish this
from each snapshot; it is not a blanket clearance of build hosts or binaries.
For a proposed update, inspect the exact Git target and relevant build changes,
verify available release provenance, check regressions and assess the fix's
reachability. Reassess old-branch backports too; do not assume freezing at 5.4.5
is safer than every later release.
