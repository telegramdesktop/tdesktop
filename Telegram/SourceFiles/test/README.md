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
| `until` | A pure, repeatable readiness observation. | Click, mutate state, emit a PASS/FAIL, or encode the expected product result. The one documented exception is the `Test::ForceWindowActive` re-assertion in the activation row under Failure diagnosis. |
| `then` | Assertions and the next action after readiness succeeded. | Resolve an unguarded replacement for the object that readiness accepted. |
| `timeoutDetails` | Return the latest observed values and identities. | Repeat only “not ready”; omit the values needed to diagnose why. |
| `skipReason` | Return why this stage does not apply, or an empty string when it does. A non-empty reason writes `TEST_RESULT: N/A: <stage> - <reason>`, skips `run`, `until` and `then`, and moves on in the same turn. | Decide the product outcome. A gate that reads a measurement instead of a precondition turns a would-be FAIL into a silent N/A. |

A stage timeout is a harness failure unless the readiness condition itself is
the behavior under test. Keep expected geometry, text, pixels, counts, and
other falsifiable outcomes in `then` or `captureAndInspect`, not in `until`.
Otherwise a real product mismatch is mislabeled as an opaque timeout.

Use `Runner::actOnWidget` when an action depends on a widget that may appear
asynchronously. It polls the resolver and optional readiness predicate, keeps
a `QPointer` to the exact accepted object, and invokes the action once. This
removes both the eager-`.run` crash and the “resolve again in `.then`” race.
The same rule is built into the harness's widget-carrying readings:
`Test::BoxShellButtons`, `Test::WindowMappedCapture` and
`Test::PaintingLayerRootResult` hold `QPointer<QWidget>`, so a reading kept
across a stage boundary answers `matched()` / `resolved()` false once its
subject is destroyed and still prints its counts, labels, identity and
refusal; format a `WidgetDescription` of the pointer while the reading is
still matched, because there is nothing to format afterwards.

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

## Resolving a chat's message list

A chat's message list is not necessarily `HistoryWidget` / `HistoryInner`.
The legacy main-chat stack is `HistoryWidget` (`history/history_widget.h`)
plus `HistoryInner` (`history/history_inner_widget.h`); the modern stack is
`HistoryView::ChatWidget` (`history/view/history_view_chat_section.h`) plus
`HistoryView::ListWidget` (`history/view/history_view_list_widget.h`); and the
admin log is a third stack again, `AdminLog::InnerWidget`, which implements
`HistoryView::ElementDelegate` itself and constructs no `ListWidget`.
`ChatWidget` already backs replies threads, forum topics and both monoforum
and Saved Messages sublists (both are `Data::SavedSublist`); scheduled
messages, pinned messages, welcome messages and the chat preview popup each
construct their own `ListWidget`.

A scenario that assumes one named widget renders every chat fails opaquely.
Publishing a live widget from `HistoryWidget`'s construction seam and then
walking the window logged `RENDER_ROUTES: published=0 walked=0 chain=[none]`
against 403 root descendants — the `HistoryInner` was never constructed, and
the walk found none either — while the window grab plainly showed the chat
painting the row.

The rule is therefore about resolution, not about a name, and stays true as
the migration proceeds. Resolve the list from the section that constructs it
and prove the candidate owns the message:

- publish the exact live object at the statement that constructs the list —
  `_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(` in
  `history/view/history_view_chat_section.cpp` — through
  `Test::PublishLiveWidget`;
- keep `Test::FindVisible<HistoryView::ListWidget>` as an independent
  cross-check and log both answers;
- accept a candidate only when its own lookup answers for the exact message:
  `list->viewByPosition(item->position())` must return an `Element` whose
  `data()` is that item and whose `delegate()` is that same list;
- scroll with the product's own entry points, `showThread` and then the list's
  `showAtPosition`; decide where the row sits with the list's public
  `elementIntersectsRange(view, from, till)`, and frame it with
  `Test::CaptureMappedRect`.

`item->mainView()` is not the `Element` the visible section owns, so it is
neither a readiness gate nor a geometry oracle. For a row a `ListWidget` was
painting it answered `view=1` with `viewHeight=0` and `itemTop=-3`; use the
list's own `viewByPosition` for both jobs instead.

The parent chain a resolved `ListWidget` produced, as the shape to expect, was
`ListWidget < Ui::ElasticScroll < ChatWidget < MainWidget < Ui::RpWidget <
Ui::RpWidget < MainWindow`.

## Input helpers

All input helpers synchronously dispatch real Qt events and drain
`Ui::PostponeCall` work after each event. They lifetime-guard the target and
stop if it is destroyed.

