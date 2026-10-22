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
