# Telegram Desktop task-test harness

This directory contains the permanent Debug-only infrastructure used by
`perform-task` overlays. Read this catalog before designing or recovering a
scenario. The overlay should add only task-specific fixtures, production
entry points, checks, and captures; do not recreate facilities already here.

The default is one packed scenario and one app launch. Put all compatible
checks, themes, values, interactions, and captures into that execution. Plan
another launch only when you can name an incompatible process-lifetime
requirement, such as a startup-only setting or persisted state that the first
flow necessarily contaminates. “The scenario is large” and “a smoke run is
simpler” are not reasons to split it. After a `TEST_FLAW`, repair every flaw
visible in the run together and rerun the packed scenario.

## The stage contract

`Runner` executes each `Stage` as three distinct operations:

| Member | Contract | Must not do |
| --- | --- | --- |
| `run` | One action whose prerequisites were established by an earlier stage. | Dereference an object that this stage is supposed to wait for. |
| `until` | A pure, repeatable readiness observation. | Click, mutate state, emit a PASS/FAIL, or encode the expected product result. |
| `then` | Assertions and the next action after readiness succeeded. | Resolve an unguarded replacement for the object that readiness accepted. |
| `timeoutDetails` | Return the latest observed values and identities. | Repeat only “not ready”; omit the values needed to diagnose why. |

A stage timeout is a harness failure unless the readiness condition itself is
the behavior under test. Keep expected geometry, text, pixels, counts, and
other falsifiable outcomes in `then` or `captureAndInspect`, not in `until`.
Otherwise a real product mismatch is mislabeled as an opaque timeout.

Use `Runner::actOnWidget` when an action depends on a widget that may appear
asynchronously. It polls the resolver and optional readiness predicate, keeps
a `QPointer` to the exact accepted object, and invokes the action once. This
removes both the eager-`.run` crash and the “resolve again in `.then`” race.

The generic startup waits are opt-in:

- `waitEvent("launch_finished")` is the normal first gate.
- `waitForSessionReady()` is needed only when the scenario consumes a
  session.
- `waitForChatsLoaded()` is needed only when the tested setup consumes the
  chats list; it is non-fatal and records whether loading completed.
- `waitForChatsLoadedStrict()` is appropriate only when chats loading is part
  of the required fixture or result.

Do not put all three waits in every scenario preamble. An injected box,
helper oracle, or direct production seam often needs none of the account or
chats gates.

## Exact objects and actions

Telegram custom widgets generally do not declare `Q_OBJECT`. Never call
`findChildren<Ui::CustomWidget*>()`; Qt may return unrelated widgets blindly
cast to that type. `FindAll<T>`, `FindFirst<T>`, and `FindVisible<T>` enumerate
real `QWidget` descendants and apply C++ RTTI safely.

`FindVisible` means only `QWidget::isVisible()`. It does not prove that the
widget belongs to the current layer, is unobscured, has non-empty mapped
geometry, or is the latest instance. Add those task-specific conditions, or
publish the exact object from its construction seam.

For layer-owned objects, repeated boxes, async replacements, and footer
callbacks with no returned pointer, use the live publication helpers:

```cpp
// In an inventoried Debug overlay hunk at the real construction seam.
Test::PublishLiveWidget(u"gift.composer"_q, composer);
Test::PublishLiveAction(
	u"gift.confirm"_q,
	box,
	[=] { confirm(); });

// In test_scenario.cpp.
runner->actOnWidget(
	u"open current composer"_q,
	[] { return Test::ReadLiveWidget(u"gift.composer"_q).widget; },
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
```

Each publication advances a generation. A destroyed context makes its widget
or action unavailable. Actions are one-shot by default; pass `true` only for
a production callback that is intentionally retryable. Use
`ReadLiveAction()` to wait for a specific generation and invocation count,
then `InvokeLiveAction()` in an action/assertion stage. This is preferable to
guessing which `RoundButton` belongs to a presentation wrapper.

