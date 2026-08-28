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
| `TypeText` | Key press/release per Unicode grapheme; surrogate pairs and joined emoji stay intact. |
| `CommitText` | One `QInputMethodEvent` commit when insertion, custom emoji, or IME semantics matter more than physical keys. |
| `PressKey` | Escape, Return, arrows, shortcuts, and other key behavior. Send to the widget that owns the event route, often the raw editor or top `BoxLayerWidget`, not a wrapper. |
| `Drag` | Left-button press, interpolated moves with `buttons()==LeftButton`, and release, then the same `QEvent::Leave` as `Click`. |
| `Wheel` | A real wheel event; `QPoint(0, -120)` is one conventional downward step. |
| `Settle` | Wrap programmatic mutations such as `InputField::setText`; drains postponed fixups after the action. |

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
| `test_runner.h` | Stages, bounded waits, exact-widget actions, prepared capture/inspection, watchdog (`TDESKTOP_TEST_WATCHDOG` in seconds) and termination. |
| `test_log.h` | Absolute flushed logs, steps, notes, checks whose `details` are printed on the passing verdict as well as the failing one, tolerances, geometry, completion markers. |
| `test_probe.h` | Append-only observation records read only through a declared window, each carrying the time it was recorded; keyed issue/answer rows correlated into one round trip by key rather than by list position, refusing every reading it cannot positively pair; and scans that must match a control before a zero counts as absence. |
| `test_widgets.h` | Safe typed discovery, live object/action publication, input, and postponed-call settlement. |
| `test_capture.h` | In-process grabs, paint-root validation, mapped rects, blank detection, crops, zoom, contact sheets. |
| `test_hover.h` | The input helpers' own self-test: a clicked-then-dragged and a never-touched `Ui::RoundButton` measured against the two fills their style names, proving a completed synthetic click and drag leave no hover. |
| `test_ink.h` | Derived paint bands, colour separation, ink scans, counts, and contrast reports. |
| `test_style.h` | Wait for palette/style samples to stabilize and assert a recorded baseline still holds. |
| `test_panel.h` | Distinguish a live `Ui::SeparatePanel` from its faded/squeezed show-animation cache. |
| `test_messages.h` | Lifetime-owned watcher for a matching newly sent server message. |
| `test_history_fixtures.h` | Inject a caller-supplied service action into a real history as a regular or (negative-control) local item, with a caller-owned lifetime that removes it, and log the menu-gating predicates. |
| `test_custom_emoji.h` | Supply an always-ready `Ui::Text::CustomEmoji` that fills the large-emoji box with worst-case ink, handed out only for document ids the scenario itself registered. |
| `test_launch_fuse.h` | Declare and verify operating-system launches while refusing every real launch in test-agent mode. |
| `test_open_handoff.h` | Inspect and assert the document-open branch without handing anything to the OS. |
| `test_transfer.h` | Observe document save/failure transitions and assert duplicate or failed transfer behavior. |
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
| Blank or partial screenshot | Wrong paint owner, animation cache, or viewport clipping. | Use prepared capture, `PanelShowSettled`, the owning ancestor, or `CaptureMappedRect`. |
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

Classify a sound assertion against changed behavior as an implementation bug,
not a test flaw. Classify a wrong fixture, target, readiness model, event
route, capture owner, or oracle as a test flaw. Preserve everything a failed
run proved, repair all visible harness faults together, and rerun the packed
scenario rather than fragmenting it into isolated launches.

When a flaw's cause is the instrument idiom rather than this task's fixture,
repair it here as well as in the overlay: add the missing helper, tighten an
existing contract, or add its row to the table above, in the same run that
diagnosed it. A diagnosis that stays in one task's notes is rediscovered by
the next task, which is how the two rows above cost four runs each before
they were written down.
