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
`Test::BoxShellButtons`, `Test::WindowMappedCapture`,
`Test::PaintingLayerRootResult` and `Test::ToastSubtreeReading` hold
`QPointer<QWidget>`, so a reading kept across a stage boundary answers
`matched()` / `resolved()` false once its subject is destroyed and still
prints its counts, labels, identity and refusal; format a
`WidgetDescription` of the pointer while the reading is still matched,
because there is nothing to format afterwards.

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
| `Wheel` | A real wheel event at the named widget's local point (`QPoint(0, -120)` is one conventional downward step). A stack-built `QWheelEvent` is not spontaneous, so Qt 6 does not propagate it; the helper re-sends up `parentWidget()` until a widget handles and accepts — the scroll area's viewport, never the `QAbstractScrollArea` itself — honouring `isWindow()` and `Qt::WA_NoMousePropagation`. Returns `WheelDelivery`; a miss is a named `refusal`, not a silent no-op. Confirm with `test_wheel.h`. |
| `Settle` | Wrap programmatic mutations such as `InputField::setText`; drains postponed fixups after the action. |
| `ForceWindowActive` | Inject window activation through the QPA seam before any focus-routed action, and re-assert it on every poll of a bounded wait. On a locked console the window is never active in Qt's sense, so `setFocus()` never reaches `QApplication::focusWidget()` and every `hasFocus()`/`isActiveWindow()` branch silently reads false. |
| `KeepMainWindowNotMarkingRead` | Minimize the primary window and poll until `MainWindow::markingAsRead()` is false with the window minimized and not hidden, or until `kNotMarkingReadBound` returns a named refusal. Call it again after `Window::Controller::activate()` or any `showHistory` that is not `anim::activation::background`, which clear the minimized bit. A locked or still-unexposed console cannot make the control true and must not be reported as success. Confirm with `test_marking_read.h`. |
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
| Complete mapped target visible in painted owner and scroll viewport before that capture | `ReadMappedTarget` / `MappedTargetReady` / `PreparedWidgetCapture::prepare(owner, origin, rect)` / `CaptureMappedTarget` |
| Full box, layer owner, animation, or asynchronously populated surface | `Runner::captureWidget` |
| Whole box inside a layer (title + footer shell) | `PaintingLayerRoot` + `Runner::captureWidget`, or `CaptureBoxLayer` |
| Box content cropped inside a layer | `CaptureInLayerRoot` |
| Open `Ui::PopupMenu` | `CapturePopupMenu` |
| Toast whose frame must not compose the product surface painted beneath it | `CaptureToastSubtree` |
| Other widget that paints no opaque background of its own (a fade-in wrapper, a tooltip) | `CaptureViaWindow` |
| Exact accepted frame plus numeric/raster assertions | `Runner::captureAndInspect` |
| Small target comparison | `Crop`, `Zoom`, `ContactSheet` |
| Foreground colour, contrast, or painted-band measurement | `MeasurePaintedInk` and the helpers in `test_ink.h` |

`CaptureInLayerRoot` crops to the box content's own rect. That is not the
whole box: `Ui::BoxLayerWidget::setTitle` creates `_title` parented to the
shell, and `addButton` re-parents each footer button onto the shell, so
both live outside `box->rect()`. `CaptureBoxLayer` grabs that shell.

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

