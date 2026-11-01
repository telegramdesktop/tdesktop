# test_log_scan_internal

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Internal dependency.

Source contract: [test_log_scan_internal.h](../../test_log_scan_internal.h).

## Use when

Maintaining the shared log reader used by `test_secrecy_scan.cpp` and
`test_publication_identity_scan.cpp`. Product overlays use the public
[secrecy](test_secrecy_scan.md) or
[publication and coverage](test_publication_identity_scan.md) APIs.
An absence of direct overlay includes does not mean this module is unused.

## Contract and ownership

`Internal::ReadLogClasses` is implemented in `test_secrecy_scan.cpp`.
It reads the supplied `SecrecyLaunchLogs` and planted control, returning
class readings, candidate identities and accepted, foreign, missing and
unreadable counts. Optional computed fields and usable secrets select the
strict scan; a null `usable` skips strict parsing. `LogScanObserver` receives
source and line callbacks synchronously, so its state must remain valid
through the call. A coverage reading alone does not prove fixture secrecy.

## Verification

Changes to this reader affect both `AppendSecrecyScanSelfTest` and
`AppendPublicationIdentityScanSelfTest`; exercise the relevant controls in
both suites. Their contracts and fixture requirements live in the public
references above.
