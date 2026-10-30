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
| `test_widget_stress.cpp` | The kit (arguments, `manifest.json`, the selected HTML), each level's entry arithmetic (frames, renderer, host, widget), the envelope and its notes, the merge, the publication and the self-check. |
| `test_widget_stress_live.cpp` | `AppendWidgetStress`: the stages, the theme, the window's placement, the update filter, the window observations and covering-window check, the one-alive check, the levels, the publication and the `Runner::onFinish` release. |

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
- **Display.** The runner moves the primary window wholly onto the screen
  with the highest `QScreen::refreshRate()` (ties: the primary), at most
  1280x1000 logical px placed 64 px inside its available geometry, out of a
  maximized or full-screen state if needed. These are harness constants, not
  product styles. It never changes a display mode. The window's `QWindow`
  surface type is recorded, not required: on Windows the main window reads
  `Direct3DSurface` once an RHI child exists, and Qt Widgets then flushes the
  raster backing store through QRhi into a swapchain.
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
  Windows only; elsewhere its reading says not applicable.

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
  theme`, `place the window on the fastest screen` (it also clears the
  window's text focus, moves the cursor off the window when it is over it,
  sends the list a leave and installs the update filter), `check the console
  and the window's exposure`;
- per level, in manifest order:
  - `[<id>] check that no other widget is alive`;
  - `[<id>] inject the watched widget` (the pass's override for measure, an
    empty one for production; the level's work log reserved in the same run,
    before its host can attach), then the layout check;
  - `show in chat list: widget stress <id> load` (the whole card, ready once
    it has a runtime), `[<id>] wait for loaded or dead` (60 s from the
    injection);
  - for a loaded widget that has not died: `show in chat list: widget stress
    <id> card` (the whole card, `WidgetCardReady()`), `[<id>] settle the list
    and its floating date` (focus cleared and cursor off again, then the
    list's scroll unchanged for 3350 ms, at most 8 s), `[<id>] warm up`,
    `[<id>] measure`;
  - `[<id>] write the entry`, `[<id>] remove the widget and wait for its
    host` (both run for a level that failed, so it is still written and
    destroyed);
  - `[<id>] pause before the next level` (1 s), or after a load's last level
    `cool down after the last level of <load>` (20 s, run even after a
    stopped ladder once one of its levels was injected);
- `keep the day theme the launch found again` (only when the runner changed
  it), `palette restore: widget stress: the day palette`, `judge the palette
  restore`, `release the window's update filter`;
- the fixture's `teardown` and `settle after the teardown`;
- `publish the launch`.

The fixture's opening stages, the setup and every level are composed in
one `Runner::addGated` group whose gate is the fixture's gate, a failed
setup gate, then the reason the ladder stopped; the write, removal and
cool-down stages are `addRecovery` stages with their own reasons, and the
stages after the ladders are outside the group.

## Readings and refusals

### The measuring window

The warm-up and the window are `manifest.run`'s, counted from the stages'
runs; the window's actual length is the two snapshots' distance
(`desktop.windowMs`). Its start reads, in order: the console lock, the
covering windows and the window against the setup's reading, the applied
options and the card's CSS width; then it starts the update stamps, reads
the tap and the host and stamps the start. Its end reads the tap and the
host, stamps the end, stops the stamps and runs the same window checks.
Between them the runner only observes: every 500 ms on its ticks it reads
the window's screen, frame, device pixel ratio, visibility and exposure and
the card's global bounds and visibility, with no grab, layout, file write or
other probe. The `data-stress` values are the tap's, each stamped when the
host handed its batch to the tap, read at the entry's writing, after the
end; no timer rereads them.

A lock, a covering window, a failed covering walk or any sampled change is
a runner error of the level, and a level with a runner error stops the
ladder. Samples do not prove the instants between them.

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

### Run file

| Field | Source |
| --- | --- |
| `kit` | `manifest.json`'s `version`; `commit` is `framework`. |
| `platform`, `client`, `renderer`, `rendererKind` | `desktop`, `Telegram Desktop`, `twidget-core`, `own`. |
| `clientBuild` | `AppVersionStr`, `Debug` or `Release`, the build's CPU architecture, then `buildNote`. |
| `framework`, `engine` | The arguments, as given. |
| `device.model`, `device.os`, `device.ramMb` | The registry's `ProcessorNameString`, `QSysInfo`'s product name with its kernel, `GetPhysicallyInstalledSystemMemory`. |
| `device.refreshHz`, `device.dpr` | The placed window's screen's `refreshRate()` and the window's `devicePixelRatio()`, read after the placement settled. |
| `pass` | `measure` or `production`. |
| `config.requested` | Measure: `compat` and `CLIENTS.md`'s table (`cpuLimitMs` 500, `cpuQuotaMs` 10000, `cpuQuotaWindowMs` 1000, `mainThreadQuotaMs` 10000, `maxTimers` 256, `memoryLimitBytes` 67108864, `features` `["webAnimations"]`); production: `strict`. Both: `width`, `theme` `light`, `warmupMs`, `measureMs`. |
| `config.applied` | The first level's host options exactly as they entered its `Job` (`policy`, the limits, `cpuKillAfterMs`, `features`, `devicePixelRatio`: the product's core ratio, not the window's), the first measured card's core CSS width and the scheme its renderer got. Every later level must match all three. |
| `date`, `notes` | The first launch's start (UTC); the runner's definitions, the smoke marker, the live runner's observation note, then `notes`. |
| `desktop` | `frameSource`; `screen` (name, geometry, refresh rate, surface) and `scales` (`windowDpr`, `coreRatio` = `style::DevicePixelRatio() * style::Scale() / 100`, `uiScale`); `launches`, one record per accepted launch. |

A launch record has its id, date, loads, entries, durations, screen and
scales, and the live runner's readings: `themeBefore` and `theme` (embedded
theme, cloud, theme file, night, wallpaper kind, accent colorizer,
`windowBg`), `themeChanged`, `rendererTheme` (the first measured renderer's
`colorScheme` and `params`), `placement` (every screen, the chosen one, the
window before and after, the target), `focusAndCursor`, `environment` (lock
and exposure), `updateFilter` (UpdateRequests delivered since install) and
`stopped` when the ladder stopped.

