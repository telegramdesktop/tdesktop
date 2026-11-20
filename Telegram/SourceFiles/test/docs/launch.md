# Launch, activation and background painting

[Harness guide](../README.md) · [Helper index](helpers/README.md)

On macOS a default `workspace.py test-run` launch of an app bundle's main
executable does not activate the client: it launches the bundle through
LaunchServices in the background, with Qt's own launch activation switched off
(`.agents/shared/test-loop.md`, Launch activation; the report reads
`launch_method: "background"`). The client starts as an inactive application
and its windows open behind the frontmost application's. A window shown
already covered reads `isExposed()` true until the window server first reports
a change for it, so `exposed=1` is no proof that it is on screen; after such a
change a covered window reads unexposed and gets no paints. Drive focus-routed
actions with `ForceWindowActive`, keep a window a stage needs painted exposed
with `KeepWindowExposed` either way, and use `test-run --activate` when
genuine OS activation is the subject. Product flows that raise or activate a
top-level window - `Ui::PopupMenu`, `Ui::SeparatePanel::showAndActivate()`,
`Window::Controller::activate()`, `DropText` into a shown field - still
request activation of the client on macOS, and so do the not-marking-read,
separate-panel-walk and popup-menu self-tests. After a background launch the
window server may grant or refuse that request: on this host it granted it
about 11 s after launch in two default runs and refused it about 20 s after
launch in a third, under a condition those runs did not establish
(`2026/10/08/launch-the-testagent-client-without-activating-it`, Runs 4, 8 and
9). A granted request takes the key window from the frontmost application.
After a refused one the client stays inactive and the frontmost application
keeps its key window, but the client's window may still be ordered in front of
the frontmost application's window by the request's own window ordering: in
Run 9 above the primary window stayed covered after its restore, while with an
overlay that made the client's own application-activation calls no-ops,
reproducing that refusal under another stage sequence, the inactive primary
window was in front for about 4 s after the not-marking-read self-test's
restore through `Window::Controller::activate()`, until a later request put it
behind again
(`2026/10/09/keep-background-testagent-runs-on-macos-unthrottled-and-painting`,
Runs 0 and 3). So the scenario's own activation is no reliable route to OS
activation under the default. A client the launch did not activate is a
background application, which macOS may nap and whose timers it may coalesce:
in background runs on this host without a hold, a sampler's heartbeat gaps
grew to 100-290 ms after about 15 s of normal cadence
(`2026/10/01/animate-gram-card-sending-and-settle-effects`, Run 1), and a 4 ms
precise sampler ticked every 50-1000 ms from about the 80th second
(`2026/10/02/show-the-input-method-composition-in-the-gram-send-amount`, Run
1). On a locked console macOS napped such a client without a hold, from about
30 s after process start
(`2026/10/09/keep-background-testagent-runs-on-macos-unthrottled-and-painting`,
Runs 0 and 4), but another application's window covering the client did not
guarantee it: on an unlocked console, a client without a hold that the
frontmost application's window covered kept an exact 16 ms timer cadence over
60 s and over 300 s (Runs 1 and 2). Those runs did not establish what made
macOS nap the client. So on macOS the harness holds one user-initiated,
latency-critical `NSProcessInfo` activity
(`NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical`;
idle system sleep stays allowed) for the whole `-testagent` run:
`Test::ApplyStartupOverrides()` (`test_agent.cpp`) begins it early in
`Application::run()`, before `Test::Start()`, and never ends it, so it lasts
until the process exits, a watchdog or stage-timeout ending included. The log
says once which case a run is in: a held run reads
`NOTE: TDESKTOP_TEST_APP_NAP=[] applied: hold=active ...`, and a run without
the hold reads `hold=off` with the reason. An overlay no longer holds an
activity of its own: the per-overlay `BeginLatencyActivity` of the
`2026/10/07/animate-the-sending-row-for-a-collectible-transfer` overlay is
history, and a second activity is harmless but redundant. A campaign whose
subject is background scheduling itself (App Nap, timer coalescing,
throttled-timer behaviour) launches with
`test-run --env TDESKTOP_TEST_APP_NAP=allow`, and its log then reads
`hold=off source=environment`; any other non-empty value is logged as
rejected and the hold is kept. Off macOS nothing reads
`TDESKTOP_TEST_APP_NAP` and nothing is held.