| Helper | Use |
| --- | --- |
| `Click` | Left press and release at the center or a supplied local point, then a `QEvent::Leave` to that same widget, so `Ui::AbstractButton::isOver()` is false on it and a `RoundButton` repaints its normal `textBg`. Send it the widget that accepts the press — an ignored press propagates to the ancestor that takes it and the leave does not, so a click aimed at a non-accepting child leaves that ancestor hovered. |
| `TypeText` | Key press/release per Unicode grapheme; surrogate pairs and joined emoji stay intact. Send it to the widget that owns the key route: for a `Ui::InputField` that is `rawTextEdit()`, never the wrapper. |
| `CommitText` | One `QInputMethodEvent` commit when insertion, custom emoji, or IME semantics matter more than physical keys. Same target rule as `TypeText`, and the fallback when key events left the field empty. |
| `PressKey` | Escape, Return, arrows, shortcuts, and other key behavior. Send to the widget that owns the event route, often the raw editor (`Ui::InputField::rawTextEdit()`) or the top `Ui::BoxLayerWidget`, not a wrapper. |
| `Drag` | Left-button press, interpolated moves with `buttons()==LeftButton`, and release, then the same `QEvent::Leave` as `Click`. |
| `Wheel` | A real wheel event; `QPoint(0, -120)` is one conventional downward step. |
| `Settle` | Wrap programmatic mutations such as `InputField::setText`; drains postponed fixups after the action. |
| `ForceWindowActive` | Inject window activation through the QPA seam before any focus-routed action, and re-assert it on every poll of a bounded wait. On a locked console the window is never active in Qt's sense, so `setFocus()` never reaches `QApplication::focusWidget()` and every `hasFocus()`/`isActiveWindow()` branch silently reads false. |
| `ClickBoxButton` | Click a box's shell footer button by label. `addButton` re-parents the button onto the `Ui::BoxLayerWidget`, so a content-rooted `FindAll<Ui::RoundButton>` never sees it; this roots the search at `Test::PaintingLayerRoot(box)`, keeps only its direct children, and refuses by name when nothing matches. |

The harness has no pointer. No helper produces a hover, and every
measurement is taken as if no cursor exists — assert a hovered state by
setting it deliberately, not by relying on a click's residue. A press also
starts a `RippleButton` ripple that decays over its style's
`showDuration + hideDuration`; finish it with `finishAnimating()` or wait
it out before measuring a fill.

Timers, queued invokes, animations, network callbacks, and reactive streams
still require condition waits. `Settle` is not a replacement for readiness.

## Capture and visual assertions

Choose the narrowest helper that captures the real paint owner:

| Need | Helper |
| --- | --- |
| Static visible widget or local rect | `CaptureWidget` / `CaptureRect` |
| Rect expressed in a child or offscreen content widget's coordinates | `CaptureMappedRect` |
| Full box, layer owner, animation, or asynchronously populated surface | `Runner::captureWidget` |
| Full box inside a layer | `PaintingLayerRoot` + `Runner::captureWidget`, or `CaptureInLayerRoot` |
| Open `Ui::PopupMenu` | `CapturePopupMenu` |
| Widget that paints no opaque background of its own (a toast, a fade-in wrapper) | `CaptureViaWindow` |
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
		Test::Note(u"composer image=%1x%2"_q
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

Measurements cross three coordinate systems, and each has a fixed contract.
`CaptureWidget` / `CaptureRect` / `CaptureMappedRect` take widget LOGICAL
rects; the image they produce — like every `Ui::GrabWidgetToImage` grab —
holds logical-size × `style::DevicePixelRatio()` DEVICE pixels, so `Crop`
and any direct `QImage` sampling index device space. Multiply logical values
by the image's `devicePixelRatio()` exactly once, at the sampling boundary.
`TDESKTOP_TEST_SCALE` is a third, independent multiplier: it scales the
logical style metrics themselves, so derive expected geometry from the live
scaled tokens, never from literals recorded at 100%. Rects from different
widgets never share an origin by accident — map both through one declared
frame (`Ui::MapFrom`, `mapToGlobal`) before comparing, cropping, or
clicking.

## Helper catalog

| Module | Facilities |
| --- | --- |
| `test_agent.h` | Runtime gate, startup scale override, sticky named events, scenario start. |
| `test_runner.h` | Stages, bounded waits, exact-widget actions, prepared capture/inspection, first-class gated skips (`skipReason`), watchdog (`TDESKTOP_TEST_WATCHDOG` in seconds) and termination. |
| `test_gated_stage.h` | The first-class gated skip's own self-test: a stage whose `skipReason` returns a reason, writing one `TEST_RESULT: N/A:` row and skipping `run`, `until` and `then` without waiting - its never-ready `until` under a one-second timeout is the falsifier - beside a stage whose gate returns an empty string and runs normally in the tick that begins it. |
| `test_log.h` | Absolute flushed logs, steps, notes, checks whose `details` are printed on the passing verdict as well as the failing one, tolerances, geometry, completion markers, N/A rows for stages that did not apply, and their count. One `LogRaw` call always writes exactly one physical line, whatever it is handed: every character Python's `str.splitlines()` breaks on - U+000A, U+000B, U+000C, U+000D, U+001C, U+001D, U+001E, U+0085, U+2028, U+2029, and so a CRLF pair as its two code points - is written as a visible `\uXXXX` escape, so a record carrying a break stays one row the external readers' line grammar reads whole and cannot mistake for a completion, while text with no separator is passed through byte for byte and the escape adds no trailing whitespace. |
| `test_log_lines.h` | The one-physical-line guarantee's own self-test: one `LogRaw`-family call driven with each of the eleven separator forms in turn, with one payload mixing them all, with a trailing separator and with a separator-only payload, each read back out of `test_log.txt` by byte offset and asserted to have added exactly one physical line under an independent transcription of `str.splitlines()` - beside a separator-free control that must read one line under any writer, and a last stage whose payload's middle line would be byte-equal to `TEST_COMPLETE` and which asserts no produced line is. |
| `test_text_reads.h` | Space-class-normalizing text comparison: `Test::NormalizeSpaces` maps U+00A0 and U+202F to U+0020 and changes nothing else, `Test::CheckTextReads` compares a read-back against an expectation through it and prints both raw strings and their whitespace code points on either verdict; with its own self-test, in which a real `Ui::FlatLabel`'s narrow-no-break-space read-back is accepted beside two deliberate FAIL negative controls - a different minute and a different day - that keep the check from becoming permissive. |
| `test_probe.h` | Append-only observation records read only through a declared window, each carrying the time it was recorded; keyed issue/answer rows correlated into one round trip by key rather than by list position, refusing every reading it cannot positively pair; and scans that must match a control before a zero counts as absence. |
| `test_widgets.h` | Safe typed discovery, live object/action publication, input, postponed-call settlement, and QPA-injected window activation. |
| `test_capture.h` | In-process grabs, paint-root validation, mapped rects, blank detection, painting-layer-root resolution for boxes inside a layer, crops, zoom, contact sheets, window-mapped capture for widgets that paint no opaque background of their own. |
| `test_layer_root.h` | The painting-layer-root resolver's own self-test: a plain `Ui::GenericBox` accepted as its own render root beside one that cleared `Qt::WA_OpaquePaintEvent`, refused and then captured through its `Ui::BoxLayerWidget`. |
| `test_via_window.h` | The window-mapped capture's own self-test: a real `Ui::Toast` whose bare prepared grab is refused in the turn it is created, beside the window-cropped frame of the same rect that the harness accepts, and a flat fixture region proving a blank frame is a `Note` and never a FAIL; a sixth stage builds an offscreen `Qt::WA_DontShowOnScreen` top level, reads it, destroys it inside the same `.run`, and shows the retained reading answering `resolved()` false with its identity, mapped rect and refusal intact. |
| `test_hover.h` | The input helpers' own self-test: a clicked-then-dragged and a never-touched `Ui::RoundButton` measured against the two fills their style names, proving a completed synthetic click and drag leave no hover. |
| `test_activation.h` | The window-activation helper's own self-test: the application deliberately de-activated and re-activated through the same QPA seam inside one stage, the wrapper-versus-inner `hasFocus()` contrast and the wrapper-versus-`rawTextEdit()` typing contrast measured on one real `Ui::InputField`. |
| `test_box_button.h` | Click a box's shell footer button by label, rooted at `Test::PaintingLayerRoot`, with its own self-test: a content-rooted `Ui::RoundButton` search that reaches none of the footer buttons beside the rooted search that finds and clicks one, and a named refusal for a label no shell button carries. |
| `test_ink.h` | Derived paint bands, colour separation, ink scans, counts, and contrast reports. |
| `test_style.h` | Wait for palette/style samples to stabilize and assert a recorded baseline still holds. |
| `test_panel.h` | Distinguish a live `Ui::SeparatePanel` from its faded/squeezed show-animation cache. |
| `test_menu.h` | Deterministic `Ui::PopupMenu` capture whose readiness is content identity only, its `showingContent` reading, and its own self-test. |
| `test_messages.h` | Lifetime-owned watcher for a matching newly sent server message. |
| `test_history_fixtures.h` | Inject a caller-supplied service action into a real history as a regular or (negative-control) local item, with a caller-owned lifetime that removes it, and log the menu-gating predicates. |
| `test_custom_emoji.h` | Supply an always-ready `Ui::Text::CustomEmoji` that fills the large-emoji box with worst-case ink, handed out only for document ids the scenario itself registered. |
| `test_launch_fuse.h` | Declare and verify operating-system launches while refusing every real launch in test-agent mode. |
| `test_open_handoff.h` | Inspect and assert the document-open branch without handing anything to the OS. |
| `test_transfer.h` | Observe document save/failure transitions and assert duplicate or failed transfer behavior. |
| `test_rpc_retry.h` | The permanent MTP resend seam: `Test::RecordRpcRetry` records one `rpc retry code=<code> type=<type> request=<constructor>` row for every code-500 or negative-code answer the transport auto-resends without calling the request's fail handler, read through the `Test::RpcRetryProbe()` accessor; with its own self-test for the recorded 500, the non-500 that reaches `.fail()` instead, and the answer for a request id this process never sent. |
| `test_scenario.cpp` | The only permanent overlay slot; the repository version remains a no-op. |

`Test::Check`'s third argument is an observation, not a failure excuse. It is
printed on both verdicts — `TEST_RESULT: PASS: <what> - <details>` and
`TEST_RESULT: FAIL: <what> - <details>` — so a green log says what each check
was made against and a passing run can be audited without re-running it. Pass
the values the check judged: the measured geometry, the observed identity, the
window and the rows behind the verdict. Text that is only true after a failure
stays conditional at the call site — `ok ? QString() : u"out of tolerance"_q` —
which leaves the passing line exactly `TEST_RESULT: PASS: <what>`, the same
line an empty `details` produces. Do not emit a `Note` beside a check only to
print a reading that check's own `details` could carry; that duplication is
what this argument replaces. Where the failure text cannot double as a true
passing observation, the `details` stay conditional and an adjacent `Note`
remains the carrier — `CheckBlockedLaunchesExactly` suppresses its mismatch
text on a pass and keeps its `Test::Note(u"blocked launch record: [...]")`,
which is what `test_launch_fuse.h` promises. None of this loosens a refusal:
failure text still has to name what it judged, and an undecidable reading is
still refused rather than printed as a `Note` a reader would take as a
measurement.

Read the selected module's header before using it; the contracts there are
more precise than the summary above. Search this directory before writing a
new local helper: an overlay that reimplements a shared facility is a test
flaw like any other, because the local copy carries none of the refusals the
shared one accumulated. One overlay rebuilt grab-check-save by hand after
`CaptureWidget` had already refused blank and hidden targets for months.

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

## Media fixtures and fixture gates

A media document is a fixture only after the run has watched it play. Metadata
does not decide this: an undecodable upload left on the shared test account can
report `song=1 audioFile=1 sharedMusic=1` with a plausible `duration`, so no
predicate over flags, title, performer, filename, or membership in some list can
tell it apart from real music. “The first song not already in profile Saved
Music” is exactly such a predicate, and it accepts an undecodable upload.
Observe playback instead:

1. Order the candidates the account already exposes by playability signals —
   already downloaded, then a longer duration, then a non-empty title or
   performer — and push known-synthetic names behind every other candidate.
   The order is a preference; nothing on it is trusted yet.
2. Really play each candidate in turn, through the ordinary production path
   (`Window::SessionController::openDocument()` with that candidate's own
   message as its `MessageContext`), and sample
   `Media::Player::instance()->getState(AudioMsgId::Type::Song)` around a
   bounded wait. Accept the first candidate whose `TrackState::position`
   strictly advances while `length > 0` and the playing id and context are
   still that candidate's own document and message.
3. Stop the probe with `Media::Player::instance()->stop(AudioMsgId::Type::Song)`
   after every attempt and assert the player is stopped, so no probe state
   leaks into the measurement that follows.
4. Feed the one validated document to every stage that needs it — the fixture,
   any injection, and the negative control — instead of re-deriving it per
   stage.

The reported `length` corroborates a probe's verdict but never decides it.
Until the stream reports its own duration,
`Media::Streaming::Player::prepareLegacyState()` substitutes the document's
declared duration, so a length equal to the declared duration means only that
no stream duration has arrived yet — a healthy candidate sampled early reads
the same way. It is a bad sign only together with a position that never
advances, while a document that really opened reports a stream-measured length
instead (`len=272910` against a declared `272000`). Keep acceptance anchored on
the strict position advance.

When a scenario's subject needs a streaming fixture, a stage immediately before
the measured action must prove the track is already advancing while the subject
still exists. Classify that stage as a fixture gate, not a check: reverting the
diff under test cannot change its reading, so its failure makes the acceptance
criteria `N/A` and the run a test flaw — never a `FAIL`. Without that gate, a
frozen reading taken after the action cannot be told apart from the product
failing.

## Scenario teardown before quit

A scenario that deliberately leaks its `State` so it outlives a stage owns the
release of everything that `State` holds into session-owned objects: every
`rpl::lifetime`, every watcher, and every raw cross-stage pointer. Destroy
them in a final teardown stage that runs before the runner quits.

Nothing else does it, and skipping it costs a whole run. A leaked `State`
whose two `rpl::lifetime`s were never released kept observers subscribed to
`Storage::Facade`'s and `Data::Session`'s streams while `~Main::Session` tore
those streams and their items down. The run reached `TEST_COMPLETE` after a
clean sweep and then died with `Caught signal 11 (SIGSEGV)`, no assertion line
anywhere in the run's logs, and a 0-byte minidump because the runner had to
kill the process. With the teardown stage added, three consecutive runs exited
0, wrote no crash report and left no minidump.

The stage is not guaranteed to run. A stage that times out, and the scenario
watchdog, make `Runner` finish immediately and skip every stage after it,
while a stage whose assertions `FAIL` does not — that run still reaches its
teardown. After a timeout the same death can follow `TEST_COMPLETE`, so read
it as this signature rather than as a second product fault, and keep the
leaked `State`'s session-observing subscriptions no wider than the stages that
need them.

## Failure diagnosis

| Symptom | Likely harness cause | Repair |
| --- | --- | --- |
| Run exits with no `TEST_COMPLETE` and no `SCENARIO_RESULT` | `TDESKTOP_TEST_WATCHDOG` is seconds; a millisecond-shaped value (for example `600000`) used to arm a multi-day timer that `test-run --deadline` always outruns, so the watchdog never wrote the markers. | Override is seconds in 1..600; implausible values fall back to 120s and the armed duration is logged next to `SCENARIO_START`. Use a value in that range, or omit the variable. |
| Timeout before the task fixture exists | Generic account/chats preamble or unrelated navigation. | Remove unused startup gates; inject the fixture or publish the production object at its real seam. |
| Assertion/crash in a stage action | `.run` dereferenced an async object or a raw pointer outlived its owner. | Use `actOnWidget`, `QPointer`, or live publication. |
| Wrong custom widget/button found | Unsafe Qt typed search or ambiguous descendant order. | Use the RTTI finders; for repeated/layer-owned controls publish the exact object/action. |
| Expected mismatch reported as timeout | Product outcome was put in `until`. | Wait only for propagation/generation; assert and log the outcome in `then`/`captureAndInspect`. |
| A stage times out at exactly the length of a product deadline it was waiting through - `stage timed out: ... - waited 60000 ms` on a stage whose `until` awaits an outcome the product bounds with its own timer - while the product's own fail row, which that `until` already accepts, would have arrived a moment later | The stage timeout coincides with the product's deadline, so the harness ends the stage before the product decides and the run records an instrument timeout where a decided negative existed. The row above is the neighbouring fault: there the outcome was put in `until`; here it is in `then` already, but the wait was not given room to reach it. | Make a stage that waits through a product timer outlast it by a clear margin - 75 s over a 60 s deadline - accept the product's own fail row as the decided negative, and keep the outcome assertion in `then`. |
| A request never answers although the server did: no `.done()`, no `.fail()`, no product row, the stage waits until its timeout, while the application Debug log repeats `RPC Info: error received, code 500, type <T>` for the same request | `Instance::Private::onErrorDefault` auto-resends every code-500 and negative-code `rpc_error` after a doubling delay and never calls a product callback for it, so nothing a scenario can read ever changes. | Read the `rpc retry code=<code> type=<type> request=<constructor>` rows the permanent seam records into `Test::RpcRetryProbe()` (`test_rpc_retry.h`), through a `mark()` taken immediately before the action, and decide from those rows plus the product's own fail row - never from the stage timeout. `<constructor>` is the boxed body constructor id in hex, compared against the `mtpc_*` constant the scenario cares about. The Debug log's lines stay the fallback for a reader without the harness log. |
| Blank or partial screenshot | Wrong paint owner, animation cache, or viewport clipping. | Use prepared capture, `PanelShowSettled`, the owning ancestor, or `CaptureMappedRect`; for a box inside a layer, `Test::PaintingLayerRoot`; for a `Ui::PopupMenu`, `Test::CapturePopupMenu`. |
| Old palette/colour sampled | Style had not settled or moved between reference and target. | Use `StyleSettled` and `StyleBaseline`. |
| A clicked button's measured fill matches no style constant, or `DeriveBand` returns `ok=0` with no rows for a widget plainly on screen | The reading was taken while the widget was still hovered by an earlier synthetic click, so it painted `textBgOver` where the check named `textBg`; before the input helpers delivered a leave this latched for the whole process. | Take the reading through helpers that leave the target pointerless (`Click`/`Drag` deliver a `QEvent::Leave`), and when a hovered reading is what is wanted, set the hover deliberately and name the fill the state actually implies. Confirm the instrument with the `test_hover.h` self-test. |
| Emoji split or custom entity absent | UTF-16 code units or the wrong editor event route were synthesized. | Use grapheme-safe `TypeText` or one `CommitText` on the raw editor. |
| Drag/wheel/cancel has no effect | Wrapper received an event owned by a child, presentation, or viewport. | Target the real event owner and use `Drag`, `Wheel`, or `PressKey`. |
| Test reaches a real external action | Missing expectation/fuse or mock seam. | Declare the exact blocked launch, mock the transport/payment boundary, and assert zero real calls. |
| Pixel probe misses only on Retina or at 125/150% | Logical rect indexed into the device-pixel grab, or a 100% literal reused at another interface scale. | Multiply by the image `devicePixelRatio()` once at the sampling boundary; derive expectations from the live scaled tokens. |
| Geometry oracle fails on plausible-looking rects | Rects from different widgets compared without a shared origin. | Map both through one declared frame (`Ui::MapFrom`, `mapToGlobal`) and log the mapped values in the failure details. |
| Process dies after `TEST_COMPLETE`, with no assertion line and often a 0-byte dump | Overlay teardown, not the product: a leaked scenario `State` still holds `rpl` subscriptions to session-owned streams while `~Main::Session` destroys them. | Destroy the scenario's lifetimes, release its watchers and null every raw cross-stage pointer in a final teardown stage — and check it ran, because a timed-out stage or the watchdog skips every stage after it. |
| Media reading frozen at position `0`, with the length equal to the document's declared duration | Undecodable fixture document — often a synthetic upload left on the shared test account — accepted on metadata alone; the app debug log shows `Streaming Error: Error in avformat_open_input`. | Select the fixture with the playability probe: play each candidate and accept only one whose position strictly advances, then reuse that document everywhere. |
| A premise fails against a row its own fixture had to create, or a check passes without ever reaching its subject | The oracle read the probe's whole history, or bracketed a slice by wall time, so rows from an earlier stage or a slow neighbouring surface answered it. | Record through `Test::Probe`, take `mark()` immediately before the action, and query only `...Since(mark)`; there is no whole-history accessor to fall back to. |
| A sweep reports a confident `found=0` that no repair ever changes | The enumeration structurally cannot reach the subject, so the zero was guaranteed before the run started and measures nothing. | Count through `Test::DiscriminatingScan` and feed it a known-present control; `report()` refuses to certify a zero the walk cannot tell from absence. |
| A green log that does not say what its checks were made against, so a passing run cannot be audited after the fact | The reading was handed to `Test::Check` as `details` back when `details` was written only on the failing branch, or worked around by folding it into `what` or by emitting a `Note` beside the check that a reader then has to re-correlate by position. | Pass the reading as `Check`'s third argument: it is printed on the passing verdict too, as `TEST_RESULT: PASS: <what> - <details>`. Keep only failure-only text behind `ok ? QString() : ...`, which still prints the bare passing line. |
| A round trip reported as a negative or otherwise impossible number, or a pair count that does not match the issue count | Two lists were related by position - an issue list against an answer list, or a row list against a parallel `crl::time` vector indexed at `mark + i` - so one extra or missing element on either side paired a row with another row's time, and the reading was emitted as a `Note` that failed nothing. | Record both sides into one `Test::Probe` with `recordIssue`/`recordAnswer` and read them through `checkRoundTripSince(mark, key)`: it pairs by key, discards and names every answer not strictly later than its issue, and refuses as a FAIL carrying the tallies rather than reporting an interval it did not positively pair. Read a bare time through `timedRowsSince`, which carries each plain row's own time. |
| A capture or `captureWidget` stage times out and its details carry `render root paints no background of its own: ... - grab N...BoxLayerWidget... instead (unpainted 0/1000)` | The render root was the box, and that box had cleared `Qt::WA_OpaquePaintEvent`. `Ui::BoxContent`'s constructor sets that attribute, so a plain box paints its own background and is accepted - but `setNoContentMargin(true)`, which 53 product call sites use, clears it again, and then the box paints no background of its own and `PreparedWidgetCapture::prepare()` refuses every frame it is offered; the poll can only end in a timeout. | Resolve the root with `Test::PaintingLayerRoot(box)` and capture the `Ui::BoxLayerWidget` it answers, or use `Test::CaptureInLayerRoot(box, name)` for a frame cropped to the box (it composes `CaptureMappedRect`, so a box that maps outside its layer is still a named FAIL). The refusal is naming the right widget - do not widen it. |
| A menu capture times out with `showingContent=0` in its last state, or the popup vanishes mid-stage | The readiness waited on the inner `Ui::Menu`'s visibility. `Ui::PopupMenu::startShowAnimation()` calls `hideChildren()`, and the children come back only from the final `paintEvent`'s `Ui::PostponeCall` - a side effect no `-testagent` run is guaranteed to reach - so the wait can last until the popup dies of a focus-out. A one-shot grab in the turn that opened the menu is the opposite failure: it lands on a show-animation frame with nothing drawn. | Capture with `Test::CapturePopupMenu`: its readiness carries content identity only - the menu exists, is visible, has non-empty geometry and carries actions - and the prepared capture's own blank-frame refusal decides when the frame is good. `showingContent` belongs in the details, never in the gate. Do not use `Test::PaintingLayerRoot` on a popup: it sets `Qt::WA_NoSystemBackground`, so the blank-root refusal never applies, and it is its own window. |
| A stage times out although its own details show every precondition met and the action already fired - for example `box=1 toggle=1 popups=0` after a click on a control in a freshly shown layer | One synthesized click on a control in a layer that is still settling intermittently does not land, and nothing observes that until the stage's 10-second timeout ends the whole scenario. The input helpers are not at fault: `Test::Click` and `Test::Drag` deliver press, release and a `QEvent::Leave` synchronously and drain every `Ui::PostponeCall` before returning. | Make the click self-correcting in the scenario, not in the helper: read the effect, click the same `QPointer`-guarded widget once more if it did not appear, and `Note` which attempt worked so the flake rate stays visible. Establish first, from the production callback, that a repeat is provably a no-op - the details-menu toggle's first statement is `if (*menu) { return; }`. |
| A focus-routed affordance produces nothing at all — a submit that formed no request, a field that never took focus, `focused=0` / `isActiveWindow=0` — with no error and no event, especially on an unattended or locked console | The platform window is not active - which is what makes `QWidget::isActiveWindow()` false, because it falls back to `QPlatformWindow::isActive()` (`qwidget.cpp:6723-6725`) - so `QWidget::setFocus()` never promotes the target to `QApplication::focusWidget()`; clearing only the QPA focus window does not reproduce it where the OS window is still active. `Window::Controller::activate()` asks the window manager, which on a locked console does not comply. | Assert it with `Test::ForceWindowActive()` before the action. A one-shot activation expected to survive across polled turns is a forbidden technique, because the platform de-activates asynchronously between event-loop turns: re-assert it on every poll of the bounded wait, where the helper caps its note to about every tenth call and reports the attempt count at pass time. This is the one documented exception to the `until` purity rule above — it mutates only which window Qt considers focused, is idempotent, and encodes no expected product result. Confirm the instrument with the `test_activation.h` self-test. |
| A stage times out while the button it aims at is plainly on screen, and `FindAll<Ui::RoundButton>(<box content>)` reports zero buttons | `Ui::BoxLayerWidget::addButton` re-parents the button onto the shell (`box_layer_widget.cpp:327-331`), so a footer button is a direct child of the shell and not a descendant of the published box content. | Click it with `Test::ClickBoxButton(box, label)`, which roots the search at `Test::PaintingLayerRoot(box)`: gate on `Test::BoxButtonReady` in `until` and click in `then`, and pair it with the self-correcting repeat of the settling-layer row above. A label no shell button carries is a named refusal listing every label seen, never a silent no-op. |
| A capture of a visible widget is blank or refused at perfectly sane geometry — `target grab still looks blank` or `render root paints no background of its own` — and the target is a toast, a tooltip or another fade-in wrapper | `Ui::Toast::internal::Widget::paintEvent` (`toast_widget.cpp:585-600`) draws its whole frame into a transparent proxy at the fade-in opacity, so the grab holds the harness base and nothing else. | Capture with `Test::CaptureViaWindow(widget, name)`, which grabs the widget's window cropped to the widget's rect mapped into it. A blank mid-fade frame is a `Note` and never a FAIL, because the decisive oracle is the joined `accessibilityName` of the toast's `Ui::FlatLabel`s and the capture is corroboration. Second fact for the same target: under the harness's drained loop a toast's `hideAnimated()` animation is starved, so toasts persist far past `Ui::Toast::kDefaultDuration` (1500 ms) — a harness-environment effect, never a product defect to “fix”. |
| `focused=0` for a field that is plainly focused, often beside `focusWindowSet=1 focusWidget=QTextEdit` | `Ui::InputField` declares its own non-virtual `bool hasFocus() const` returning `_inner->hasFocus()` (`input_field.cpp:4276`), and `QWidget::hasFocus()` is non-virtual too, so a read through the `QWidget*` a generic finder returns answers for the wrapper, not for the inner editor that holds the focus. | `dynamic_cast<Ui::InputField*>` the resolved widget and call its own `hasFocus()`, or test `QApplication::focusWidget()` for descendancy of the field. `Ui::PasswordInput` is a `QLineEdit` subclass with no wrapper, so a direct read there is already right. |
| A passcode / cloud-password box accepted Enter and produced no request: no SRP computed, no API call, no error | `PasscodeBox::submit()` (`passcode_box.cpp:350-383`) is five per-field `hasFocus()` branches and nothing else; in a Qt-inactive window every branch reads false, Enter falls through and `save()` never runs. | Submit through the box's own shell button with `Test::ClickBoxButton` — `lng_passcode_submit` when a caller set `customSubmitButton`, otherwise `lng_settings_save` or `lng_passcode_remove_button` — whose handler calls `save()` with no focus dependency; assert activation first, per the activation row above. Two of those branches are irreversible on a live account: `lng_passcode_remove_button` is the turning-off submit that disables the cloud password (`passcode_box.cpp:263-269`), and a `customSubmitButton` is `tr::lng_theme_delete()` on the account self-destruction flow (`self_destruction_box.cpp:58`). Place that click last in the scenario, per step 3 of Building one reliable packed scenario above. `PressKey(Qt::Key_Enter)` is a last resort only, and log which path submitted (`submittedVia=`) so a fallback stays visible. |
| Text was typed and nothing was inserted: the field's own text stays empty and `changes()` never fires, even with `focused=1 focusWidget=QTextEdit` | The events were aimed at the `Ui::InputField` wrapper, which does not override `keyPressEvent` (`input_field.h:433-437`), and Qt propagates an ignored key event up the parent chain and never down into a child - the `QEvent::KeyPress` case of `QApplication::notify` re-delivers it to `w->parentWidget()` until it is accepted or a window is reached - so the keys reach the wrapper's ancestors right up to the primary window and never the inner `QTextEdit` that owns the text. | Type into `dynamic_cast<Ui::InputField*>(widget)->rawTextEdit()`, fall back to `Test::CommitText` on that same editor when the text stays empty, and prove insertion by reading the field's own `getLastText()` afterwards. |
| A text oracle fails on a date, time or number whose two sides read identically in the details | The formatter's U+202F or U+00A0 against the read-back's U+0020. `Ui::Text::String`'s block parser maps every space-class character except U+00A0 to U+0020 (`text_block_parser.cpp`, `replaceWithSpace`), so a `Ui::FlatLabel` fed `langDateTime()`'s narrow no-break space before AM/PM reads back a plain space through `accessibilityName()`, while a U+00A0 survives the parse. | Compare through `Test::CheckTextReads` / `Test::NormalizeSpaces` (`test_text_reads.h`): they map U+202F and U+00A0 to U+0020 on both sides and print both raw strings' distinct `U+XXXX` whitespace tokens on the passing verdict as well as the failing one. Never loosen digits, month or AM/PM. |
| A check is missing from the log with no PASS and no FAIL | The stage's gate read false and it ran as a hand-rolled no-op - `run` and `then` skipped, `until` answered true - so it wrote nothing at all. Distinct from the stages a timed-out stage or the watchdog skips (see “Scenario teardown before quit” above), which leave the timeout's own FAIL behind them. | Give the stage a `skipReason` (`test_runner.h`): a non-empty return emits `TEST_RESULT: N/A: <stage> - <reason>` in the turn that begins it, counts in `SCENARIO_RESULT`'s skipped clause and in the `test-run` report's skipped list, and never waits or times out. Confirm the instrument with the `test_gated_stage.h` self-test. |

Classify a sound assertion against changed behavior as an implementation bug,
not a test flaw. Classify a wrong fixture, target, readiness model, event
route, capture owner, or oracle as a test flaw. Preserve everything a failed
run proved, repair all visible harness faults together, and rerun the packed
scenario rather than fragmenting it into isolated launches.

When a flaw's cause is the instrument idiom rather than this task's fixture,
repair it here as well as in the overlay: add the missing helper, tighten an
existing contract, or add its row to the table above, in the same run that
diagnosed it. A diagnosis that stays in one task's notes is rediscovered by
the next task, which is how the `Test::Probe` window row and the
`Test::DiscriminatingScan` row above cost four runs each before they were
written down.