## Input helpers

All input helpers synchronously dispatch real Qt events and drain
`Ui::PostponeCall` work after each event. They lifetime-guard the target and
stop if it is destroyed.

| Helper | Use |
| --- | --- |
| `Click` | Left press and release at the center or a supplied local point. |
| `TypeText` | Key press/release per Unicode grapheme; surrogate pairs and joined emoji stay intact. |
| `CommitText` | One `QInputMethodEvent` commit when insertion, custom emoji, or IME semantics matter more than physical keys. |
| `PressKey` | Escape, Return, arrows, shortcuts, and other key behavior. Send to the widget that owns the event route, often the raw editor or top `BoxLayerWidget`, not a wrapper. |
| `Drag` | Left-button press, interpolated moves with `buttons()==LeftButton`, and release. |
| `Wheel` | A real wheel event; `QPoint(0, -120)` is one conventional downward step. |
| `Settle` | Wrap programmatic mutations such as `InputField::setText`; drains postponed fixups after the action. |

Timers, queued invokes, animations, network callbacks, and reactive streams
still require condition waits. `Settle` is not a replacement for readiness.

## Capture and visual assertions

Choose the narrowest helper that captures the real paint owner:

| Need | Helper |
| --- | --- |
| Static visible widget or local rect | `CaptureWidget` / `CaptureRect` |
| Rect expressed in a child or offscreen content widget's coordinates | `CaptureMappedRect` |
| Full box, layer owner, animation, or asynchronously populated surface | `Runner::captureWidget` |
| Exact accepted frame plus numeric/raster assertions | `Runner::captureAndInspect` |
| Small target comparison | `Crop`, `Zoom`, `ContactSheet` |
| Foreground colour, contrast, or painted-band measurement | `MeasurePaintedInk` and the helpers in `test_ink.h` |

`captureWidget` and `captureAndInspect` reject missing, hidden, empty,
near-uniform, and invalid transparent-root frames. Their readiness predicate
should identify current content: model id, generation, title, child count,
paint generation, or settled animation state. It must not assert that the
result has the expected height or colour.

`captureAndInspect` saves the accepted image before it runs assertions, so a
failure still has decisive evidence:

```cpp
runner->captureAndInspect(
	u"gift_composer_150"_q,
	[] { return Test::ReadLiveWidget(u"gift.composer"_q).widget; },
	[](QWidget *widget) {
		return widget->property("contentGeneration").toInt() > 0;
	},
	[](QWidget *widget, const QImage &image) {
		Test::Note(u"composer size=%1x%2 image=%3x%4"_q
			.arg(widget->width())
			.arg(widget->height())
			.arg(image.width())
			.arg(image.height()));
		const auto content = widget->childrenRect();
		Test::Check(
			widget->rect().contains(content),
			u"composer contains its child geometry"_q,
			u"owner=%1,%2 %3x%4 children=%5,%6 %7x%8"_q
				.arg(widget->rect().x())
				.arg(widget->rect().y())
				.arg(widget->rect().width())
				.arg(widget->rect().height())
				.arg(content.x())
				.arg(content.y())
				.arg(content.width())
				.arg(content.height()));
	});
```

If a child grab exposes the harness background, capture the ancestor that
paints the background. If a row is clipped by a scroll viewport, grab the
content widget and map the row rect with `CaptureMappedRect`; do not treat a
partial screenshot as product evidence.

## Helper catalog