Read [input targeting](helpers/test_widgets.md#input-helpers) for the actual
event route, [window exposure](helpers/test_window_exposure.md) for painting
and [console state](helpers/test_console_lock.md) when diagnosing a refusal.

## Downloads, the update checker and URL handlers

`Test::ApplyStartupOverrides()` (`test_agent.cpp`) also keeps a `-testagent`
run's downloads, update checks and URL-handler registration inside the run.
It runs in `Application::run()` right after the settings are read, before the
launch's URL-scheme check, `startDomain()` and `Test::Start()`. Each override
has its own escape, passed with `test-run --env NAME=VALUE`, which changes
only that override; any other non-empty value is logged as rejected and the
default is kept. Each writes one row per process, with
`source=<default|environment>` and the reason:
`NOTE: TDESKTOP_TEST_DOWNLOADS=[<value>] applied: default-folder=...`,
`NOTE: TDESKTOP_TEST_UPDATER=[<value>] applied: checker=...` and
`NOTE: TDESKTOP_TEST_URL_SCHEME=[<value>] applied: registration=...`. None of
them sets a saved setting or option, so none of them reaches the copy's saved
download path, auto-update, last update check or `skip-url-scheme-register`.

Downloads. A `-testagent` run's default download folder is one the run creates
fresh under its evidence directory (`TDESKTOP_TEST_EVIDENCE_DIR`, which
`test-run` sets to `--run-dir`, or `test_evidence/` under the working folder
without it): `downloads/` when nothing exists under that name yet, otherwise a
new `downloads-XXXXXX/` beside it, kept after exit. Whatever a prepared run
directory already has at `downloads` (a folder, a file or a junction to
elsewhere) is never reused. The row names the folder actually used
(`default-folder=run ... - the run folder <path>`), and
`File::DefaultDownloadPath()` returns it through `Test::DefaultDownloadFolder()`.
When neither folder can be created the launch logs
`NOTE: TDESKTOP_TEST_DOWNLOADS=[<value>] setup refused: ...`, writes the same
line to stderr (`app_stderr.txt` in the run directory under `test-run`) and
quits before the accounts, the windows or the scenario start, instead of
falling back to the host's folder. Only the default folder changes and nothing
is saved: a stored download path, `Tmp()` (the session's temporary folder) or
the ask-for-path prompt still decide first, and the row prints them
(`stored path [<path>] ask=<0|1>`). A scenario that sets its own path gets it,
and the copy stores it as the product does. Downloaded fixture files land in
the run directory, so keep `--run-dir` in an ignored folder. The export
settings' location label (the account export and the single-chat export) may
still name the product's default Downloads folder while an export with an empty
saved path goes to the run folder. A campaign whose subject is the download
folder, the export folder or that label launches with
`TDESKTOP_TEST_DOWNLOADS=product` (`default-folder=product`), which reserves
nothing.

Update checker. `-noupdate`, which `test-run` always passes, only skips
installing an already-ready (downloaded) update at startup
(`Sandbox::socketError()`); it does not stop the checker. A `-testagent`
process suppresses update checks instead (`checker=off`): every
`Core::Updater::start()` returns before it computes a delay, probes for an
update file, starts the HTTP or MTP checker or arms its timer, whether a
window, a timer, a settings action or the crash-recovery window (which can
start the checker before `Application::run()`) calls it. The decision is made
once per process, at the first checker start or at `ApplyStartupOverrides()`,
whichever comes first, and lives in memory only: nothing is saved, so the
copy keeps its own auto-update and last check. `TDESKTOP_TEST_UPDATER=allow`
(`checker=product`) leaves the decision to the product, which checks as the
copy's auto-update, last check, build and external-updater state say; set it
from process start. The row does not print the stored auto-update or last
check, since the decision can come before the settings are read; an updater
campaign reads them in its overlay. `Update Info:` lines are in the working
folder's `DebugLogs/log_*.txt`, and a download goes into
`<working dir>/tupdates/`.

URL handlers. The product registers the `tg`, `tonsite` and `ton` handlers on
a copy an older build last wrote (`Local::oldSettingsVersion() < AppVersion`)
when the copy's `skip-url-scheme-register` option is off
(`Application::autoRegisterUrlScheme()`). A `-testagent` launch skips that
automatic registration (`registration=off`): `autoRegisterUrlScheme()` queues
it only when `Test::AllowAutomaticUrlRegistration()` is true as well. The
copy's option is only read, never set; the row prints the launch's own
`due=<yes|no> skip-url-scheme-register=<on|off>`.
`TDESKTOP_TEST_URL_SCHEME=register` (`registration=product`), or a launch
without `-testagent`, lets the product register when due and not skipped.
The `registertg` settings code registers on request either way. A
registration writes the host owner's real handlers: on Windows the
`HKCU\Software\Classes` scheme keys, the `tdesktop.*` ProgIDs,
`HKCU\Software\TelegramDesktop\Capabilities` and Telegram's
`RegisteredApplications` value. A campaign that lets it run follows the
recipe below and launches with `--portable-root`, so the written command
(with `-workdir`) differs from what this checkout's Debug build may already
have registered.

### Windows registry recipe for a registration control

Use it only on a host where every entry a registration writes already exists:
the restore writes baseline values back one by one, so it cannot remove a key
or value the control created. The campaign also needs exclusive use of the
values of step 1 from their first baseline reading through the final
comparison of step 5: at most one registration control per Windows user at a
time (a second one waits until the first has finished step 5 before it takes
its own baseline, so it never records the first's temporary registration as
the baseline), and no other registrar writing them meanwhile (another Telegram
build's `registertg` or automatic registration, a link-handler installer, the
owner). When that cannot be guaranteed for the account, run the control under
a disposable Windows user whose baseline has them all, or record the control
as unverified.

1. Before any launch that could register, in the executable's registry view
   (`reg query ... /reg:32` for the Win32 build, `/reg:64` for x64), verify
   that each of these is present and record its type and data:
   - `HKCU\Software\Classes\tg`, `\tonsite` and `\ton`: the default value and
     `URL Protocol`, the `DefaultIcon` default and the `shell\open\command`
     default;
   - `HKCU\Software\Classes\tdesktop.tg`, `\tdesktop.tonsite` and
     `\tdesktop.ton`: the `DefaultIcon` and `shell\open\command` defaults;
   - `HKCU\Software\TelegramDesktop\Capabilities`: `ApplicationName`,
     `ApplicationDescription` and the `UrlAssociations` values `tg`,
     `tonsite` and `ton`;
   - `HKCU\Software\RegisteredApplications`: the `Telegram Desktop` value.

   If any is missing, the baseline is partial: do not run a writing control on
   this host. Run it under a disposable Windows user whose baseline has them
   all, or record the control as unverified.
2. Export the baseline in the same view (`/reg:32` here and below for the
   Win32 build): `reg export <key> <file> /y /reg:32` for each of the six
   scheme and ProgID keys and for the Capabilities key, and keep the
   `reg query` reading (presence, type and data) of the `Telegram Desktop`
   value. Never export or reimport the whole `RegisteredApplications` key: it
   holds other applications' values.
3. Run the control inside cleanup (steps 4 and 5) that also runs on a
   failure, a timeout, an interruption or a partial write.
4. Stop the campaign's exact process, then read each value of step 1 again in
   the same view. The control writes each of them as `REG_SZ`
   (`RegisterLegacy()` and `RegisterDefaultProgram()` in
   `base_url_scheme_win.cpp`, with the data from
   `Core::Application::RegisterUrlScheme()`):
   `"<exe>" -workdir "<cWorkingDir()>" -- "%1"` for the commands,
   `"<exe>,1"` for the icons, `URL:Telegram Link`, `URL:TonSite Link` or
   `URL:Ton Link` and an empty `URL Protocol` for the scheme roots,
   `Telegram Desktop` for `ApplicationName` and `ApplicationDescription`,
   `tdesktop.<scheme>` for each association and
   `SOFTWARE\TelegramDesktop\Capabilities` for the `Telegram Desktop` value.
   A value whose type and data now equal that write and differ from step 1
   was changed by the control: write back that one value with its step-1
   type and data, `reg add <key> /ve /t <type> /d <data> /f /reg:32` for a
   default value and `/v <name>` instead of `/ve` for a named one, Telegram's
   `RegisteredApplications` value included (pass the data so that `reg`
   receives its quotes and `%1` unchanged). Leave a value that still reads as
   in step 1. A value that reads as neither was changed by another writer:
   leave it and report it. Never `reg import` an export to restore: it also
   puts back every other value under the key, such as another tool's verb
   under `tg\shell` that changed meanwhile.
5. Read the values of step 1 once more and compare them with step 1, then
   export the same keys again and compare them with the step-2 exports to
   confirm that no other value changed. Report a difference instead of
   importing over it. These checks reduce but cannot detect a write that
   races between a value's step-4 reading and its restore: the restore
   overwrites it and step 5 can still match step 1, which is why the campaign
   needs exclusive use of the values.

## Release builds with the harness

The harness is compiled in Debug builds. The CMake option
`TDESKTOP_RELEASE_TEST_HARNESS`, OFF by default, also compiles it into the
Release configuration, for local measurements of an optimized build: it
defines `TDESKTOP_RELEASE_TEST_HARNESS` for the whole Telegram target in
Release only, so Debug, MinSizeRel and RelWithDebInfo stay unchanged.
Configuring it ON together with `DESKTOP_APP_ENABLE_LTO`, the official release
builds' setting, fails; such a binary is never shipped. Switch it by
reconfiguring the existing tree, and back OFF when done:

```
C:\Telegram\ThirdParty\CMake\bin\cmake.exe -S <source> -B <source>\out -DTDESKTOP_RELEASE_TEST_HARNESS=ON
C:\Telegram\ThirdParty\CMake\bin\cmake.exe -S <source> -B <source>\out -DTDESKTOP_RELEASE_TEST_HARNESS=OFF
```

The definition is on every Release compile command, so the first Release
build after either switch rebuilds every Telegram object.

- Harness translation units are gated
  `#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS`. An overlay
  hunk in a product file must use the same condition to be compiled in such a
  build; one under `#ifdef _DEBUG` is left out.
- `test_wallet_ready.cpp` stays Debug-only: it calls the Debug-only
  `Wallet::Session::debugClearNetworkState()`.
- Where a harness header says "Debug build", "Debug-only" or "the Release
  branch", read a build with the harness and one without it.
- `out/Release` has no golden folder: launch with `workspace.py test-run
  --portable-root <dir>`, with `<dir>/test_TelegramForcePortable` copied from
  the Debug golden (`.agents/shared/test-loop.md`, wallet-custody limit
  included).
- `-testagent` turns on debug logging and the deadlock detector's ping thread
  in every configuration (`core/launcher.cpp`, `core/sandbox.cpp`), an
  observer cost inside any measurement.
