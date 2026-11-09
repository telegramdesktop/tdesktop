# test_widget_stress

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API (a permanent stage pack), with an internal module.
Source contract: [test_widget_stress.h](../../test_widget_stress.h).
Internal to it:
[test_widget_stress_internal.h](../../test_widget_stress_internal.h).

| File | Holds |
| --- | --- |
| `test_widget_stress.h` | The contract: `WidgetStressPass`, `WidgetStressArgs`, `AppendWidgetStress`, `RunWidgetStressSelfCheck`. |
| `test_widget_stress_internal.h` | The model the two translation units share: the manifest, the selection, a level's readings, an entry, the launch, and the functions the live stages call. |
| `test_widget_stress.cpp` | The kit (arguments, `manifest.json`, the selected HTML), each level's entry arithmetic (frames, renderer, host, widget, the in-window samples' latch), the envelope and its notes, the merge, the publication and the self-check. |
| `test_widget_stress_live.cpp` | `AppendWidgetStress`: the stages, the theme, the window's placement on the fastest screen (on the [fixture's one-column reading, move and settle](test_widget_fixture.md#one-column-window)), its gates, `placement` record and stay-on-top hint, the update filter, the idle check, the layout capture, the window observations, the covering-window check and its worker thread, the card capture, the one-alive check, the levels, the publication and the `Runner::onFinish` release. |

## Use when

A scenario measures the desktop client with widget-sandbox's
`packages/stress` kit and needs the run file of one pass that the package's
`tools/report.mjs` reads. `Test::AppendWidgetStress` appends the whole
linear runner for the loads it is given. Each selected manifest widget
becomes a local outgoing widget message with width anchor
`manifest.run.width` in the legacy `HistoryInner` of a Test chat, shown in
the product's own `HtmlWidget` view through the
[shared widget fixture](test_widget_fixture.md); its host is watched with
[test_widget_host](test_widget_host.md) (applied options, stats, load,
death, renderer work) and its validated ops are read with
[test_widget_ops](test_widget_ops.md) (`data-stress` values, batch counts,
scheme). It follows `CLIENTS.md` ("Host configuration", "The runner, step by
step") at the pinned tag.

Caller: the campaign of
`2026/10/30/run-the-widget-stress-kit-at-v0-4-0-on-a-release-build-without-lto`,
one launch per load and pass.

Use something else when:

- the conformance sets are measured: [test_widget_conformance](test_widget_conformance.md);
- one widget's host readings are enough: arm a watch through the fixture
  ([test_widget_host](test_widget_host.md));
- the report is wanted: `report.mjs` runs on the run files afterwards; the
  pack writes no report.

## Preconditions and limits

- **Session and chat.** Append it after the overlay's `waitEvent`,
  `waitForSessionReady` and `waitForChatsLoaded`, with `Test::UseTestSession`
  before them. `WidgetStressArgs::chat` is a chat of the Test account that
  meets the fixture's gates: loaded, its folder known, no unread messages.
- **One widget alive at a time.** Before each level the runner reads every
  loaded widget view of the chat: none may have a runtime that is waiting,
  constructing or live, none may lie in the list's visible area, and every
  earlier level's tap must be finished. Otherwise the level is refused and
  the ladder stops. Pick a chat whose bottom holds no widget message.
- **Release harness.** Measured passes run on a Release build configured
  with `TDESKTOP_RELEASE_TEST_HARNESS`
  ([Release builds with the harness](../launch.md#release-builds-with-the-harness)),
  launched with `--portable-root`. A Debug build runs the same stages for
  development; its production options carry the Debug CPU scale, so its
  files are not results.
- **One load per launch.** A launch may name several loads, but a ladder of
  nine levels already takes about three minutes; keep one load per launch
  under the 600 s watchdog and let the driver merge the launches.
- **Renderer timing overlay.** The renderer figures need the disposable
  `tgw_renderer.cpp` hunk of [test_widget_host](test_widget_host.md#the-overlay),
  kept in the campaign's overlay with the scenario and never committed.
  Without it every measured level is a runner error ("no renderer work was
  recorded"), never an all-null success.
- **Display.** The runner moves the main window wholly onto the screen
  with the highest `QScreen::refreshRate()` (ties: the primary screen),
  its frame placed 64 px inside the screen's available geometry and at most
  1000 logical px tall, out of a maximized or full-screen state if needed;
  the margin (the fixture's `kWidgetWindowMargin`) and the height are
  harness constants, not product styles. It never changes a display mode:
  `device.refreshHz` is the rate Qt reports for that screen; a screen
  slower than another is never chosen. Every screen's
  name, geometry and refresh rate go into the launch record.
- **One column.** The width is not a constant: it comes from the column
  styles at runtime, through the
  [fixture's one-column placement](test_widget_fixture.md#one-column-window),
  which the conformance runner shares.
  `Window::SessionController::computeColumnLayout()` lays the main window
  out in one column when its body
  (`bodyWidget()->width() - filtersWidth()`) is narrower than
  `st::columnMinimalWidthLeft + st::columnMinimalWidthMain`, so
  `MoveFixtureWindow()` asks for a `bodyWidget()` one logical px narrower
  than that, plus `st::windowFiltersWidth` when the chat filters sidebar is
  shown, plus the window's own margins beside its body, never below the
  window's minimum width, on the screen the runner chose. A first move puts
  the window on that screen; once it has held its geometry for 1 s
  (`FixtureWindowSettled()`, at most `kWidgetPlaceBound`, 6 s, each stage
  that plus `kDefaultStageTimeout`) a second one recomputes the target from
  the settled window, whose frame margins and scale can differ on the new
  screen. A gate then requires `OnlyFixtureChatShown()`:
  `adaptive().isOneColumn()`, a body below the threshold, no
  `Dialogs::Widget` shown in `MainWidget` and the chat's legacy
  `HistoryWidget` shown as wide as `MainWidget`: only the chat, no chat
  list and no third column. The card's 390 CSS px width check of every
  level proves the bubble is not squeezed. The runner keeps its own policy
  around those shared steps: the fastest screen, the stay-on-top hint set
  before the first move, leaving a maximized or full-screen state (not
  given back at the release), its three placement gates, the `placement`
  record of each move's readings from before it, and the focus and cursor
  clearing after the gates.
- **On top.** Before the move the runner sets `Qt::WindowStaysOnTopHint`
  on the window's `QWindow` with `QWindow::setFlag`
  ([window exposure](test_window_exposure.md)'s mechanism and bans: never
  `QWidget::setWindowFlags`, `raise()`, `activateWindow()` or
  `requestActivate()`). On Windows Qt applies it as a topmost
  `SetWindowPos` without activation, with a frame change the placement's
  settle covers. A gate requires the hint and, on Windows, `WS_EX_TOPMOST`.
  Only a hint the runner set is cleared again, in the close stage and in
  `Runner::onFinish`. The per-level covering-window check is unchanged.
- **Surface.** The window's `QWindow` surface type is recorded, not
  required: on Windows the main window reads `Direct3DSurface` once an RHI
  child exists, and Qt Widgets then flushes the raster backing store
  through QRhi into a swapchain.
- **Nothing else animates.** After the placement, the theme and the
  chat's opening, with no widget injected, the runner settles the list
  (focus cleared, cursor off the window, the scroll held 3350 ms, at most
  8 s) and counts the window's `QEvent::UpdateRequest` events over 2000 ms
  with the levels' update filter. Any update, or a scroll that never held,
  fails the gate and refuses the launch before it measures; the count,
  the stamps' span and their first offsets go into the launch record, and
  the stamps are cleared before the first level. Then one screen capture of
  the window's frame (`QScreen::grabWindow(0, ...)`, screen-relative, so a
  window over it would show) is saved as `stress-layout-<loads>.png`
  through `SaveImage` (its `SCREENSHOT` row) with its size noted: evidence,
  not a verdict, taken even when the idle check failed.
- **Theme.** The day palette comes from [test_palette](test_palette.md). When
  the day theme it leaves kept is not the default one (a theme file, a cloud
  theme or another embedded theme), the runner keeps the default through the
  product's Settings route (`Window::Theme::ApplyDefaultWithPath()` with no
  path, then `KeepApplied()`), and after the ladder keeps the recorded theme
  again the way the product keeps it (`ApplyDefaultWithPath()` for an embedded
  one, `Apply()` for a file or a cloud theme's cached file) before the palette
  restore. A theme preview alone is not used: `MainWindow::themeUpdated()`
  covers the window with its keep-or-revert warning and reverts the preview
  16 s later. A custom wallpaper the default theme replaced is not restored.
- **Durations.** `manifest.run`'s warm-up and window. `warmupMs` and
  `measureMs` replace them only for a smoke, which the file's notes and its
  requested configuration mark, so it never merges with a real pass.
- **Not measured.** `frames.dropped`, `frames.longTaskMs`,
  `frames.mainThreadMsPerSec`, `thermal`, `host.processMemoryMb` and
  `device.lowPowerMode` are always `null`. The covering-window check is
  Windows only; elsewhere its reading says not applicable and the window's
  worker samples the console lock alone.

## Minimal use

In the disposable overlay's `SetupScenario`; the overlay reads its launch
from the environment (`TDESKTOP_TEST_STRESS_CHAT`, `_PASS`, `_LOADS`,
`_OUT`, `_WARMUP_MS`, `_MEASURE_MS`, `_NOTES`, `_SELF_CHECK`), so one saved
overlay serves every launch of a campaign:

```cpp
void SetupScenario(not_null<Runner*> runner) {
	const auto session = Test::UseTestSession(runner);
	runner->waitEvent(u"launch_finished"_q);
	runner->waitForSessionReady();
	runner->waitForChatsLoaded();
	const auto env = [](const char *name) {
		return qEnvironmentVariable(name);
	};
	const auto ms = [&](const char *name) -> std::optional<crl::time> {
		const auto text = env(name);
		return text.isEmpty()
			? std::nullopt
			: std::make_optional(crl::time(text.toLongLong()));
	};
	if (env("TDESKTOP_TEST_STRESS_SELF_CHECK") == u"1"_q) {
		runner->add({
			.name = u"widget stress self-check"_q,
			.run = [] {
				for (const auto &row : Test::RunWidgetStressSelfCheck()) {
					Test::Check(row.passed, row.name, row.details);
				}
			},
		});
	}
	Test::AppendWidgetStress(runner, {
		.package = u"<absolute packages/stress directory>"_q,
		.chat = <the Test chat's PeerId, parsed from _CHAT>,
		.output = env("TDESKTOP_TEST_STRESS_OUT"),
		.pass = (env("TDESKTOP_TEST_STRESS_PASS") == u"production"_q)
			? Test::WidgetStressPass::Production
			: Test::WidgetStressPass::Measure,
		.loads = env("TDESKTOP_TEST_STRESS_LOADS").split(
			QChar(','),
			Qt::SkipEmptyParts),
		.framework = u"<the widget-sandbox tag>"_q,
		.engine = u"<the twidget pin, by its subject>"_q,
		.buildNote = u"<the build's configuration>"_q,
		.notes = env("TDESKTOP_TEST_STRESS_NOTES").split(
			QChar('<separator>'),
			Qt::SkipEmptyParts),
		.warmupMs = ms("TDESKTOP_TEST_STRESS_WARMUP_MS"),
		.measureMs = ms("TDESKTOP_TEST_STRESS_MEASURE_MS"),
	});
	Test::AppendTestSessionRestore(runner, session);
}
```

Keep the chat id in the private launch environment. The pack Checks its own
outcome; read its `STRESS_LAUNCH` row, then validate the run file and run
`node <package>/tools/report.mjs --out <dir> <measure> <production>` on the
final files.

## Stages

Every stage name, Check and Note starts `widget stress: `, after
`palette switch: ` or `palette restore: ` in the palette helpers' stages and
`show in chat list: ` in the show helper's (`widget stress <id> load` and
`widget stress <id> card`). A level's own stages name it `[<id>]`. In order:

- `report the package, the loads and the output` (the package, the selection
  and the output are read while the stages are appended; a refusal ends the
  pack here);
- the fixture's `capture the baseline and show the window normally`, `keep
  the window exposed`, `open the chat in the legacy HistoryWidget`;
- `palette switch: widget stress: the day palette`, `keep the default day
  theme`, `place the window on the fastest screen, on top`, `size the
  window for the one-column layout` (its gates; it also clears the
  window's text focus, moves the cursor off the window when it is over it,
  sends the list a leave and installs the update filter), `check the console
  and the window's exposure`, `settle the list before the idle check`,
  `check that nothing else updates the window while idle`, `capture the
  window's layout from the screen` (an `addRecovery` stage, run once the
  idle check ran);
- per level, in manifest order:
  - `[<id>] check that no other widget is alive`;
  - `[<id>] inject the watched widget` (the pass's override for measure, an
    empty one for production; the level's work log reserved in the same run,
    before its host can attach), then the layout check;
  - `show in chat list: widget stress <id> load` (the whole card, ready once
    it has a runtime), `[<id>] wait for loaded or dead` (60 s from the
    injection; dead is the level's death, [below](#deaths));
  - for a loaded widget that has not died: `show in chat list: widget stress
    <id> card` (the whole card, `WidgetCardReady()` or the level's death, so
    a widget that stops during the show ends it at once instead of waiting
    out the bound; a card in view and painted whose live runtime cut no
    frame for it within the bound is measured anyway,
    `desktop.setup.cardFrameLate` true and a Note, because a renderer too
    slow to show a level is that level's reading), `[<id>]
    settle the list and its floating date` (focus cleared and cursor off
    again, then the list's scroll unchanged for 3350 ms, at most 8 s),
    `[<id>] warm up`, `[<id>] measure` (its end, for the launch's first
    level that loaded and showed its card, also takes the card capture);
  - `[<id>] write the entry`, `[<id>] remove the widget and wait for its
    host` (both run for a level that failed, so it is still written and
    destroyed);
  - `[<id>] pause before the next level` (1 s), or after a load's last level
    `cool down after the last level of <load>` (20 s, run even after a
    stopped ladder once one of its levels was injected);
- `keep the day theme the launch found again` (only when the runner changed
  it), `palette restore: widget stress: the day palette`, `judge the palette
  restore`, `release the window's update filter and stay-on-top hint`;
- the fixture's `teardown` and `settle after the teardown`;
- `publish the launch`.

The fixture's opening stages, the setup and every level are composed in
one `Runner::addGated` group whose gate is the fixture's gate, a failed
setup gate, then the reason the ladder stopped; the layout capture, write,
removal and cool-down stages are `addRecovery` stages with their own
reasons, and the stages after the ladders are outside the group.

## Readings and refusals

### The measuring window

The warm-up and the window are `manifest.run`'s, counted from the stages'
runs; the window's actual length is the two snapshots' distance
(`desktop.windowMs`). Its start reads, in order: the console lock, the
covering windows and the window against the setup's reading, the applied
options and the card's CSS width; then it starts the window's worker
thread, starts the update stamps, reads the tap and the host and stamps
the start. Its end stops and joins the worker, reads the tap and the host,
stamps the end, stops the stamps and runs the same window checks.
Between them the main thread only observes: every 500 ms on its ticks it
reads the window's screen, frame, device pixel ratio, visibility and
exposure and the card's global bounds and visibility, and on every tick,
until it sees one, the [desktop stop](#deaths) reading, with no grab,
layout, file write or other probe. The `data-stress` values are the tap's, each
stamped when the host handed its batch to the tap, read at the entry's
writing, after the end; no timer rereads them.

The worker repeats the ends' console-lock reading and covering-window walk
every 500 ms from its start, off the GUI thread: on the measured window's
native handle and the card's native rectangle that the start's walk read,
with `WTSQuerySessionInformationW` and the z-order calls of the walk
(`GetTopWindow`, `GetWindow`, `IsWindowVisible`, `IsIconic`,
`DwmGetWindowAttribute`, `GetWindowLongPtrW`, `GetLayeredWindowAttributes`,
`GetWindowThreadProcessId`, `GetClassNameW`), never a Qt object and no
window message, so a busy main thread cannot stall it. It latches its first
failure; the main thread reads its record only after the join.
`desktop.window.coverAndLock` holds `samples`, `sampleEveryMs`, `zOrder`
(whether the samples walked the z-order or, without the start's native
rectangle, read only the lock), `costMs` and `maxCostMs` (the samples' own
time on the worker, summed and the longest), `joinMs` (the main thread's
stop and join, inside the window just before the end snapshot) and
`failure` (`sample`, `offsetMs` from the start snapshot, `details`, or
`null`).

A lock, a covering window or a failed covering walk at an end or in a
worker sample, and any sampled change, is a runner error of the level; so
is a window of at least two sampling periods that no worker sample saw. A
level with a runner error stops the ladder. Samples do not prove the
instants between them.

After the end of the launch's first level that loaded and showed its card
(not `cardFrameLate`, not dead at the end), outside every timed window and
before the card's removal, the runner captures that card's global
rectangle from the screen (`QScreen::grabWindow(0, ...)`, screen-relative,
so a window over it would show), saves it as `stress-card-<loads>.png`
through `SaveImage` (its `SCREENSHOT` row), notes its pixel size and
Checks, next to it, that the save succeeded, the card's core is
`manifest.run.width` (390) CSS px wide and the renderer's scheme is
`light`. A failed Check refuses the launch.

### Frames and the null `dropped`

`frames` are the intervals between consecutive `QEvent::UpdateRequest`
events delivered to the exact top-level `QWidget` that shows the card, inside
the window, stamped by a harness event filter that handles nothing and never
repaints (`desktop.frameSource` `window-update-request`). It is an
observable window-update proxy, not a DWM presentation proven display-paced:
the window updates on demand, so a static widget's sparse updates are not
dropped frames, and `dropped` stays `null`. `report.mjs` treats a null
`dropped` as zero for `fullRateUpTo`, so its client-frame full-rate column is
an update-cadence observation, not a display limit. The filter keeps 16384
stamps a window; more is a runner error.

The same filter first reads the window idle: before the first level, with
no widget injected and the list settled, a launch whose window gets more
than one `UpdateRequest` in 2000 ms is refused ("Nothing else animates"
above), so nothing on screen was animating when the launch began; one
isolated update, a single repaint such as a badge or status refresh, is
recorded with its offset and allowed, since an animation repaints the
window repeatedly. It
does not prove the unsampled instants of later levels; their own
observations are below.

### Run file

| Field | Source |
| --- | --- |
| `kit` | `manifest.json`'s `version`; `commit` is `framework`. |
| `platform`, `client`, `renderer`, `rendererKind` | `desktop`, `Telegram Desktop`, `twidget-core`, `own`. |
| `clientBuild` | `AppVersionStr`, `Debug` or `Release`, the build's CPU architecture, then `buildNote`. |
| `framework`, `engine` | The arguments, as given. |
| `device.model`, `device.os`, `device.ramMb` | The registry's `ProcessorNameString`, `QSysInfo`'s product name with its kernel, `GetPhysicallyInstalledSystemMemory`. |
| `device.refreshHz`, `device.dpr` | The chosen screen's `refreshRate()`, as Qt reports it for the display mode it found, and the placed window's `devicePixelRatio()`, read after the placement settled. |
| `device.window` | An extra key (the schema allows them): `width` and `height`, the placed top-level `QWidget`'s logical size; `layout`, the session controller's column layout (`one-column` for an accepted launch); `filtersSidebar`, whether the chat filters sidebar takes its width. `null` when never read. Part of `device`, so the merge refuses a launch whose window differs from the file's. |
| `pass` | `measure` or `production`. |
| `config.requested` | Measure: `compat` and `CLIENTS.md`'s table (`cpuLimitMs` 500, `cpuQuotaMs` 10000, `cpuQuotaWindowMs` 1000, `mainThreadQuotaMs` 10000, `maxTimers` 256, `memoryLimitBytes` 67108864, `features` `["webAnimations"]`); production: `strict`. Both: `width`, `theme` `light`, `warmupMs`, `measureMs`. |
| `config.applied` | The first level's host options exactly as they entered its `Job` (`policy`, the limits, `cpuKillAfterMs`, `features`, `devicePixelRatio`: the product's core ratio, not the window's), the first measured card's core CSS width and the scheme its renderer got. Every later level must match all three. |
| `date`, `notes` | The first launch's start (UTC); the runner's definitions, the smoke marker, the live runner's observation note, then `notes`. |
| `desktop` | `frameSource`; `screen` (name, geometry, refresh rate, surface) and `scales` (`windowDpr`, `coreRatio` = `style::DevicePixelRatio() * style::Scale() / 100`, `uiScale`); `launches`, one record per accepted launch. |

A launch record has its id, date, loads, entries, durations, screen,
scales and `window` (as in `device`), and the live runner's readings:
`themeBefore` and `theme` (embedded theme, cloud, theme file, night,
wallpaper kind, accent colorizer, `windowBg`), `themeChanged`,
`rendererTheme` (the first measured renderer's `colorScheme` and `params`),
`placement` (every screen with its name, geometry, available geometry,
refresh rate, scale and whether it is the primary; the chosen one; the
window before; the first move's `target` and the settled window's
`corrected` one, each with its frame, client geometry, body width, layout
reading (the `MoveFixtureWindow()` reading from before that move) and
whether it moved the window; the window after), `layout` (the
column layout, body, threshold, `bodyWidget()`, filters width, MainWidget
and chat widths, shown chat and chat lists, minimum width), `stayOnTop`
(the hint before, whether the runner applied it, the hint and Windows'
`WS_EX_TOPMOST` after) and `stayOnTopRelease` (the same after the release),
`focusAndCursor`, `environment` (lock and exposure), `idle` (`updates`,
`spanMs`, the first 20 `offsetsMs`, `windowMs`, and the settle's
`dateWaitMs`, `dateHeld`, `scrollChanges` and `focusAndCursor`),
`layoutScreenshot` (file name, whether it was saved, the frame and the
image's pixel size), `cardScreenshot` (file name, whether it was saved,
the level, the card's global rectangle, the image's pixel size, the card's
CSS width and the renderer's scheme), `updateFilter` (UpdateRequests
delivered since install) and `stopped` when the ladder stopped.

### Run entry

| Field | Source and definition |
| --- | --- |
| `id`, `load`, `value` | The manifest entry. |
| `loaded`, `bootMs` | The host's `loaded` and its own `loadedMs`, from the host's creation. |
| `dead`, `diedAtMs` | The level's death ([below](#deaths)): the host's first dead reason verbatim, and steady-clock ms from the host's creation to the change that carried it; for a stop the desktop decided itself, `RendererCore`'s stop reason and an estimate of the host clock at the runner's first reading of it. |
| `frames` | `count` intervals, nearest-rank `p50`/`p95` and `max` of the window's update intervals (above); fewer than two updates give count 0 and nulls. |
| `renderer` | Complete groups of the measured core's work inside the window: each timed `frame()` attempt's own time plus the message and tick work since the attempt before it; the first group is partial and discarded, trailing work is kept apart. An attempt may end before the QPainter replay when the list has not laid out the card's size; it still closes its group, and `desktop.rendererWork` counts the attempts that painted an image apart. `msPerFrame` the mean, `p95Ms` the nearest rank, `displayListOps` the last sampled attempt's list size; no sample gives nulls. |
| `host` | `SandboxStats` deltas of `jsMs`, `queries`, `queryMs` over the snapshots' actual seconds; `throttledTasks` the delta; `opsPerSec` the newest host's validated ops in the window; `maxBatchOps` its largest batch since creation; `nodes` and `memoryBytes` at the end. A counter going backwards is a named error and null. |
| `widget` | The last `data-stress` value at or before the start (else the first after it) and the last at or before the end: `fps`, `frames` and `jank` differences, `maxGap` the largest published inside the window, `last` the end value. |
| `errors`, `diags` | The host's first 20 error logs verbatim, then the runner's errors, each `runner: `; diag kind to count. |
| `thermal` | `null`. |

A level that never loaded, or died before its window, has frames count 0 and
null window metrics, with its boot, death and final host readings; a widget
that dies during its window keeps the whole window. Fixture, timeout and
instrument failures are runner errors, never deaths.

#### Deaths

A level's death is, in order: the host's own `dead` other than
`terminated`; a stop the desktop decided itself; then the host's own value,
`terminated` included. A paint bound's refusal of the widget's frames stops
its `RendererCore` and retires the runtime without the host, so the host
watch never carries that stop ([test_widget_host](test_widget_host.md)).
The runner reads it with the fixture's `WidgetDesktopStop()` for the
level's watched core ([test_widget_fixture](test_widget_fixture.md)) and
latches its first answer with that reading's `crl::profile()`. It reads on
every 50 ms tick of the load wait, the warm-up and the window, in each of
the card show's readiness checks, at the skip of each window stage, at a
refused card show's excuse, and before the window's end snapshot and the
level's final one; once latched it reads nothing more.

The same death decides the load wait, the window skips, the card show's
end (a stopped card is never cut a frame, so it is not `cardFrameLate`)
and the refused show's excuse. `ApplyDesktopStop()` merges it into the
level's end snapshot (the window's end, or the final reading of a level
without one): `dead` is `RendererCore`'s reason, `deadAt` the latching
reading, and `diedAtMs` `loadedMs + (that reading - loadedAt) / 1000` in
floating point, the host clock at the first reading that saw the stop,
null without both loading anchors (`loadedMs`, and `loadedAt`, the merge
that saw `loaded`). That is an estimate, not a measurement of the stop:
the first reading comes after the stop by the readings' lag (main-thread
painting, the posted retirement, the gaps between stages), and the merge
that saw `loaded` after the load by the host callback's. The run file's
notes say so. A window's
end takes only a stop seen before its stamp: one first seen later is never
dated into it, as a host death after the end is not. A host's own reason
is never replaced, and without a desktop stop a `terminated` stays.

`desktop` of an entry: `measured`, `windowMs`, `updates`, `rendererWork`
(samples, frames, `replayedFrames` and `unreplayedFrames` (the frame()
attempts inside the window that replayed the display list into an image,
and those that returned before it), ticks, messages, foreign,
`partialGroupMs`, `trailingWorkMs`, `trailingWorkEvents`), `statsAgeMs`
(`start`, `end`), `errorLogs`, `values` (`received`, `inWindow`, `late`,
`lastText`), `runnerErrors`; and the live runner's: `oneAlive` (what was
checked, widget views, alive runtime states, visible count, earlier
unfinished levels: counts and states only, no message of the chat), `setup`
(`layoutMs`, `loadDecidedMs`, both shows, `cardFrameLate`, `dateWaitMs`,
`dateHeld`, `scrollChanges`, `setupMs` from the injection to the warm-up,
the actual `warmupMs`, `focusAndCursor`), `window` (the start and end checks
with their lock, window and covering readings, `samples`, `sampleEveryMs`,
`sampleCostMs`, `changed`, and `coverAndLock`, the worker's samples above,
`null` for a level without a window), `hosts` (`optionsCalls`, `attached`,
`generation`), `cardCssWidth`, `scheme`, `updatesDropped`.

### Rows and Checks

- `STRESS_ENVIRONMENT`: the fixture's environment row.
- `STRESS_ENTRY: {...}` per written level: id, loaded, dead, measured,
  frames, renderer, the widget's fps.
- `STRESS_LAUNCH: {...}` once: pass, loads, selected levels, entries,
  `merged`, the written file, its size and the refusal.
- Checks: the package; each `fixture gate:` (the default day theme, `the
  window is wholly on the screen with the highest refresh rate`, `only the chat is shown, in the
  one-column layout`, `the window stays on top`, the console and exposure,
  `nothing else updates the window while idle`); per level `[<id>] one
  options call, one host and one tap generation`, `[<id>] the entry has no
  runner error`, `[<id>] the widget's host is gone before another
  injection`; `the card screenshot is of a 390 CSS px card in the light
  theme` once; the theme kept again, the palette back, the fixture's
  teardown and settle; `the launch is merged into the pass's run file`.
- `SCREENSHOT: "screenshots/stress-layout-<loads>.png"` once per launch
  whose idle check ran, before its `layout screenshot:` Note, and
  `SCREENSHOT: "screenshots/stress-card-<loads>.png"` once per launch with
  a shown card, before its `card screenshot:` Note.

### Merge and acceptance

A launch is accepted only with no failed fixture or setup gate, no stopped
ladder, no failed Check in the whole launch and exactly its selection's
entries, in manifest order, none with a runner error. An accepted launch
merges into `WidgetStressArgs::output` when every key of `kit`, `platform`,
`client`, `clientBuild`, `framework`, `renderer`, `rendererKind`, `engine`,
`device`, `pass` and `config` equals the file's (a refusal names the first
that differs): its loads' entries replace theirs, all stay in manifest order,
the file keeps its date, appends new notes and the launch record once. Any
other launch, or one the merge or the write refuses, writes
`<output>.launch-<loads>.json` with `launch.accepted` false and every
refusal, and never touches the run file. A pass is complete at the
manifest's 127 entries.

## Lifetime and cleanup

`Runner::onFinish` is registered first, before the palette helper's: on
every path it joins a window's worker that is still running (the window's
end and the entry's writing join it first on their paths, and the worker's
owner joins it when destroyed), removes the update filter, clears the
stay-on-top hint when
the runner set it (once; the close stage does it first on a normal path),
keeps the replaced day theme again (once), runs the fixture's idempotent
release (messages destroyed, taps and host watches disarmed, so their cores
leave the work registry; the option, the chat, the exposure hint and the
window state restored) and, when the publish stage never ran, writes the
launch file with "the launch ended before its publish stage". The palette
helper's own release follows it.

Lasting effects, all in the portable account the launch runs in: the window
keeps its placement on the chosen screen, its one-column width and its
normal state (Telegram saves its geometry); the palette switch persists
what `test_palette.h` describes; a changed day theme is kept again but a
replaced custom wallpaper is not; the cursor is moved. Each launch writes
its run file or launch file, its layout capture and its card capture.

## Verification

Executed in the app: the Release dry runs and both measured passes of
`2026/10/30/run-the-widget-stress-kit-at-v0-4-0-on-a-release-build-without-lto`
ran the whole runner on Windows (the placement on the fastest screen, the
one-column window on top, the idle check, the layout capture, the
endpoint and 500 ms main-thread window checks, the self-check with
`_SELF_CHECK`, 127 schema-valid entries per pass, each with one host).
Not yet in the app: the worker's in-window samples, the card capture and
the replay counts, compiled in Debug and Release (Win32, MSVC), their
arithmetic passing the 15 rows `RunWidgetStressSelfCheck()` had then,
outside it. The Release runs above ran the placement while its reading,
target and settle were still the runner's own; since
`2026/10/31/put-the-conformance-runners-window-in-the-one-column-layout`
they are the [fixture's](test_widget_fixture.md#one-column-window), and
the runner has not placed its window through them in the app yet: that
task's evidence campaign runs a Debug stress-kit smoke for the placement
gates and the `placement` record.

The self-check now has 16 rows. Its `desktop stop:` row merges literal
desktop stops into the control level's window end: a null and a
`terminated` dead replaced, the window kept (its 4 frame intervals),
`deadAt` the reading that saw the stop and `diedAtMs` 84.5 + 9750.25 =
9834.75 (an integer division of the microseconds would give 9834.5); a
host's own reason kept with its `diedAtMs` and `deadAt`; no stop keeping
a null and a `terminated` dead; and `diedAtMs` null without the load's
stamp or without `loadedMs`. The
[deaths](#deaths) reading, its merge and that row compiled in the Windows
Win32 Debug build of
`2026/10/31/stop-a-widget-on-its-last-picture-when-the-paint-bounds-refuse-its-frames`;
the self-check runs only from an overlay (`_SELF_CHECK` above), so that
task's evidence loop runs it and the live reading, neither of which has
run yet.

## Related guidance

[Shared widget fixture](test_widget_fixture.md);
[widget host watch](test_widget_host.md);
[validated-ops tap](test_widget_ops.md);
[palette switch](test_palette.md);
[chat-list show](test_chat_show.md);
[console lock](test_console_lock.md);
[window exposure](test_window_exposure.md);
[Release builds with the harness](../launch.md#release-builds-with-the-harness);
[scenario teardown](../teardown.md).
