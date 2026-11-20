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

`ApplyStartupOverrides()` also keeps a run's downloads, update checks and
URL-handler registration inside the run, each with its own escape passed with
`test-run --env` and one `NOTE:` row per process; none of them sets a saved
setting or option
([Downloads, the update checker and URL handlers](../launch.md#downloads-the-update-checker-and-url-handlers)):

- Unless `TDESKTOP_TEST_DOWNLOADS=product`, which reserves nothing, it
  creates the run's default download folder fresh under `EvidenceDir()`:
  `downloads/`, or a new `downloads-XXXXXX/` sibling when anything (a folder,
  a file, a junction) already has that name. Row
  `TDESKTOP_TEST_DOWNLOADS=[<value>] applied: default-folder=<run|product>
  source=<default|environment> - <reason>; stored path [<stored>] ask=<0|1>`,
  whose reason names the folder used. `DefaultDownloadFolder()` returns it,
  absolute with a trailing slash, and is empty without `-testagent`, under the
  opt-out and in a build without the harness; `File::DefaultDownloadPath()`
  returns it when it is not empty. When no fresh folder can be created it
  logs `TDESKTOP_TEST_DOWNLOADS=[<value>] setup refused: ...`, also on
  stderr, and `ApplyStartupOverrides()` returns false, so `Application::run()`
  quits before the rest of startup. A stored path, `Tmp()` or the
  ask-for-path prompt still decide first. The export settings' location label
  may still name the product's default folder while the export goes to the run
  folder; an export-folder campaign uses `TDESKTOP_TEST_DOWNLOADS=product`.
- `-noupdate`, which `test-run` passes, only skips installing an
  already-ready update at startup. Update checks are suppressed instead:
  `SuppressUpdateChecks()` is true for the whole `-testagent` process unless
  `TDESKTOP_TEST_UPDATER=allow`, and `Core::Updater::start()` returns at its
  early gate when it is, before any request, update-file probe or timer. The
  decision is selected once, on the first checker start (the crash window can
  come before `Application::run()`) or in `ApplyStartupOverrides()`, and is
  kept in memory only: nothing is saved. It is false without `-testagent` and
  in a build without the harness, reading nothing. Row
  `TDESKTOP_TEST_UPDATER=[<value>] applied: checker=off source=default -
  update checks suppressed for this process`, or with `allow`
  `checker=product source=environment - the product decides whether to
  check`.
- `AllowAutomaticUrlRegistration()` is false under `-testagent` unless
  `TDESKTOP_TEST_URL_SCHEME=register`, so `Application::autoRegisterUrlScheme()`
  does not queue the launch's automatic `tg`, `tonsite` and `ton`
  registration; it is true without `-testagent`, in a build without the
  harness and before `ApplyStartupOverrides()` selects it. The copy's own
  `skip-url-scheme-register` and the version check still apply first, the
  option is only read, and the `registertg` code still registers on request.
  Row `TDESKTOP_TEST_URL_SCHEME=[<value>] applied: registration=<off|product>
  source=<default|environment> - <reason>; due=<yes|no>
  skip-url-scheme-register=<on|off>`. A control that lets it run on Windows
  follows the
  [registry recipe](../launch.md#windows-registry-recipe-for-a-registration-control).

## Related guidance

[Launch and background behavior](../launch.md); [account fixtures](../account-fixtures.md).
[Release builds with the harness](../launch.md#release-builds-with-the-harness): when `Active()` can be true outside Debug.
