# test_agent

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_agent.h](../../test_agent.h).

## Use when

Runtime gate, startup overrides and fixture access. Read the contract below and the source header before use.

## Contract and usage

Runtime gate, startup scale override, the macOS App Nap hold
(`ApplyStartupOverrides()` begins one
`NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical`
`NSProcessInfo` activity early in `Application::run()`, before
`Test::Start()`, and holds it until the process exits, logging one `NOTE:
TDESKTOP_TEST_APP_NAP=[<value>] applied: hold=<active|off>
source=<default|environment> - <reason>` row; `TDESKTOP_TEST_APP_NAP=allow`
leaves it off and any other value is rejected; nothing off macOS), sticky
named events, scenario start, account fixture secrets (`TwoStepPassword()`
reads `2svpassword.txt`, `GramAccount()` reads `test_gram_account.txt`,
`RewriteGramAccountWords()` rewrites the live and golden copies at the rotated
wallet's address after a confirmed rotation, `StageGramAccountLiveWords()`
stages a proper prefix of the live copy's own words for a self-test and
`RestoreGramAccountLiveWords()` lengthens such a staged copy back to the words
it was cut from, neither touching the golden sibling; `GramAccountLivePath()`
/ `GramAccountGoldenPath()` name the two copies for metadata-only checks).

## Related guidance

[Launch and background behavior](../launch.md); [account fixtures](../account-fixtures.md).