| Module | Facilities |
| --- | --- |
| `test_agent.h` | Runtime gate, startup scale override, sticky named events, scenario start. |
| `test_runner.h` | Stages, bounded waits, exact-widget actions, prepared capture/inspection, watchdog and termination. |
| `test_log.h` | Absolute flushed logs, steps, notes, checks, tolerances, geometry, completion markers. |
| `test_widgets.h` | Safe typed discovery, live object/action publication, input, and postponed-call settlement. |
| `test_capture.h` | In-process grabs, paint-root validation, mapped rects, blank detection, crops, zoom, contact sheets. |
| `test_ink.h` | Derived paint bands, colour separation, ink scans, counts, and contrast reports. |
| `test_style.h` | Wait for palette/style samples to stabilize and assert a recorded baseline still holds. |
| `test_panel.h` | Distinguish a live `Ui::SeparatePanel` from its faded/squeezed show-animation cache. |
| `test_messages.h` | Lifetime-owned watcher for a matching newly sent server message. |
| `test_launch_fuse.h` | Declare and verify operating-system launches while refusing every real launch in test-agent mode. |
| `test_open_handoff.h` | Inspect and assert the document-open branch without handing anything to the OS. |
| `test_transfer.h` | Observe document save/failure transitions and assert duplicate or failed transfer behavior. |
| `test_scenario.cpp` | The only permanent overlay slot; the repository version remains a no-op. |

Read the selected module's header before using it; the contracts there are
more precise than the summary above. Search this directory before writing a
new local helper.

## Building one reliable packed scenario

1. Derive every falsifiable check from the task, visual contract, and retained
   diff before writing overlay code.
2. Choose the most direct fixture that still executes the changed production
   code. Publish exact objects/actions at construction and callback seams when
   unrelated navigation would be the fragile part.
3. Arrange nondestructive states first and destructive/closing actions last.
   Reuse one live fixture through its empty, rich, long, error, retry, light,
   and dark states when those transitions are production behavior.
4. Put readiness and generation checks in `until`; put actual-versus-expected
   values in `then` or `captureAndInspect`.
5. Log every value needed to diagnose a failure in the first run. A timeout
   should name the last object generation, visibility, geometry, state, and
   callback count instead of forcing a speculative second overlay edit.
6. Save decisive tight captures as the scenario reaches each state. Do not
   postpone all visuals to the end, after later actions have replaced them.
7. Finish with guards: no undeclared OS launch, no real payment/network call
   when mocked, expected callbacks exactly once, and no leftover expectation.

## Failure diagnosis

| Symptom | Likely harness cause | Repair |
| --- | --- | --- |
| Timeout before the task fixture exists | Generic account/chats preamble or unrelated navigation. | Remove unused startup gates; inject the fixture or publish the production object at its real seam. |
| Assertion/crash in a stage action | `.run` dereferenced an async object or a raw pointer outlived its owner. | Use `actOnWidget`, `QPointer`, or live publication. |
| Wrong custom widget/button found | Unsafe Qt typed search or ambiguous descendant order. | Use the RTTI finders; for repeated/layer-owned controls publish the exact object/action. |
| Expected mismatch reported as timeout | Product outcome was put in `until`. | Wait only for propagation/generation; assert and log the outcome in `then`/`captureAndInspect`. |
| Blank or partial screenshot | Wrong paint owner, animation cache, or viewport clipping. | Use prepared capture, `PanelShowSettled`, the owning ancestor, or `CaptureMappedRect`. |
| Old palette/colour sampled | Style had not settled or moved between reference and target. | Use `StyleSettled` and `StyleBaseline`. |
| Emoji split or custom entity absent | UTF-16 code units or the wrong editor event route were synthesized. | Use grapheme-safe `TypeText` or one `CommitText` on the raw editor. |
| Drag/wheel/cancel has no effect | Wrapper received an event owned by a child, presentation, or viewport. | Target the real event owner and use `Drag`, `Wheel`, or `PressKey`. |
| Test reaches a real external action | Missing expectation/fuse or mock seam. | Declare the exact blocked launch, mock the transport/payment boundary, and assert zero real calls. |

Classify a sound assertion against changed behavior as an implementation bug,
not a test flaw. Classify a wrong fixture, target, readiness model, event
route, capture owner, or oracle as a test flaw. Preserve everything a failed
run proved, repair all visible harness faults together, and rerun the packed
scenario rather than fragmenting it into isolated launches.
