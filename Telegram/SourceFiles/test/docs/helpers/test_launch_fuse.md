# test_launch_fuse

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_launch_fuse.h](../../test_launch_fuse.h).

## Use when

Assert intended OS launches while blocking them. Read the contract below and the source header before use.
For an argument that carries a credential, such as a token URL, use the
[private expectation](#private-expectations).

## Contract and usage

Declare and verify operating-system launches while refusing every real launch
in test-agent mode.

The five `Platform::File::Unsafe*` wrappers call
`Test::BlockLaunch(function, argument)`. In a test run it records the launch in
`BlockedLaunches()`, fires `launch_blocked` and refuses it: a launch declared
in advance with `Test::ExpectBlockedLaunch(argument)` PASSes and consumes that
declaration, and any other launch FAILs. A scenario closes with
`CheckNoBlockedLaunchExpectations` (a declared launch that never happened
FAILs), `CheckBlockedLaunchesExactly` (the whole record, in order) or
`CheckNoBlockedLaunches` (nothing beyond the declared launches). A public
expectation prints its argument in full in every one of these rows.

## Private expectations

For an argument that carries a credential, such as a server-issued token URL
a campaign also hands to `Test::CheckSecrecy`, call
`Test::ExpectPrivateBlockedLaunch(argument, label)` instead of
`ExpectBlockedLaunch`. It matches only the exact argument and is consumed by
one matching launch, like a public expectation. `BlockedLaunches()` still
records the real argument in memory, so `CheckBlockedLaunchesExactly` compares
real arguments.

Every row that would print a covered argument prints
`private:<label> <scheme://host>` instead. `scheme://host` is the URL's scheme
and its fully encoded host, never its userinfo, port, path, query or fragment;
an argument that does not parse as a URL with both a scheme and a host, such
as a file path, prints `(not a url)` in its place. That text appears in the
registration Note, the PASS of the matched launch, the FAIL of a repeated
launch, the leftover list of `CheckNoBlockedLaunchExpectations`, and the
record text of `CheckBlockedLaunchesExactly` (its Note and its mismatch
details) and of `CheckNoBlockedLaunches` (its "already failed" Note). The
coverage lasts the whole run: a consumed argument stays covered, so a repeat
launch or a later record never prints it whole.

While a private expectation is live, a launch that matches no expectation
still FAILs as `blocked launch: Platform::File::<function>`, with the details
`withheld <scheme://host> while private expectations are live: ...`: the
launched argument's `scheme://host`, then each live private text, separated
by semicolons. The launched argument is never printed whole, because it may
be a private argument the product changed. From then on it prints as
`withheld <scheme://host>` in the record. A launch that matches a public
expectation while a private one is live keeps its public row. When no private
expectation is involved, public and undeclared rows keep their exact text.

Matching is exact only; there is no prefix form. A product path can change a
URL before the launch. `UiIntegration::handleUrlClick` in
`core/ui_integration.cpp` first strips `tgWebAuth*` and `autologin_token`
parameters from an http(s) URL on a whitelisted host through
`UrlWithoutWebAuthTokens` (`core/click_handler_types.cpp`), unless the context
sets `keepWebAuthTokens`. Then `UrlWithAutoLoginToken`
appends `autologin_token=` for a host listed in the account's
`autologin_domains` app config, re-encodes the URL through `QUrl::toEncoded()`
and upgrades `http` to `https`. Such a launch fails closed: it FAILs as
withheld and leaves the private expectation as a leftover, and nothing private
is printed. The credential URLs of
`2026/10/16/check-gift-withdrawal-passwords-with-the-current-srp-id-and-reuse-them`
and
`2026/10/18/fall-back-when-the-server-refuses-an-expired-gift-withdrawal-temporary-password`
were both launched exactly.

Limits: the host itself is printed, so a host that is itself secret is not
covered, and the label is printed as given.

## Verification

`Test::AppendLaunchFuseSelfTest(runner)` is the module's self-test. Register
it alone in an overlay's `SetupScenario`. It takes no account action and
opens no chat or network, so an empty `--portable-root` sandbox is enough: the
[session gate](test_session_gate.md#readings-and-refusals) passes as
`no-authorized-account`. Its arguments are synthetic `.example` URLs with a
path and a query, plus one synthetic file path, and every launch goes through
`Test::BlockLaunch`. It adds four synchronous stages, each named
`launch fuse self-test: <stage>`:

1. `private expectations and their exact launches`: a private URL and a
   private file path each PASS on their exact launch with no new failure, and
   `BlockedLaunches()` equals the expected record of real arguments.
2. `a mismatching launch under a live private expectation`: a third private
   expectation, which stays the leftover, is registered and a different URL
   is launched; the launch adds exactly one failure, the withheld FAIL.
3. `a public expectation and its launch, the control`: a public expectation
   and its launch PASS with no new failure while the leftover private
   expectation is still live; stage 4 reads their rows in the public form.
4. `the record, the leftover and the test-log scan`:
   `CheckBlockedLaunchesExactly` passes and notes the record,
   `CheckNoBlockedLaunches` writes its "already failed" Note because the
   mismatch was not consumed, and of these and
   `CheckNoBlockedLaunchExpectations` only the leftover FAILs. The stage then
   reads back, by byte offset, what the self-test appended to `test_log.txt`.
   It scans every row through `Test::DiscriminatingScan` for each private
   argument whole and for each private path and query alone, and checks that
   no row contains one. The public argument is the scan's in-run control: a
   scan that cannot see a printed argument fails
   `launch fuse self-test scan discriminates`. Last, twelve literal rows,
   transcribed from the templates rather than built by the fuse's formatters,
   must each appear exactly once: the three private Notes, the two private
   PASSes, the mismatch FAIL, the public Note and PASS, the record Note and
   PASS, the "already failed" Note and the leftover FAIL.

The checks print counts, row indices and needle names, never a needle.

The expected verdict is `SCENARIO_RESULT: FAIL (failures: 2)`, because there
is no expected-FAIL facility. The only FAIL rows are the deliberate mismatch
and leftover:

```text
TEST_RESULT: FAIL: blocked launch: Platform::File::UnsafeOpenUrl - withheld https://fuse-launched.example while private expectations are live: private:fuse-self-test-leftover https://fuse-expected.example
TEST_RESULT: FAIL: launch fuse self-test: a private expectation left over at the end - declared but never blocked: private:fuse-self-test-leftover https://fuse-expected.example
```

Every other verdict is PASS, including
`launch fuse self-test scan discriminates` and the stage checks that confirm
both deliberate FAILs through `FailureCount()` deltas.

There is no teardown. The leftover stays live and every argument stays
covered for the rest of the run, and the exact-row checks assume an empty fuse
at the start, so the self-test is never packed with a product scenario.

## Related guidance

[The secrecy scan](test_secrecy_scan.md); [open hand-off](test_open_handoff.md); [failure diagnosis](../diagnostics.md).
