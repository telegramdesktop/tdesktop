# test_publication_identity_scan_self_test

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Implementation: [test_publication_identity_scan_self_test.cpp](../../test_publication_identity_scan_self_test.cpp).
Entry contract: [test_publication_identity_scan.h](../../test_publication_identity_scan.h).

## Use when

Verifying the [publication-identity and launch-log coverage readers](test_publication_identity_scan.md),
especially their claim separation from strict fixture secrecy, and the
evidence-relative [`SCREENSHOT` marker](test_capture.md#saved-images-and-the-screenshot-marker)
those readers scan. This suite does not need routine calls from product
overlays.

## Fixture and verification

`AppendPublicationIdentityScanSelfTest(runner)` adds seven synchronous
stages. Six run over synthetic log trees: coincidences, supported
presentations, enumeration, accepted selection, claim separation and
formatter output. These synthetic checks need no account or network. The
fixture uses `QTemporaryDir` to remove its log tree when the synchronous
reading finishes.

The sixth stage, `publication_self_output`, ends with the actual
`CheckLaunchLogEnumeration()` and `CheckPublicationIdentity()` wrappers over
this launch's real logs. Their coverage needs a Send and a Recv header in
this launch's MtpLog, which only real MTP traffic writes; an account or a
session does not imply it. After its nine synthetic checks the stage reads
both header counts through `ReadLaunchLogEnumeration(SelectThisLaunchLogs({}), {})`,
which plants and prints nothing and whose empty control claims no
coverage, then notes one fixed row:

```text
NOTE: PUBLICATION_SELF_WRAPPER_GATE: send_headers=<n> recv_headers=<n> applies=<0|1>
```

With either count zero (`applies=0`) neither wrapper runs, so no wrapper
setup or result row follows; the stage's results end with exactly:

```text
TEST_RESULT: N/A: publication self-test: actual healthy wrapper 0 - this launch's MtpLog lacks a Send or Recv header
TEST_RESULT: N/A: publication self-test: actual healthy wrapper 1 - this launch's MtpLog lacks a Send or Recv header
```

These N/A rows are results that establish nothing about the wrappers; the
nine synthetic checks and their notes before the gate still apply, so a
launch without the pair reads that stage with no FAIL. With both headers
(`applies=1`) every wrapper assertion stays mandatory: each call returns
healthy with no new failure, one setup and one scan row, exactly 16
(coverage) or 36 (identity) appended lines, fixed rows and no presentation
or control token. A real log can still refuse for another reason, such as
a relevant string over the parser's limit, and that stays a FAIL. A
campaign that needs the wrapper checks to run waits for the Send/Recv pair
before appending the pack.

The seventh stage, `publication_self_screenshot_marker`, protects
`SaveImage`'s marker. It applies only when `EvidenceDir()` has exactly one
directory component named `synthetic_marker_collision_handle`, for example a
fresh `test-run --run-dir <dir>/synthetic_marker_collision_handle`;
otherwise it writes `TEST_RESULT: N/A`, which establishes nothing. It saves
five probes through the real `SaveImage` into a fresh evidence root: plain,
padded Unicode, quote, literal escape and mixed separators. For each it
requires exactly the independently written row with an LF or CRLF
terminator, the expected absolute return path and the probe's exact pixels
on reload. Windows cannot store the last three names; there each may be
refused, with an empty return and no marker. Every actual row then gets a
decided, controlled strict reading for that one username, with no finding.
The same reader finds the handle exactly once in the plain file's historical
absolute row, built in memory and never logged, and in the actual relative
row of a probe named after the handle. A null image and a missing parent
directory must each return empty and append nothing. Its checks print only
labels, counts and match flags, never a path or a raw row. The probe images
stay in the run's `screenshots` folder as evidence; a rerun needs a fresh
root.

The checks cover positive matches, coincidence negatives, missing coverage,
selection and parser bounds, and actual output. A passing identity or
coverage result must never become a strict-secret verdict. Read the public
reference for the finite grammar and named refusals these controls protect;
record executed self-test results in the change's evidence.