### Run entry

| Field | Source and definition |
| --- | --- |
| `id`, `load`, `value` | The manifest entry. |
| `loaded`, `bootMs` | The host's `loaded` and its own `loadedMs`, from the host's creation. |
| `dead`, `diedAtMs` | The first dead reason verbatim, and steady-clock ms from the host's creation to the change that carried it. |
| `frames` | `count` intervals, nearest-rank `p50`/`p95` and `max` of the window's update intervals (above); fewer than two updates give count 0 and nulls. |
| `renderer` | Complete groups of the measured core's work inside the window: each rebuilt frame's own time plus the message and tick work since the rebuilt frame before it; the first group is partial and discarded, trailing work is kept apart. `msPerFrame` the mean, `p95Ms` the nearest rank, `displayListOps` the last sampled frame's list size; no sample gives nulls. |
| `host` | `SandboxStats` deltas of `jsMs`, `queries`, `queryMs` over the snapshots' actual seconds; `throttledTasks` the delta; `opsPerSec` the newest host's validated ops in the window; `maxBatchOps` its largest batch since creation; `nodes` and `memoryBytes` at the end. A counter going backwards is a named error and null. |
| `widget` | The last `data-stress` value at or before the start (else the first after it) and the last at or before the end: `fps`, `frames` and `jank` differences, `maxGap` the largest published inside the window, `last` the end value. |
| `errors`, `diags` | The host's first 20 error logs verbatim, then the runner's errors, each `runner: `; diag kind to count. |
| `thermal` | `null`. |

A level that never loaded, or died before its window, has frames count 0 and
null window metrics, with its boot, death and final host readings; a widget
that dies during its window keeps the whole window. Fixture, timeout and
instrument failures are runner errors, never deaths.

`desktop` of an entry: `measured`, `windowMs`, `updates`, `rendererWork`
(samples, frames, ticks, messages, foreign, `partialGroupMs`,
`trailingWorkMs`, `trailingWorkEvents`), `statsAgeMs` (`start`, `end`),
`errorLogs`, `values` (`received`, `inWindow`, `late`, `lastText`),
`runnerErrors`; and the live runner's: `oneAlive` (what was checked, widget
views, alive runtime states, visible count, earlier unfinished levels: counts
and states only, no message of the chat), `setup` (`layoutMs`,
`loadDecidedMs`, both shows, `dateWaitMs`, `dateHeld`, `scrollChanges`,
`setupMs` from the injection to the warm-up, the actual `warmupMs`,
`focusAndCursor`), `window` (the start and end checks with their lock, window
and covering readings, `samples`, `sampleEveryMs`, `sampleCostMs`,
`changed`), `hosts` (`optionsCalls`, `attached`, `generation`),
`cardCssWidth`, `scheme`, `updatesDropped`.

### Rows and Checks

- `STRESS_ENVIRONMENT`: the fixture's environment row.
- `STRESS_ENTRY: {...}` per written level: id, loaded, dead, measured,
  frames, renderer, the widget's fps.
- `STRESS_LAUNCH: {...}` once: pass, loads, selected levels, entries,
  `merged`, the written file, its size and the refusal.
- Checks: the package; each `fixture gate:` (theme, placement, raster
  surface, console and exposure); per level `[<id>] one options call, one
  host and one tap generation`, `[<id>] the entry has no runner error`,
  `[<id>] the widget's host is gone before another injection`; the theme
  kept again, the palette back, the fixture's teardown and settle; `the
  launch is merged into the pass's run file`.

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
every path it removes the update filter, keeps the replaced day theme again
(once), runs the fixture's idempotent release (messages destroyed, taps and
host watches disarmed, so their cores leave the work registry; the option,
the chat, the exposure hint and the window state restored) and, when the
publish stage never ran, writes the launch file with "the launch ended
before its publish stage". The palette helper's own release follows it.

Lasting effects, all in the portable account the launch runs in: the window
keeps its placement and normal state (Telegram saves its geometry); the
palette switch persists what `test_palette.h` describes; a changed day
theme is kept again but a replaced custom wallpaper is not; the cursor is
moved. Each launch writes its run file or launch file.

## Verification

Executed: `RunWidgetStressSelfCheck()`'s 14 rows (manifest selection,
frames, renderer groups and refusals, host rates, widget clipping and
refusals, counter reset, a never-loaded death, envelope, integers, ladder
merge and refusal, acceptance) passed in a standalone QtCore harness, and
three deliberate breakages failed six of them; Debug single-file compiles of
`test_widget_stress.cpp` and `test_widget_stress_live.cpp` (Windows, Win32,
MSVC). The live stages have not run in the app.

Proposed, by the campaign: a Release smoke of short loads (a small moving
load, a query or canvas load, a production cap-crossing load) with
`_SELF_CHECK`, smoke durations and the renderer overlay, whose files
validate against `run.schema.json`, whose entries show one host per level,
nonempty renderer work for a changing widget, unscaled applied options and
the placed screen's refresh rate, and whose card crops, taken outside timed
windows, show the whole light card on screen; and a deliberately covered or
moved window refused before any timed run.

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