For a capture whose subject is the currently visible mapping — a card, row,
or child rectangle that must appear whole in the painted owner — the caller
supplies that origin widget, the painted owner, and the exact origin-local
rectangle to `ReadMappedTarget` / `MappedTargetReady`. Readiness is
whole-rectangle containment in the owner **and** the relevant viewport
(`Ui::ElasticScroll` itself, because its `viewport()` is Dummy and returns
the inner content; `QAbstractScrollArea::viewport()` otherwise). It never
intersects, clips, or reframes the requested rect. Origin-local containment
alone is not visibility: a HistoryInner-local card rect can sit fully
inside the inner widget while a negative scroll offset leaves part of it
outside HistoryWidget. `CaptureMappedTarget` refuses that unready reading
and then composes `CaptureMappedRect`, which still independently refuses a
rect that is not fully inside the grabbed widget. A nonpainting owner whose
geometry fits is still not capture-ready (`PreparedWidgetCapture::prepare`).
`PaintingLayerRoot` resolves boxes inside layers specifically; it is not a
history/list paint-owner resolver. Callers still own semantic navigation
and exact item identity.

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
| `test_runner.h` | Stages, bounded waits, exact-widget actions, prepared capture/inspection, first-class gated skips (`skipReason`), `onFinish` release hook (and its finish-release self-test), watchdog (`TDESKTOP_TEST_WATCHDOG` in seconds) and termination. |
| `test_gated_stage.h` | The first-class gated skip's own self-test: a stage whose `skipReason` returns a reason, writing one `TEST_RESULT: N/A:` row and skipping `run`, `until` and `then` without waiting - its never-ready `until` under a one-second timeout is the falsifier - beside a stage whose gate returns an empty string and runs normally in the tick that begins it. |
| `test_log.h` | Absolute flushed logs, steps, notes, checks whose `details` are printed on the passing verdict as well as the failing one, tolerances, geometry, completion markers, N/A rows for stages that did not apply, and their count. One `LogRaw` call always writes exactly one physical line, whatever it is handed: every character Python's `str.splitlines()` breaks on - U+000A, U+000B, U+000C, U+000D, U+001C, U+001D, U+001E, U+0085, U+2028, U+2029, and so a CRLF pair as its two code points - is written as a visible `\uXXXX` escape, so a record carrying a break stays one row the external readers' line grammar reads whole and cannot mistake for a completion, while text with no separator is passed through byte for byte and the escape adds no trailing whitespace. |
| `test_log_lines.h` | The one-physical-line guarantee's own self-test: one `LogRaw`-family call driven with each of the eleven separator forms in turn, with one payload mixing them all, with a trailing separator and with a separator-only payload, each read back out of `test_log.txt` by byte offset and asserted to have added exactly one physical line under an independent transcription of `str.splitlines()` - beside a separator-free control that must read one line under any writer, and a last stage whose payload's middle line would be byte-equal to `TEST_COMPLETE` and which asserts no produced line is. |
| `test_text_reads.h` | Space-class-normalizing text comparison: `Test::NormalizeSpaces` maps U+00A0 and U+202F to U+0020 and changes nothing else, `Test::CheckTextReads` compares a read-back against an expectation through it and prints both raw strings and their whitespace code points on either verdict; with its own self-test, in which a real `Ui::FlatLabel`'s narrow-no-break-space read-back is accepted beside two deliberate FAIL negative controls - a different minute and a different day - that keep the check from becoming permissive. |
| `test_lang_pack.h` | Install synthetic key/value overrides into the RUNNING language pack and take them back down symmetrically: `Test::InstallLangPack` freezes the pack identity, the `serialize()` snapshot and one `LangFrozenValue` per key before applying one `Instance::applyDifference`, whose two `Expects` it holds by construction, and `LangPackFixture::remove()` restores through `switchToId` + `fillFromSerialized` and then one EMPTY difference purely to deliver `Lang::Updated()`, so a key that was default before the install comes back and an already-painted reactive label re-reads with no interaction. `frozen(key)` is the only value-returning read, so an expectation taken after the install — the one that passes vacuously — is not expressible through it; `checkRestored` compares live against frozen on both the value and the non-default axis and prints every reading on the passing verdict. `checkInstalled` certifies the caller's requested override: with no placeholder the expectation is that text and the reading is `getValue`; with a placeholder the expectation is the `QString` `Instance::ParseStrings` returns for that requested text — the encoding `applyValue` stores — and the reading is still `getValue`, so `getNonDefaultValue` stays only the non-empty axis. A placeholder `ParseStrings` omits (unknown, not accepted by that key, repeated, or malformed) is not certified: `checkInstalled` FAILs by name and the verdict quotes the raw non-default value and whether `getValue` stayed at the frozen reading. A `#custom` pack, an empty override list, a key `GetKeyIndex` answers `kKeysCount` for (a plural phrase's base name included), an empty override value, a `frozen()` miss, an oracle on a fixture that never installed and an out-of-order `remove()` are seven named refusals rather than silences. With its own arm-selectable self-test, whose two `LangRestoreFault` arms deliberately leave one key installed or suppress the notification, and whose placeholder arm on `lng_dlg_search_from` shows a `{user}` install PASS and a deliberate `{amount}` install FAIL. |
| `test_probe.h` | Append-only observation records read only through a declared window, each carrying the time it was recorded; keyed issue/answer rows correlated into one round trip by key rather than by list position, refusing every reading it cannot positively pair; and scans that must match a control before a zero counts as absence. |
| `test_widgets.h` | Safe typed discovery, live object/action publication, input, postponed-call settlement, QPA-injected window activation, and wheel delivery along Qt's parent ladder with a named `WheelDelivery` refusal. |
| `test_wheel.h` | The wheel helper's own self-test: a covering `Ui::AbstractButton` inside a `Ui::ScrollArea` whose subject wheel moves the area and names the viewport, beside the same wheel aimed at the viewport (today's working target), a wheel aimed at the `QAbstractScrollArea` itself that is not certified delivered, and a parentless-window button that refuses by name. |
| `test_capture.h` | In-process grabs, paint-root validation, mapped rects, complete-target mapped readiness (`ReadMappedTarget` / `MappedTargetReady` / `CaptureMappedTarget` / `PreparedWidgetCapture::prepare(owner, origin, rect)`), blank detection, painting-layer-root resolution for boxes inside a layer, `CaptureBoxLayer` for the whole `Ui::BoxLayerWidget` (title + `addButton` footer row, which `setTitle` / `addButton` parent onto the shell) versus `CaptureInLayerRoot` for a frame cropped to the box content, crops, zoom, contact sheets, window-mapped capture for widgets that paint no opaque background of their own; a **toast** goes to `test_toast_capture.h` instead, whose frame cannot compose the surface beneath it. |
| `test_mapped_target.h` | Complete-target mapped readiness measuring itself: a painted owner taller than its `Ui::ElasticScroll`, a child rectangle that fits the owner while remaining partly outside the scroll, then the same origin/owner/local rect capturable after ordinary `scrollToY`, plus a sibling `Ui::ScrollArea` whose clipper is `viewport()`, overlap-only / empty / hidden refusals, a nonpainting root that geometry would otherwise accept, and `PaintingLayerRoot` unresolved on the fixture. |
| `test_marking_read.h` | Keep the primary window from marking messages read on an unlocked console. `ReadMainWindowMarking` / `MarkingReadDetails` carry `markingAsRead`, `isActive`, `isMinimized`, `isHidden`, `exposed` and `screenLocked` plus the `WindowActivation` reading, and a refusal. `KeepMainWindowNotMarkingRead` sets `Qt::WindowMinimized` (not `Controller::minimize()`, which tray-hides in `WorkMode::TrayOnly`, and not `QWidget::hide()`, which makes `CaptureWidget` refuse the window) and refuses by name if that lever is not holding within `kNotMarkingReadBound`. A null controller refuses without searching for another window. The self-test covers a true control, the helper, `Controller::activate()` undoing the lever, a second call, a non-blank `PreparedWidgetCapture` of a main-window widget while the lever holds, the null refusal, and showing the window again. A locked or still-unexposed host skips the deciding half instead of passing it. |
| `test_layer_root.h` | The painting-layer-root resolver's own self-test: a plain `Ui::GenericBox` accepted as its own render root beside one that cleared `Qt::WA_OpaquePaintEvent`, refused and then captured through its `Ui::BoxLayerWidget`; `CaptureBoxLayer` saves the whole shell beside `CaptureInLayerRoot`'s content crop, and `MisframedDetails` is quoted on a rect outside the layer. |
| `test_via_window.h` | The window-mapped capture's own self-test: a real `Ui::Toast` whose bare prepared grab is refused in the turn it is created, beside the window-cropped frame of the same rect that the harness accepts, and a flat fixture region proving a blank frame is a `Note` and never a FAIL; a sixth stage builds an offscreen `Qt::WA_DontShowOnScreen` top level, reads it, destroys it inside the same `.run`, and shows the retained reading answering `resolved()` false with its identity, mapped rect and refusal intact. |
| `test_toast_capture.h` | The toast capture whose frame cannot compose the product surface beneath it: the live-toast walk over the top level widgets (`Ui::Toast::internal::Manager` keeps `_toastByWidget` private and enumerates nothing), a grab rooted at the toast subtree that `Ui::GrabWidgetToImage` fills with `st::windowBg` wherever the toast does not cover, a settled-show readiness term measured against `st::toastBg`'s own blend over that base because `_shownLevel` is private, named refusals for a root that is not the live toast and for a rect outside the toast's geometry that quote both rects and never reframe, the joined-label text oracle read through `Test::CheckTextReads`, and its own self-test over a synthetic sentinel surface it paints, which counts sentinel pixels in both saved frames and then hides that surface to show no pixel of the subtree frame depending on it, and which drives both documented ambiguity branches of the resolver — a second live toast shown and taken down inside one stage, and the empty walk its own teardown arranges — showing `FindLiveToast()` answering `nullptr` for each, and which measures a default-duration toast live on `ToastSubtreeReady` inside its lifetime and gone from the walk after it, quoting the live count, the texts and the elapsed time. |
| `test_hover.h` | The input helpers' own self-test: a clicked-then-dragged and a never-touched `Ui::RoundButton` measured against the two fills their style names, proving a completed synthetic click and drag leave no hover. |
| `test_activation.h` | The window-activation helper's own self-test: the application deliberately de-activated and re-activated through the same QPA seam inside one stage, the wrapper-versus-inner `hasFocus()` contrast and the wrapper-versus-`rawTextEdit()` typing contrast measured on one real `Ui::InputField`. |
| `test_box_button.h` | Click a box's shell footer button by label, rooted at `Test::PaintingLayerRoot`, with its own self-test: a content-rooted `Ui::RoundButton` search that reaches none of the footer buttons beside the rooted search that finds and clicks one, a named refusal for a label no shell button carries, a disabled or hidden footer readable as `present`+`disabled`/`hidden` with `matched()` still false (`BoxButtonDisabled` / `BoxButtonHidden` beside `BoxButtonReady`), `box_button_shell` saved through `CaptureBoxLayer` so the frame contains the footer row, and a same-launch hidden-then-ready shell-button reading while the layer show animation runs. `ClickBoxButton` still Fails on an unusable button. |
| `test_ink.h` | Derived paint bands, colour separation, ink scans, counts, and contrast reports. `DeriveBand` names the case where the requested fill is the image's background outside the candidate, distinct from no-rows; `MeasurePaintedInk` carries that reason in `report`, and can no longer answer with an empty `report` on any path. A scan that cannot classify a frame refuses by name as well: `InkScanState` answers `outside-image`, `no-rows-in-band`, `candidates-collide`, `background-collinear`, `no-ink` or `classified` beside a `reason` that is never empty on a returned reading, while `ok` keeps its one meaning — this band was scanned — so only the two states that looked at no pixel are false and every frame-level number on the other five is a real measurement. `background-collinear` is a candidate within `kInkMargin` of the segment from the scan's measured modal background to another candidate: the reading is not `classified`, and `countAt` refuses every count as `count=none` instead of returning the zero that candidate can never earn. It loses to `candidates-collide` and to `no-ink`, which keep their texts. `candidates` and `counts` are filled before either refusal returns, so the two are always the same length, and one candidate's count is read through `InkScan::countAt(i)`, which names the candidate and refuses an index the reading does not have, a reading that classified nothing and a candidate pair that does not separate, instead of indexing past the end of a vector the scan never filled; `InkCountDetails` prints that refusal as `count=none`, never as a zero. `FormatInkScan` starts with `FormatInkReport`'s unchanged text and then names the candidate colours, their counts, `CollisionDump`'s collision and all five thresholds on the **passing** verdict as well as the failing one. Own self-test over synthetic images. |
| `test_ink.h` (`ReadChromaticRaster`) | Where a coloured glyph clusters in a captured band. Given a pen and a fill to exclude, it keeps pixels whose channel spread clears `kChromaticFloor` and that match neither excluded colour within `kSameTolerance`, slides a square window across their x range, and reports the densest window's box, matched count, total chromatic count, density and the mean colour of the pixels it matched — never a colour the caller passed in. `no-chromatic` (the band was scanned and no pixel cleared the floor) and `below-density` (chromatic pixels exist but no window reaches `kChromaticDensityFloor`) are named refusals; `readBox`, `readMean` and `readDensity` refuse on both, so an empty box is never a measurement. A band that misses the image is `raster-outside-band`, distinct from both. `FormatChromaticRaster` prints the box, both counts, the density and the measured mean on a found reading and names the refusal on the others. Own self-test over synthetic images, registered from `AppendDeriveBandSelfTest`. |
| `test_ink.h` (`ReadGlyphCore`) | Which pen painted a text run, read from its solid glyph cores rather than from nearest-candidate attribution. The band's background is its modal colour. Ink is a pixel at least `kInkDelta` from that background. A solid core is an ink pixel within `kGlyphCoreTolerance` (8) of the per-channel extreme away from the background, which is full coverage rather than a fringe or a partial blend. The reported modal colour is the mode of those cores, with its share and the core count; a named pen is only a count of solid cores within the same tolerance and is never copied into the modal. `no-paint` is a band with no ink. `no-solid-core` is a band that has ink but no pixel within the tolerance of the extreme, so a fringe colour is not returned as the pen. `readModal` and `solidAt` refuse both, and `core-outside-band` is a band that misses the image. Distinct from `background-collinear`, which is an attribution refusal, and from `ReadChromaticRaster`, which is a mark-density reading and not a pen reading. Own self-test over synthetic images, registered from `AppendDeriveBandSelfTest`. |
| `test_style.h` | Wait for palette/style samples to stabilize and assert a recorded baseline still holds. |
| `test_panel.h` | Distinguish a live `Ui::SeparatePanel` from its faded/squeezed show-animation cache, and answer which top-level `Ui::SeparatePanel`s exist. `Test::WalkPanels(&scan, query)` is the one walk eight disposable overlays each wrote by hand: it enumerates them with a caller-supplied `exclude` of panels the caller already holds — never a type — and a caller-chosen `Test::PanelLiveness`, where `Settled` is decided by this module's own `ReadPanelShowState` and not by `isVisible()`, so a panel still painting its show cache is not counted as one that opened, while `Shown` is kept beside it as that reading's control. A mark is simply a previous answer: `Test::PanelList` is `std::vector<QPointer<QWidget>>`, so it keeps nothing alive and a panel at a reused address cannot read back as one the mark held, and `query.before` — or `Test::PanelsAdded`, the same implementation — reads the difference back as pointers and never as a count. A walk that reached no `Ui::SeparatePanel` of any kind refuses by name instead of answering a zero, certified through `Test::DiscriminatingScan` whose control is every panel the walk reached, so "panels reached, none settled" is a certified zero while "no panel reached" is not; and `Test::PickPanel` refuses an ambiguous identity ask, naming every candidate it saw, in three refusal sentences no two of which read alike. `PanelWalkText` / `PanelListText` are the pure, pollable readings and `PANEL_WALK` the one reporting row. Identifying a panel from its labels is `FindAll<Ui::FlatLabel>` + `CheckTextReads` composed at the call site; no label-join idiom is promoted here. Own self-test over a synthetic top-level panel it creates, shows and takes down itself. |
| `test_menu.h` | Deterministic `Ui::PopupMenu` capture whose readiness is content identity only, its `showingContent` reading, and its own self-test. |
| `test_messages.h` | Lifetime-owned watcher for a matching newly sent server message. |
| `test_history_fixtures.h` | Inject a caller-supplied service action into a real history as a regular or (negative-control) local item, with a caller-owned lifetime that removes it, and log the menu-gating predicates. |
| `test_custom_emoji.h` | Supply an always-ready `Ui::Text::CustomEmoji` that fills the large-emoji box with worst-case ink, handed out only for document ids the scenario itself registered. |
| `test_launch_fuse.h` | Declare and verify operating-system launches while refusing every real launch in test-agent mode. |
| `test_open_handoff.h` | Inspect and assert the document-open branch without handing anything to the OS. |
| `test_transfer.h` | Observe document save/failure transitions and assert duplicate or failed transfer behavior. |
| `test_rpc_retry.h` | The permanent MTP resend seam: `Test::RecordRpcRetry` records one `rpc retry code=<code> type=<type> request=<constructor>` row for every code-500 or negative-code answer the transport auto-resends without calling the request's fail handler, read through the `Test::RpcRetryProbe()` accessor; with its own self-test for the recorded 500, the non-500 that reaches `.fail()` instead, and the answer for a request id this process never sent. Its synthesized `rpc_error` answers now go through `test_rpc_fixture.h` rather than a hand-rolled `processCallback`. |
| `test_rpc_fixture.h` | Type-safe controlled RPC replies for a registered request. Successful answers take the generated request's boxed `ResponseType` (`tl::boxed`), which writes the constructor word; a bare factory such as `MTP_messages_messages(...)` omits that word and must not be `.write()`'d into the reply. `DiagnoseControlledRpcResult` requires a complete decode with no leftover primes (truncation and trailing words included) **before** `MTP::Instance::processCallback`. `DeliverControlledRpcSuccess` / `DeliverControlledRpcPrepared` also require `hasCallback` and refuse delivery on a diagnosis so the parser stays registered. `DeliverControlledRpcError` is the explicit boxed `rpc_error` route (registered or not). `DeliverControlledRpcStale` is the explicit canceled/unknown-id success route: it still calls `processCallback` after a valid boxed body. Own self-test over `messages.getMessages` (empty and nonempty boxed `messages.Messages`, bare/truncated/trailing/incompatible rejects, code-400 error, cancel-then-stale). |
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
release of everything that `State` holds: every `rpl::lifetime`, every
watcher, every raw cross-stage pointer, and every `base::Timer`. Register
that release with `Runner::onFinish`. `finish()` runs those callbacks on
every path that reaches it — stage timeout, the scenario watchdog,
skip-to-end, and normal completion — exactly once, after the ticker and
watchdog are cancelled, before the `kFinishDrainDelay` fuse drain, and
before `Complete()` / `Core::Quit()`. A callback registered after `finish()`
has already run executes immediately and is never silently dropped.

