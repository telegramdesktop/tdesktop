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
formatter/wrapper output. It needs no account or network. The fixture uses
`QTemporaryDir` to remove its log tree when the synchronous reading
finishes.

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
