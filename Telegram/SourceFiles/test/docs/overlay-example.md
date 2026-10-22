# Overlay APIs and minimal scenario

[Harness guide](../README.md) · [Helper index](helpers/README.md)

- `test_runner.h` — the staged scenario engine: `Stage{name, run, until, then, timeout}`,
  `waitEvent`, `waitForSessionReady`, the normal bounded non-fatal `waitForChatsLoaded()`, and
  explicit strict `waitForChatsLoadedStrict()`; timing out an ordinary `Stage` ends the whole
  scenario, while the wall-clock watchdog (default 120s; `TDESKTOP_TEST_WATCHDOG` override in seconds, 1..600, otherwise the default; the armed duration is logged at scenario start)
  guarantees `TEST_COMPLETE` + quit on every exit path including timeout. `actOnWidget` waits for
  and lifetime-guards the exact target before acting once. `captureAndInspect` saves the accepted
  prepared frame and then runs numeric/raster assertions against that same widget and image.
- `test_log.h` — evidence dir from `TDESKTOP_TEST_EVIDENCE_DIR` (the workspace `test-run`
  helper sets it), flushed absolute-path logging, `Step/Pass/Fail/Check/Note`, `CheckNear`
  tolerance assertions, `LogGeometry`, the standard markers.
- `test_widgets.h` — `FindAll<T>`/`FindFirst<T>`/`FindVisible<T>` (the `dynamic_cast`-based
  finders that avoid the guaranteed `findChildren<CustomWidget*>` crash), `Click`, `TypeText`,
  `CommitText`, `PressKey`, `Drag`, and `Wheel` via real Qt events, plus generation-counted,
  lifetime-guarded `PublishLiveWidget` / `PublishLiveAction` seams for exact layer-owned objects;
  each delivered event runs with postponed-call processing
  deferred and is followed by a drain of every pending `Ui::PostponeCall` to empty, so
  postponed input fix-ups AND their own change handling are settled when the helper returns;
  wrap programmatic `setText` in `Test::Settle`.
- `test_capture.h` — `CaptureWidget`/`CaptureRect` (visibility check, `QWidget::grab()` so
  floating elements and locked desktops cannot occlude, automatic blank-image FAIL, geometry
  log, `SCREENSHOT` marker), plus `PreparedWidgetCapture`; `Runner::captureWidget` polls an
  exact target until it has a valid painted frame and saves that same accepted frame,
  `Crop`/`Zoom`/`ContactSheet` for tight same-scale evidence.
- `test_ink.h`, `test_style.h`, and `test_panel.h` — painted-ink/contrast measurement, settled
  palette baselines, and detection of a `Ui::SeparatePanel` show-animation cache.
- `test_messages.h`, `test_transfer.h`, `test_open_handoff.h`, and `test_launch_fuse.h` — direct
  sent-message, document-transfer, document-open, and safely blocked OS-launch oracles. Prefer
  these established task-specific observers over reconstructing the same state in a scenario.
- `test_agent.h` — `Test::Fire(name)` / `HasFired(name)` named waitpoints;
  `launch_finished` fires at the end of `Application::run()`. `TDESKTOP_TEST_SCALE` is applied
  by the harness at startup, and on macOS the harness holds the App Nap opt-out for the whole run
  (`TDESKTOP_TEST_APP_NAP=allow` leaves it off; see [launch behavior](launch.md)).
- `test_scenario.cpp` — the overlay-owned slot: it defines `Test::SetupScenario(runner)` and
  is a no-op in the repository.

A minimal scenario shape:

```cpp
void SetupScenario(not_null<Runner*> runner) {
	runner->waitEvent(u"launch_finished"_q);
	runner->actOnWidget(
		u"activate the published target"_q,
		[] { return Test::ReadLiveWidget(u"task.target"_q).widget; },
		[](QWidget *widget) { Test::Click(widget); },
		[](QWidget *widget) {
			return widget->isVisible() && widget->isEnabled();
		},
		Test::kDefaultStageTimeout,
		[](QWidget *widget) {
			return u"visible=%1 enabled=%2 size=%3x%4"_q
				.arg(widget->isVisible())
				.arg(widget->isEnabled())
				.arg(widget->width())
				.arg(widget->height());
		});
	runner->captureAndInspect(
		u"target_surface"_q,
		[] { return Test::ReadLiveWidget(u"task.surface"_q).widget; },
		[](QWidget *widget) {
			return widget->property("contentGeneration").toInt() > 0;
		},
		[](QWidget *widget, const QImage &image) {
			Test::LogGeometry(u"surface"_q, widget->geometry());
			Test::Check(
				image.width() >= widget->width(),
				u"capture covers the surface width"_q,
				u"image=%1 widget=%2"_q
					.arg(image.width())
					.arg(widget->width()));
		});
}
```