A final teardown stage is still useful for work that must happen while the
session still exists and the runner is still stepping, but it is not the
release point. The stage is not guaranteed to run: a stage that times out,
and the scenario watchdog, make `Runner` finish immediately and skip every
stage after it, while a stage whose assertions `FAIL` does not — that run
still reaches its teardown. `onFinish` still runs in those abort paths, and
it also runs when a teardown stage already ran, so the callback must be safe
to call after teardown.

Skipping the hook costs a whole run. A leaked `State` whose two
`rpl::lifetime`s were never released kept observers subscribed to
`Storage::Facade`'s and `Data::Session`'s streams while `~Main::Session` tore
those streams and their items down. The run reached `TEST_COMPLETE` after a
clean sweep and then died with `Caught signal 11 (SIGSEGV)`, no assertion line
anywhere in the run's logs, and a 0-byte minidump because the runner had to
kill the process. With the release registered, three consecutive runs exited
0, wrote no crash report and left no minidump. After a timeout the same death
can follow `TEST_COMPLETE`, so read it as this signature rather than as a
second product fault, and keep the leaked `State`'s session-observing
subscriptions no wider than the stages that need them.

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
| A controlled success fixture never reaches `.done()`, or a later valid answer looks like a stale/missing callback, while the application Debug log shows `RESPONSE_PARSE_FAILED` / `Response parse failed` | A bare TL constructor factory or an incomplete buffer was written into `MTP::Response::reply` and handed to `processCallback`. `MakeDoneHandler` calls `senderRequestHandled` before `Result::read`, returns false on a missing constructor word or truncated body, and `processCallback` then unregisters the request, so the fixture mistake is consumed as a local parse failure. | Deliver through `test_rpc_fixture.h`: typed success takes the boxed `Request::ResponseType`, `DiagnoseControlledRpcResult` refuses empty/truncated/trailing/wrong-constructor buffers **before** `processCallback`, and explicit error/stale routes are separate. Confirm with `AppendControlledRpcSelfTest`. |
| Blank or partial screenshot | Wrong paint owner, animation cache, or viewport clipping. | Use prepared capture, `PanelShowSettled`, the owning ancestor, or `CaptureMappedRect`; for a box inside a layer, `Test::PaintingLayerRoot`; for a `Ui::PopupMenu`, `Test::CapturePopupMenu`. Gate a visible mapped card/row on `Test::MappedTargetReady` / `prepare(owner, origin, rect)` so origin-local containment cannot pass a target that is still clipped. |
| A mapped capture is refused after readiness passed, or a HistoryInner-local (or other origin-local) rectangle was treated as visible while part of it sat outside the painted owner or scroll | Readiness checked the origin widget's own rect, which still contains a child that a negative scroll offset has moved outside `HistoryWidget` / `Ui::ElasticScroll`. `ElasticScroll::viewport()` returns the inner content, so using it as the clipper recreates the same pass. `PaintingLayerRoot` does not name a chat paint owner. | Use `Test::ReadMappedTarget(owner, origin, localRect)`: whole-rect containment in the painted owner and the relevant clipper (`ElasticScroll` itself, or `QAbstractScrollArea::viewport()`). Do not intersect or reframe. `CaptureMappedTarget` refuses an unready reading; `CaptureMappedRect` still refuses a rect that is not fully inside the grabbed widget. Confirm with `test_mapped_target.h`. |
| Old palette/colour sampled | Style had not settled or moved between reference and target. | Use `StyleSettled` and `StyleBaseline`. |
| A clicked button's measured fill matches no style constant, or `DeriveBand` returns `ok=0` with no rows for a widget plainly on screen | The reading was taken while the widget was still hovered by an earlier synthetic click, so it painted `textBgOver` where the check named `textBg`; before the input helpers delivered a leave this latched for the whole process. | Take the reading through helpers that leave the target pointerless (`Click`/`Drag` deliver a `QEvent::Leave`), and when a hovered reading is what is wanted, set the hover deliberately and name the fill the state actually implies. Confirm the instrument with the `test_hover.h` self-test. |
| Every in-flight band reading reports `derivedOk=0 inkPixels=0` on a footer button while a restored or control band still measures | `st::defaultBoxButton` derives from `defaultLightButton`, whose `textBg` is `lightButtonBg`, and `lightButtonBg: windowBg` (`ui/colors.palette`) — the box's own background. `DeriveBand`'s premise is a row whose modal is the requested fill, so on that style the fill cannot be separated from the image outside the candidate. The old answer was the same empty `ok=0` as the hovered-fill row above, or as "no row of the recovered box has the pill fill as its own background". | Read `DeriveBand.reason`: when the fill is the surroundings it now says `the requested fill is the image's background outside the candidate, so no band can be derived`, distinct from the no-rows refusal. `MeasurePaintedInk.report` carries the same reason instead of a bare zero. Confirm with `AppendDeriveBandSelfTest`. Do not repair this as latched hover. |
| Emoji split or custom entity absent | UTF-16 code units or the wrong editor event route were synthesized. | Use grapheme-safe `TypeText` or one `CommitText` on the raw editor. |
| Drag/wheel/cancel has no effect | Wrapper received an event owned by a child, presentation, or viewport. | For drag and keys, target the real event owner. For a wheel, `Test::Wheel` now climbs to the consuming ancestor (the viewport, not the `QAbstractScrollArea`); if it still does nothing, read `WheelDelivery.refusal` instead of retargeting by hand. Confirm a wheel with `test_wheel.h`. |
| A wheel-delta oracle reads identical values before and after, on a widget plainly inside a scroll area | `Test::Wheel` sent a non-spontaneous `QWheelEvent` that Qt 6 does not propagate; a covering button ignores it and the scroll area never sees it. Delivering to the `QAbstractScrollArea` itself is the neighbouring trap: `event()` returns false without ignoring, so an acceptance check certifies a no-op. | Use the repaired `Test::Wheel`, which replays Qt's parent ladder or refuses by name. Do not treat `isAccepted()` on a scroll area as success. Confirm with the `test_wheel.h` self-test. |
| Test reaches a real external action | Missing expectation/fuse or mock seam. | Declare the exact blocked launch, mock the transport/payment boundary, and assert zero real calls. |
| Pixel probe misses only on Retina or at 125/150% | Logical rect indexed into the device-pixel grab, or a 100% literal reused at another interface scale. | Multiply by the image `devicePixelRatio()` once at the sampling boundary; derive expectations from the live scaled tokens. |
| Geometry oracle fails on plausible-looking rects | Rects from different widgets compared without a shared origin. | Map both through one declared frame (`Ui::MapFrom`, `mapToGlobal`) and log the mapped values in the failure details. |
| Process dies after `TEST_COMPLETE`, with no assertion line and often a 0-byte dump | Overlay teardown, not the product: a leaked scenario `State` still holds `rpl` subscriptions to session-owned streams while `~Main::Session` destroys them. | Destroy the scenario's lifetimes, release its watchers and null every raw cross-stage pointer from `Runner::onFinish` — a teardown stage is not enough, because a timed-out stage or the watchdog skips every stage after it. |
| `Telegram finished, result: 0` then `QObject::~QObject: Timers cannot be stopped from another thread` and a fresh dump | Overlay teardown, not the product: a scenario-owned `base::Timer` in static or leaked `State` was still armed when `QApplication` died. | Cancel the timer (and destroy lifetimes / null cross-stage pointers) from `Runner::onFinish`; do not rely on a teardown stage, which timeout and the watchdog skip. |
| Media reading frozen at position `0`, with the length equal to the document's declared duration | Undecodable fixture document — often a synthetic upload left on the shared test account — accepted on metadata alone; the app debug log shows `Streaming Error: Error in avformat_open_input`. | Select the fixture with the playability probe: play each candidate and accept only one whose position strictly advances, then reuse that document everywhere. |
| A premise fails against a row its own fixture had to create, or a check passes without ever reaching its subject | The oracle read the probe's whole history, or bracketed a slice by wall time, so rows from an earlier stage or a slow neighbouring surface answered it. | Record through `Test::Probe`, take `mark()` immediately before the action, and query only `...Since(mark)`; there is no whole-history accessor to fall back to. |
| A sweep reports a confident `found=0` that no repair ever changes | The enumeration structurally cannot reach the subject, so the zero was guaranteed before the run started and measures nothing. | Count through `Test::DiscriminatingScan` and feed it a known-present control; `report()` refuses to certify a zero the walk cannot tell from absence. |
| A green log that does not say what its checks were made against, so a passing run cannot be audited after the fact | The reading was handed to `Test::Check` as `details` back when `details` was written only on the failing branch, or worked around by folding it into `what` or by emitting a `Note` beside the check that a reader then has to re-correlate by position. | Pass the reading as `Check`'s third argument: it is printed on the passing verdict too, as `TEST_RESULT: PASS: <what> - <details>`. Keep only failure-only text behind `ok ? QString() : ...`, which still prints the bare passing line. |
| A round trip reported as a negative or otherwise impossible number, or a pair count that does not match the issue count | Two lists were related by position - an issue list against an answer list, or a row list against a parallel `crl::time` vector indexed at `mark + i` - so one extra or missing element on either side paired a row with another row's time, and the reading was emitted as a `Note` that failed nothing. | Record both sides into one `Test::Probe` with `recordIssue`/`recordAnswer` and read them through `checkRoundTripSince(mark, key)`: it pairs by key, discards and names every answer not strictly later than its issue, and refuses as a FAIL carrying the tallies rather than reporting an interval it did not positively pair. Read a bare time through `timedRowsSince`, which carries each plain row's own time. |
| A capture or `captureWidget` stage times out and its details carry `render root paints no background of its own: ... - grab N...BoxLayerWidget... instead (unpainted 0/1000)` | The render root was the box, and that box had cleared `Qt::WA_OpaquePaintEvent`. `Ui::BoxContent`'s constructor sets that attribute, so a plain box paints its own background and is accepted - but `setNoContentMargin(true)`, which 53 product call sites use, clears it again, and then the box paints no background of its own and `PreparedWidgetCapture::prepare()` refuses every frame it is offered; the poll can only end in a timeout. | Resolve the root with `Test::PaintingLayerRoot(box)` and capture the `Ui::BoxLayerWidget` it answers — `Test::CaptureBoxLayer(box, name)` is that whole-shell grab, including `_title` and the `addButton` footer row parented onto the shell — or use `Test::CaptureInLayerRoot(box, name)` for a frame cropped to the box content (it composes `CaptureMappedRect`, so a box that maps outside its layer is still a named FAIL). The refusal is naming the right widget - do not widen it. |
| A menu capture times out with `showingContent=0` in its last state, or the popup vanishes mid-stage | The readiness waited on the inner `Ui::Menu`'s visibility. `Ui::PopupMenu::startShowAnimation()` calls `hideChildren()`, and the children come back only from the final `paintEvent`'s `Ui::PostponeCall` - a side effect no `-testagent` run is guaranteed to reach - so the wait can last until the popup dies of a focus-out. A one-shot grab in the turn that opened the menu is the opposite failure: it lands on a show-animation frame with nothing drawn. | Capture with `Test::CapturePopupMenu`: its readiness carries content identity only - the menu exists, is visible, has non-empty geometry and carries actions - and the prepared capture's own blank-frame refusal decides when the frame is good. `showingContent` belongs in the details, never in the gate. Do not use `Test::PaintingLayerRoot` on a popup: it sets `Qt::WA_NoSystemBackground`, so the blank-root refusal never applies, and it is its own window. |
| A stage times out although its own details show every precondition met and the action already fired - for example `box=1 toggle=1 popups=0` after a click on a control in a freshly shown layer | One synthesized click on a control in a layer that is still settling intermittently does not land, and nothing observes that until the stage's 10-second timeout ends the whole scenario. The input helpers are not at fault: `Test::Click` and `Test::Drag` deliver press, release and a `QEvent::Leave` synchronously and drain every `Ui::PostponeCall` before returning. | Make the click self-correcting in the scenario, not in the helper: read the effect, click the same `QPointer`-guarded widget once more if it did not appear, and `Note` which attempt worked so the flake rate stays visible. Establish first, from the production callback, that a repeat is provably a no-op - the details-menu toggle's first statement is `if (*menu) { return; }`. |
| A focus-routed affordance produces nothing at all — a submit that formed no request, a field that never took focus, `focused=0` / `isActiveWindow=0` — with no error and no event, especially on an unattended or locked console | The platform window is not active - which is what makes `QWidget::isActiveWindow()` false, because it falls back to `QPlatformWindow::isActive()` (`qwidget.cpp:6723-6725`) - so `QWidget::setFocus()` never promotes the target to `QApplication::focusWidget()`; clearing only the QPA focus window does not reproduce it where the OS window is still active. `Window::Controller::activate()` asks the window manager, which on a locked console does not comply. | Assert it with `Test::ForceWindowActive()` before the action. A one-shot activation expected to survive across polled turns is a forbidden technique, because the platform de-activates asynchronously between event-loop turns: re-assert it on every poll of the bounded wait, where the helper caps its note to about every tenth call and reports the attempt count at pass time. This is the one documented exception to the `until` purity rule above — it mutates only which window Qt considers focused, is idempotent, and encodes no expected product result. Confirm the instrument with the `test_activation.h` self-test. The other side of this fact is an unlocked console whose active window marks messages read: that is the `KeepMainWindowNotMarkingRead` row below, not another use of `ForceWindowActive`. |
| An injected or fixture message is read the moment its chat is shown, an injected incoming item's notification never appears, or `HistoryInner::checkActivation` aborts on `IsServerMsgId`, next to `markingAsRead=1` with `screenLocked=0` | The agent-launched main window is active on an unlocked console, so `MainWindow::markingAsRead()` is true. Clearing the QPA focus window does not help while `auto-scroll-inactive-chat` is on, and `QWidget::hide()` makes `CaptureWidget` refuse the window. This is the other side of the activation row above: there the window is not active, here it is and it marks messages read. | Call `Test::KeepMainWindowNotMarkingRead`, and call it again after `Window::Controller::activate()` or any non-background `showHistory`. Confirm with `AppendMainWindowNotMarkingReadSelfTest`. |
| A stage times out while the button it aims at is plainly on screen, and `FindAll<Ui::RoundButton>(<box content>)` reports zero buttons | `Ui::BoxLayerWidget::addButton` re-parents the button onto the shell (`box_layer_widget.cpp:327-331`), so a footer button is a direct child of the shell and not a descendant of the published box content. | Click it with `Test::ClickBoxButton(box, label)`, which roots the search at `Test::PaintingLayerRoot(box)`: gate on `Test::BoxButtonReady` in `until` and click in `then`, and pair it with the self-correcting repeat of the settling-layer row above. A label no shell button carries is a named refusal listing every label seen, never a silent no-op. |
| `ReadBoxButtons` / `BoxButtonReady` / `ClickBoxButton` in the turn the box was resolved names every shell button `(hidden)`, or `BoxButtonReady` is false, while the box is plainly being shown and a later stage reads and clicks the same button | `LayerStackWidget::prepareForAnimation()` calls `layer->hide()` on the current layer (`ui/layers/layer_widget.cpp:732-754`) and `animationDone()` calls `layer->show()` again (`:756-782`). The stack's `BackgroundWidget` paints the animation from cached pixmaps while that hide holds, so every descendant of the `Ui::BoxLayerWidget` — the `_title` label, the content, and the re-parented footer buttons — answers `QWidget::isVisible()` false. `ReadBoxButtons` records that as `(hidden)` (a merely disabled footer included, because `!visible` wins over `isDisabled()`), and `BoxButtonReady` / `ClickBoxButton` are correct to refuse: a click then really would be the silent no-op those helpers exist to remove. `anim::type::instant` skips this hide; the product default `anim::type::normal` does not. | Take the judgement on a bounded readiness wait in `until` (`Test::BoxButtonReady`) rather than as an assertion in the turn the box is resolved. Do not relax `BoxButtonReady`'s visible-and-enabled requirement. Confirm with the `test_box_button.h` self-test. Distinguish this whole-layer hide from a product-hidden or disabled footer (`BoxButtonHidden` / `BoxButtonDisabled` after the layer has shown). |
| Every busy reading reports no matched button (`matched=0` / `buttonFound=false`) while the footer button is plainly on screen and other clicks in the same run work | `ReadBoxButtons` refuses to *match* a disabled or hidden shell button on purpose — a click on one is the silent no-op the helper exists to remove — so `matched()` is false and `ClickBoxButton` Fails. The only previous answer was a formatted `labels` entry `"Save (disabled)"` / `"Save (hidden)"`, which a caller either string-matched or read as "the button is not there". | Read `present` / `disabled` / `hidden` on the same `BoxShellButtons` reading, or `BoxButtonDisabled` / `BoxButtonHidden` beside `BoxButtonReady`. `matched()` stays false and `ClickBoxButton` still Fails. Confirm with the `test_box_button.h` self-test. Do not parse `labels` for a boolean and do not treat `matched()==false` as absent. |
| A capture of a visible widget is blank or refused at perfectly sane geometry — `target grab still looks blank` or `render root paints no background of its own` — and the target is a toast, a tooltip or another fade-in wrapper | `Ui::Toast::internal::Widget::paintEvent` (`toast_widget.cpp:585-600`) draws its whole frame into a transparent proxy at the fade-in opacity, so the grab holds the harness base and nothing else. | For a **toast**, capture with `Test::CaptureToastSubtree(toast, name)` (`test_toast_capture.h`) gated on `Test::ToastSubtreeReady` — its settled-show term is this module's own answer to the same blankness, and its frame cannot compose the surface beneath; never `Test::CaptureViaWindow`, which would (see the row below). For a tooltip or another fade-in wrapper, capture with `Test::CaptureViaWindow(widget, name)`, which grabs the widget's window cropped to the widget's rect mapped into it. A blank mid-fade frame is a `Note` and never a FAIL, because the decisive oracle is the joined `accessibilityName` of the toast's `Ui::FlatLabel`s and the capture is corroboration. Second fact for the same target: only inside a synchronously drained input helper, which starves `hideAnimated()`, a toast's fade-out never completes, so toasts persist far past `Ui::Toast::kDefaultDuration` (1500 ms) — a harness-environment effect of that drained helper, never a product defect to “fix”. Across the runner's real event-loop ticks the manager's own hide timer still fires and takes an ordinary toast down; see the live-toast walk row below. |
| A toast capture is perfectly legible and shows the sheet, box or row underneath it: words, a hint or a row the toast is not about, readable through the toast, and its four corners holding whatever was behind them | `Test::CaptureViaWindow` grabs the toast's **window** cropped to the toast's rect, and `st::toastBg` is `#2c3033e5` (`ui/colors.palette:448`) — alpha `0xE5`, about 90% opaque — so about a tenth of every pixel the product painted underneath composes into the frame. The corners are worse: the rounded background is `_roundRect.paint(p, rect())` (`toast_widget.cpp:525-528` → `round_rect.cpp:65-125` → `image_prepare.cpp:75-98`), whose corner mask is transparent outside the arc, so the rect's four corners are not covered at all and the surface beneath shows there unblended. | Capture with `Test::CaptureToastSubtree(toast, name)` (`test_toast_capture.h`), which grabs the toast subtree itself — `Ui::GrabWidgetToImage` fills every pixel the toast leaves uncovered with `st::windowBg->c`, so the frame holds the harness base and never the product surface. Gate on `Test::ToastSubtreeReady` and read the phrase through `Test::CheckToastReads`. A root that is not the live toast, or a rect outside its geometry, is a named FAIL quoting both rects — never a silent reframe and never a quiet no-op. Resolve the toast with `Test::FindLiveToast()`; `Ui::Toast::internal::Manager` enumerates nothing. |
| A check read `Test::FindLiveToast()` as null and concluded no toast was raised, or a reading taken after a longer settle found an empty walk and treated that as a confident absence | `Test::FindLiveToast()` is `return (all.size() == 1) ? all.front() : nullptr` (`test_toast_capture.cpp`), so one `nullptr` answers both zero live toasts and more than one — a null-versus-non-null assertion cannot tell "no toast was raised" from "two toasts were". Separately, the persist fact in the fade-in row above holds only inside a synchronously drained helper; across the runner's real event-loop ticks `Ui::Toast::internal::Manager` still arms `_hideTimer` for `_hideAt`, `hideByTimer` calls `Instance::hideAnimated()`, and the completed fade reaches `Instance::hide()`, so a reading taken on a longer settle finds an empty walk and reads as a confident absence of a toast that was raised correctly. | Record `Test::FindLiveToasts().size()` and every live toast's text (`Test::ReadToastText`) in the check's own `details`, which print on the passing verdict too. Take the reading inside the toast's lifetime on `Test::ToastSubtreeReady` rather than on a longer settle. Do not treat `FindLiveToast()`'s `nullptr` as "no toast" without that count and those texts. |
| `focused=0` for a field that is plainly focused, often beside `focusWindowSet=1 focusWidget=QTextEdit` | `Ui::InputField` declares its own non-virtual `bool hasFocus() const` returning `_inner->hasFocus()` (`input_field.cpp:4276`), and `QWidget::hasFocus()` is non-virtual too, so a read through the `QWidget*` a generic finder returns answers for the wrapper, not for the inner editor that holds the focus. | `dynamic_cast<Ui::InputField*>` the resolved widget and call its own `hasFocus()`, or test `QApplication::focusWidget()` for descendancy of the field. `Ui::PasswordInput` is a `QLineEdit` subclass with no wrapper, so a direct read there is already right. |
| A passcode / cloud-password box accepted Enter and produced no request: no SRP computed, no API call, no error | `PasscodeBox::submit()` (`passcode_box.cpp:350-383`) is five per-field `hasFocus()` branches and nothing else; in a Qt-inactive window every branch reads false, Enter falls through and `save()` never runs. | Submit through the box's own shell button with `Test::ClickBoxButton` — `lng_passcode_submit` when a caller set `customSubmitButton`, otherwise `lng_settings_save` or `lng_passcode_remove_button` — whose handler calls `save()` with no focus dependency; assert activation first, per the activation row above. Two of those branches are irreversible on a live account: `lng_passcode_remove_button` is the turning-off submit that disables the cloud password (`passcode_box.cpp:263-269`), and a `customSubmitButton` is `tr::lng_theme_delete()` on the account self-destruction flow (`self_destruction_box.cpp:58`). Place that click last in the scenario, per step 3 of Building one reliable packed scenario above. `PressKey(Qt::Key_Enter)` is a last resort only, and log which path submitted (`submittedVia=`) so a fallback stays visible. |
| `descriptionRead=0` for a `PasscodeBox` whose description the product plainly shows, while the same run's title read-back answers `titleLabelRead=1` | `PasscodeBox` renders `CloudFields::customDescription` (`passcode_box.h:59`) into `Ui::Text::String _about` (`passcode_box.h:174`, set at `passcode_box.cpp:272-278`) and draws it from the box's own `paintEvent` (`passcode_box.cpp:392`, `_about.drawLeft(...)`). No `FlatLabel` exists for it, so every label and accessibility read-back returns nothing on a correct product. The title is different: `BoxContent::setTitle` reaches `Ui::BoxLayerWidget`, which creates an `object_ptr<Ui::FlatLabel> _title` (`box_layer_widget.h:144`, `box_layer_widget.cpp:196-212`), so a title read-back does work. | Decide it in process from the box's own construction seam: publish the comparison of `fields.customDescription` against the expected `tr::` phrase through `Test::CheckTextReads` and record the boolean as a row (`descriptionIsRestore=1 descriptionIsPhrase=0`), keeping the label read-back as a detail only. The row is about the **description** alone; a title read-back needs no such repair. |
| Text was typed and nothing was inserted: the field's own text stays empty and `changes()` never fires, even with `focused=1 focusWidget=QTextEdit` | The events were aimed at the `Ui::InputField` wrapper, which does not override `keyPressEvent` (`input_field.h:433-437`), and Qt propagates an ignored key event up the parent chain and never down into a child - the `QEvent::KeyPress` case of `QApplication::notify` re-delivers it to `w->parentWidget()` until it is accepted or a window is reached - so the keys reach the wrapper's ancestors right up to the primary window and never the inner `QTextEdit` that owns the text. | Type into `dynamic_cast<Ui::InputField*>(widget)->rawTextEdit()`, fall back to `Test::CommitText` on that same editor when the text stays empty, and prove insertion by reading the field's own `getLastText()` afterwards. |
| A text oracle fails on a date, time or number whose two sides read identically in the details | The formatter's U+202F or U+00A0 against the read-back's U+0020. `Ui::Text::String`'s block parser maps every space-class character except U+00A0 to U+0020 (`text_block_parser.cpp`, `replaceWithSpace`), so a `Ui::FlatLabel` fed `langDateTime()`'s narrow no-break space before AM/PM reads back a plain space through `accessibilityName()`, while a U+00A0 survives the parse. | Compare through `Test::CheckTextReads` / `Test::NormalizeSpaces` (`test_text_reads.h`): they map U+202F and U+00A0 to U+0020 on both sides and print both raw strings' distinct `U+XXXX` whitespace tokens on the passing verdict as well as the failing one. Never loosen digits, month or AM/PM. |
| A check is missing from the log with no PASS and no FAIL | The stage's gate read false and it ran as a hand-rolled no-op - `run` and `then` skipped, `until` answered true - so it wrote nothing at all. Distinct from the stages a timed-out stage or the watchdog skips (see “Scenario teardown before quit” above), which leave the timeout's own FAIL behind them. | Give the stage a `skipReason` (`test_runner.h`): a non-empty return emits `TEST_RESULT: N/A: <stage> - <reason>` in the turn that begins it, counts in `SCENARIO_RESULT`'s skipped clause and in the `test-run` report's skipped list, and never waits or times out. Confirm the instrument with the `test_gated_stage.h` self-test. |
| A language-restore check reports `PASS` against the wrong language: after the “restore” the fixture's synthetic text is still live through the product's own accessor, or an already-painted reactive label still shows it, and the row written to observe the restore passed anyway | Two independent halves. `Lang::Instance::fillFromSerialized` (`lang_instance.cpp:423`) never clears `_values` or `_nonDefaultValues` and re-applies only the pairs that were **non-default** in the serialized snapshot (`:544-546`), so a key that was *default* before the install is absent from that set and keeps the fixture's value. And neither it (`:550`, which fires `_idChanges` only) nor `switchToId` on an ordinary id (its `_updated.fire` at `:257` sits inside the `#TEST_X` / `#TEST_0` branch) fires `Lang::Updated()`, the only signal `Lang::details::Value` (`:811-817`) re-reads on and the producer every `tr::` phrase funnels into (`lang_values.h:104-106`), so an already-painted `Ui::FlatLabel` keeps the fixture text. The vacuous PASS is the consequence rather than a second fault: an expectation rebuilt from the live producers at read time matches a restore that did nothing, so the check reports `PASS` against the wrong language — three rows did exactly that in run 27 of `2026/09/07/prepare-gasless-gram-transfers-for-server-selection`, and only an unrelated post-teardown control caught it. | Install and remove through `Test::InstallLangPack` / `LangPackFixture::remove()` (`test_lang_pack.h`), whose removal is `switchToId` + `fillFromSerialized` **plus** one empty `MTP_langPackDifference` for the instance's own lang code and version, which applies and resets nothing and exists only to fire the notification at `lang_instance.cpp:698`. Freeze the expectation **before** the install through `frozen(key)` and never rebuild it from the live producers afterwards; `checkRestored` compares against those frozen bytes and against the key's frozen non-default state, so “was default before, is default again” is proved rather than “reads the same string”. Confirm the instrument with `AppendLangPackSelfTest`, whose `LangRestoreFault::LeaveOneInstalled` and `::SuppressNotification` arms fail exactly the restore row and the read-back rows. |
| A language-pack scenario or self-test writes nothing but `TEST_RESULT: N/A` rows, every stage gated with “no candidate language key resolves in the generated table” and every candidate quoted as already overridden (`nonDefault="Cancel"` beside `value="Cancel"`), and widening the candidate list changes nothing | The cached cloud language pack overrides **every** key the generated table knows, so a currently-default key does not exist to be found. The client's own log states the count: `Lang Info: Loaded cached, keys: 10993`, which `Lang::Instance::fillFromSerialized` writes from `nonDefaultValuesCount` (`lang_instance.cpp:543`), against `kKeysCount = 10948` (`lang_auto_counts.h`) — two counts over different sets, since `applyValue` writes `_nonDefaultValues` unconditionally (`:731-732`) and so counts cloud keys the generated table does not know. That holds on any client that has ever downloaded a cloud pack, which is every real client and every `-testagent` run against a real account. The property is universal over the key table rather than specific to the candidates tried, which is why a longer candidate list cannot help and why the gate is permanent rather than flaky. | Arrange the precondition instead of searching for it. Freeze the live pack FIRST — an outer `Test::InstallLangPack` fixture does exactly that, and its `remove()` restores it — then call `Lang::Instance::switchToId` with the identity read from the instance itself: `reset` (`lang_instance.cpp:281-305`) rewrites every `_values[i]` from `GetOriginalValue(i)`, clears `_nonDefaultValues`, zeroes `_nonDefaultSet` and sets `_version` to 0, so every key is default by construction, and on an ordinary id it fires `_idChanges` only and never `_updated` (`:250-261`). Assert that premise with a check of its own — `Test::AppendLangPackSelfTest`'s first stage is that whole arrangement, and its `both keys are default after the live pack was held and reset` row FAILs if the reset did not take. One ordering trap for anyone reproducing the leftover-value half by hand: the snapshot to restore to must be taken **after** that reset, or `fillFromSerialized` re-applies the cloud override and the half does not reproduce. |
| A scenario dies inside its own ink scan: `SIGABRT` with no `TEST_COMPLETE` after a long run of `PASS` rows and saved screenshots, the dump pointing at `std::vector::operator[]`; or, with no crash at all, a candidate count read back as a plausible `0` from a reading that classified nothing | `ScanInk`’s refusing paths returned before `result.candidates = std::move(candidates)` and `result.counts.assign(...)`, so a refused reading carried `ok=0` with **both vectors empty** and no reason of any kind. `scan.counts[i]`, `front()` or `back()` on that reading is a read past the end the hardened Debug `std::vector` aborts, and the hand-rolled `counts.empty() ? 0 : counts[0]` guard three campaigns wrote instead of crashing turns the same refused reading into a number a log reader cannot tell from a measurement. `MeasurePaintedInk` compounded it: when the derivation succeeded and the scan then refused, neither branch of its report chain ran and `report` stayed the empty string. | Read a count only through `Test::InkScan::countAt(i)`, which names the candidate and answers a named refusal for an index the reading does not have, for a reading that classified nothing and for candidates that do not separate; branch on `InkScan::state` and `reason`, never on an empty vector. Print the reading through `Test::FormatInkScan`, which carries the candidate colours, the counts, the collision and the five thresholds on the passing verdict too, so a palette whose references do not separate is readable without re-running the campaign. The refusing paths now fill both vectors, so they are always the same length, and `MeasurePaintedInk` writes a non-empty `report` on every path. Confirm the instrument with `AppendDeriveBandSelfTest`. Do not read this as the latched-hover row or the light-footer-fill row above: those are `DeriveBand` refusing to derive a band at all, this is a scan that ran and could not classify what it scanned. |
| A `Classified` reading with `reason=none`, every candidate count zero and `ambiguous` equal to `inkPixels`, on pixels that are demonstrably painted | The candidates separate from each other and from the fill by the shipped thresholds, but one candidate lies on the segment from the measured background to another, so the classifier's margin never lets a pixel of it win. The app's dim text is this triple: `windowSubTextFg` `#999999` on the segment from `windowBgOver` `#f1f1f1` to `windowFg` `#000000` (`ui/colors.palette`). Observed in `2026/09/21/ui-show-the-recipient-in-the-gram-send-box`, whose day-theme arms failed on correct pixels. | The scan answers `background-collinear` instead of `classified`, and its `reason` names the unreachable candidate, the candidate whose segment swallows it, and the measured background. `countAt` refuses with `count=none`. Confirm with `AppendDeriveBandSelfTest`. `candidates-collide` and `no-ink` keep their own texts. |
| A "this surface paints a coloured glyph" claim decided by counting pixels that are neither the pen colour nor the fill, passing and failing on totals only about 2% apart | Subpixel text rendering puts chromatic fringes on ordinary glyphs, so that count cannot separate a coloured mark from plain text. The diagnosing run saw 794 such pixels (mean `#888988`) for an ASCII name against 813 (mean `#c94840`) for an emoji. Density does separate them: the emoji filled about 70% of a near-square window, the fringes about 26% of a wide smear. | Read `ReadChromaticRaster` (`test_ink.h`). It reports the densest chromatic window and the measured mean of the pixels it matched, and refuses by name as `no-chromatic` or `below-density` instead of handing back an empty box. Confirm with the chromatic arm of `AppendDeriveBandSelfTest`. |
| A "text is not link-coloured" (or "text is pen X") check fails on correct neutral text because `ScanInk` answers `classified` with a non-zero `countAt` for a chromatic candidate, in both themes | Subpixel antialiasing puts chromatic fringes on the glyph. `ScanInk` attributes each ink pixel to the candidate segment nearest it, so those fringes go to the link pen even when the solid cores are the neutral pen. A zero on `countAt(chromatic)` is not reachable, and no threshold on the non-zero count is principled. `ReadChromaticRaster` does not repair it either: it is a mark-density reading, not a pen reading, and its `below-density` refusal is no proof that the text is not a chromatic pen. | Read `ReadGlyphCore` (`test_ink.h`). It reports the modal colour of the solid cores and each named pen's solid-core count, and refuses by name as `no-paint` or `no-solid-core` instead of handing back a fringe colour. Confirm with the glyph-core arm of `AppendDeriveBandSelfTest`. |
| A panel reading that changed by one and was attributed to the panel the scenario expected, or a zero recorded as "no panel opened" by a walk that was never shown to reach a panel it could see | A count is not a difference: one panel closing while another opens reads as no change, and a count cannot name which pointer is new — and an `isVisible()` / `!isHidden()` filter counts a `Ui::SeparatePanel` that is still painting its show-animation cache, so the panel such a reading names may be the wrong one. A bare zero from a walk with no control cannot be told from an enumeration that structurally could not see a panel. Distinct from the `Test::DiscriminatingScan` row above, which is a sweep whose enumeration cannot reach its subject at all: here the walk reached the right widgets and read the wrong statistic out of them. | Take a `Test::PanelList` mark before the action and read `Test::WalkPanels(&scan, { .before = mark })`'s `added` — pointers, not a count — under the default `Settled` liveness, which `ReadPanelShowState` decides; certify the zero with `scan.report()`, and read `PanelWalkText` / `PanelWalk::refusal` where a pure reading safe to poll from `until` is wanted; refuse an ambiguous identity ask with `Test::PickPanel` instead of taking the front of the list. Confirm the instrument with `Test::AppendSeparatePanelWalkSelfTest`. |
| Exactly two `FAIL` rows in the first stage of `Test::AppendSeparatePanelWalkSelfTest` — the `fixture gate:` premise row reading `state=live translucentWindows=0` where `show_cache` was expected, and the settled reading holding the same panel it was expected to exclude — while every other row in the run passes, including the `!isHidden()` control in that same stage, the `panel_walk_show_cache discriminates` scan report, and all of the settle, exclusion and teardown stages | Not a harness fault but a host precondition. `Ui::SeparatePanel` builds its show-animation cache only inside `if (_useTransparency)` (`ui/widgets/separate_panel.cpp:1127-1132`), and `_useTransparency` is `Ui::Platform::TranslucentWindowsSupported()` latched once in `initGeometry` (`:1424`) — an inline `return true` on macOS and Windows (`ui/platform/mac/ui_utility_mac.h:16-18`, `ui/platform/win/ui_utility_win.h:19-21`) and the display server's answer everywhere else (`ui/platform/linux/ui_utility_linux.cpp:589-624`: true on Wayland, the `_NET_WM_CM_S0` selection owner on X11, false otherwise). With no cache `hideChildren()` never runs, so the panel's children are never all hidden at once, `ReadPanelShowState` answers `live` in the turn the panel was shown, and the `Settled` walk keeps it. `Ui::PopupMenu` carries the same precondition on the same predicate and states it in `test_menu.h`. | Tell it from an instrument fault in one glance: the failing premise row names itself `fixture gate:`, and an instrument fault takes the controls down with it — the scan's `discriminates` row and the `!isHidden()` control would fail too, and so would the settle and difference stages; here both controls pass and only the two cache rows fail. Nothing in the harness is to be repaired: read the pair as the host precondition, keep the rest of the run's verdict, and prove the cache-versus-settled distinction on a host whose display server supports translucent windows. Do not convert it to a `skipReason`: `test_menu.cpp:248-253` answers this same predicate with a named hard `Check` rather than a skip, and `test_panel.h`'s doc block records why this module follows it — a skip would withdraw the cache-versus-settled certification on exactly the hosts that cannot perform it, instead of reporting it. This is a host property the run cannot arrange, so it is not a fixture gate in the “Media fixtures and fixture gates” sense above either. |
| A click check reads no effect in the very tick that clicked - for example `detailCount=0 before=0 id= via=handler guard=[]` after a tap that plainly reached a handler - and the stages that depend on that click read their `skipReason` in the same tick and skip as `N/A`, so whether the click worked is never read | `ActivateClickHandler` (`Telegram/lib_ui/ui/click_handler.cpp`) does not call `onClick` itself: it posts `strong->onClick(context)` through `crl::on_main(guard, ...)`, so the handler runs in a later main-queue turn. The production `HistoryInner` release path (`Telegram/SourceFiles/history/history_inner_widget.cpp`, the `mouseActionCancel()` followed by `ActivateClickHandler(window(), activated, prepareClickContext(...))`) and a direct call from a scenario defer alike. This differs from the settling-layer row above: there the synthesized click intermittently does not land at all; here the click landed and the handler ran, one tick later than the reading. Observed in `2026/09/17/open-the-chat-after-sending-grams-from-a-profile` (`work/test.md`, Run 3). | Click, then wait a bounded tick for the effect in `until` (the details count grew, or 2.5 s), and only then assert in `then`; a `skipReason` that depends on a click's effect is read after that wait, never in the tick that clicked. Repaired that way from Run 4 of the same campaign. |
| A tap on a service message that also displays a media activates the wrong link: the guard rows name a registered handler that is not the media's (`guard=[registered id=... constructor=...]`, no media guard row) and an unrelated request such as `payments.getSavedStarGifts` registers right after it - the recipient's profile opened instead of the media's own action | `HistoryView::Service::textState` (`Telegram/SourceFiles/history/view/history_view_service_message.cpp`) lays the service text above a displayed media: with the media displayed it shrinks the text geometry by `st::msgServiceMargin.top() + media->height()`, computes `mediaTop` below that geometry, and hands the point to `media->textState` only in its `mediaDisplayed && point.y() >= mediaTop` branch. A link scan that walks the element from its top therefore meets the text's own links first - for a transfer card that is the recipient-name peer link - and never reaches the media's link. Observed in `2026/09/17/open-the-chat-after-sending-grams-from-a-profile` (`work/test.md`, Run 4). | Scan only the media rows: from the element's bottom up to `height - media->height()`, and print `mediaTop` and `mediaHeight` in the details so a miss names the geometry it searched. Repaired that way from Run 5 of the same campaign, which found the link at `mediaTop=47 mediaHeight=196`. |
| A readiness gate on a served one-page history times out with the list resting at its bottom - `scrollTop=5055 scrollTopMax=5055`, the card resolved and mapped whole inside its owner - and every later stage is skipped | The legacy `HistoryWidget` requests its older page only from `HistoryWidget::preloadHistoryByScroll` (`Telegram/SourceFiles/history/history_widget.cpp`), which calls `loadMessages()` only while `scrollTop <= kPreloadHeightsCount * scrollHeight` (`kPreloadHeightsCount` is 3 screens). A served history shown at its bottom never issues that request, so `historyLoadedAtTop()` stays false while `historyLoadedAtBottom()` is already true, and a gate that requires both edges before the first scroll can never turn true. Observed in `2026/09/17/open-the-chat-after-sending-grams-from-a-profile` (`work/test.md`, Run 1). | Gate an at-bottom reading on the bottom edge only (`historyLoadedAtBottom()`), which is the edge the claim is about, and print `loadedTop`, `loadedBottom` and the scroll movement in the readout so a timeout names its failing term. Repaired that way from Run 2 of the same campaign. |
| A "the box stays open after the refusal" check reads `isVisible()` false on a box that plainly exists - `Ui::GenericBox 0,48 364x176`, later closed by `closeBox` - and the same tick's Info layer readout says `(visible 0)` where a passing leg of the same check says `(visible 1)` | The sibling of the layer-hide row above: `LayerStackWidget::prepareForAnimation()` (`Telegram/lib_ui/ui/layers/layer_widget.cpp`) hides the current layer while a show or hide animation paints from cached pixmaps, so a refusal that lands inside the box's own show animation is read while the whole layer, the box included, answers `QWidget::isVisible()` false. That row covers the shell buttons reading `(hidden)` in the turn the box was resolved; this one covers a synchronous `isVisible()` read on the box itself, taken in the tick a controlled refusal arrived, on a box that is being shown rather than resolved. Observed in `2026/09/17/open-the-chat-after-sending-grams-from-a-profile` (`work/test.md`, Run 4, C5b and C5c). | Read the box after a bounded settle (visible, or 1.5 s) and print `visible`, `hidden` and `layerShown` so the row shows which term answered. Repaired that way from Run 5 of the same campaign, whose settles measured 152 ms and 37 ms. |

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
