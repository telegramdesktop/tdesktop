# test_publication_identity_scan_self_test

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Implementation: [test_publication_identity_scan_self_test.cpp](../../test_publication_identity_scan_self_test.cpp).
Entry contract: [test_publication_identity_scan.h](../../test_publication_identity_scan.h).

## Use when

Verifying the [publication-identity and launch-log coverage readers](test_publication_identity_scan.md),
especially their claim separation from strict fixture secrecy. This suite
does not need routine calls from product overlays.

## Fixture and verification

`AppendPublicationIdentityScanSelfTest(runner)` adds six synchronous stages
over synthetic log trees: coincidences, supported presentations, enumeration,
accepted selection, claim separation and formatter/wrapper output. It needs
no account or network. The fixture uses `QTemporaryDir` to remove its log
tree when the synchronous reading finishes.

The checks cover positive matches, coincidence negatives, missing coverage,
selection and parser bounds, and actual output. A passing identity or
coverage result must never become a strict-secret verdict. Read the public
reference for the finite grammar and named refusals these controls protect;
record executed self-test results in the change's evidence.
