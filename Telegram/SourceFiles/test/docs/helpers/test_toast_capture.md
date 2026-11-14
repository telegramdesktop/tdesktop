# test_toast_capture

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_toast_capture.h](../../test_toast_capture.h).

## Use when

Isolated toast-subtree captures, shown/hiding/gone toast state with a bounded gone wait, and painted-content captures. Read the contract below and the source header before use.

Choose by the claim. `CaptureToastSubtree` corroborates the textual oracle
(`CheckToastReads`) with a frame that cannot hold the surface beneath, and
qualifies background settledness only. `ReadToastState` and
`ReadToastGoneWait` say whether a toast is shown, fading or gone, which the
live-toast walk alone does not. `ReadToastContent` and `CaptureToastContent`
save a toast as visual proof that its text or link was painted.

## Contract and usage

The toast capture whose frame cannot compose the product surface beneath it:
the live-toast walk over the top level widgets (`Ui::Toast::internal::Manager`
keeps `_toastByWidget` private and enumerates nothing), a grab rooted at the
toast subtree that `Ui::GrabWidgetToImage` fills with `st::windowBg` wherever
the toast does not cover, a settled-show readiness term measured against
`st::toastBg`'s own blend over that base because `_shownLevel` is private,
named refusals for a root that is not the live toast and for a rect outside
the toast's geometry that quote both rects and never reframe, the joined-label
text oracle read through `Test::CheckTextReads`, and its own self-test over a
synthetic sentinel surface it paints, which counts sentinel pixels in both
saved frames and then hides that surface to show no pixel of the subtree frame
depending on it, and which drives both documented ambiguity branches of the
resolver — a second live toast shown and taken down inside one stage, and the
empty walk its own teardown arranges — showing `FindLiveToast()` answering
`nullptr` for each, and which measures a default-duration toast live on
`ToastSubtreeReady` inside its lifetime and gone from the walk after it,
quoting the live count, the texts and the elapsed time.

`FindLiveToasts()` and `FindLiveToast()` keep that meaning: live is visible
in the walk, which includes a toast `hideAnimated()` is fading. Read the
phase through [toast state](#toast-state-shown-hiding-gone).
`ToastSubtreeReady` and `CaptureToastSubtree` qualify background settledness
only and also accept a settled frame whose children were never painted; see
[painted-content captures](#painted-content-captures).

## Toast state: shown, hiding, gone

The toast's shown level and `Ui::Toast::Instance::_hiding` are private, so
the phase is measured from outside, over a chain of readings: membership in
the `FindLiveToasts()` walk plus the settled-background measurement
`ToastSubtreeReady` takes.

| Phase | Meaning |
| --- | --- |
| `toast-shown` | In the walk, and the toast-rooted grab measures `st::toastBg`'s settled blend. This is exactly `ToastSubtreeReady`'s term, so it says nothing about painted children. |
| `toast-hiding` | In the walk and below the settled blend after an earlier reading of the same chain measured it shown. A toast drops below its full level again only through `hideAnimated()`. |
| `toast-unsettled` | In the walk, below the settled blend, never measured shown in this chain: a fade-in, or a fade-out whose chain started mid-way, which one frame cannot tell apart. |
| `toast-gone` | Destroyed or no longer in the walk. Final: a reread stays gone without a grab. |
| `toast-unresolved` | No widget, or one that is not a `Ui::Toast::internal::Widget`; `refusal` names the case. Final. A null widget, such as a `FindLiveToast()` that refused an ambiguity, is unresolved and never gone. |

Start a chain with `ReadToastState(widget)` while the toast is still shown
and continue it with `RereadToastState(previous)` on every poll; a reading
of a live toast takes one grab. `ToastStateReading` carries `phase`, `live`,
`seenShown`, `settledPermille` (-1 when no grab was taken), `readings`,
`identity`, `text` (the first reading's `ReadToastText`) and `refusal`.
`ToastStateDetails` prints
`toast-state: target=<identity|<none>> text="<text>" phase=<name> live=<0|1> seenShown=<0|1> settled=<n|none>/1000 readings=<n>`
and ` - <refusal>` when there is one.

`ReadToastGoneWait(previous, waited, bound)` is the bounded wait for gone,
a pure reading of the chain given the caller's own elapsed time: it rereads
the chain once. `waited` 0 never refuses. Once `waited >= bound`, a toast
that is not gone answers
`toast-not-gone: <identity> "<text>" still <phase> after <waited> ms (bound <bound> ms); live toasts: [...]`
and `live` lists every toast the walk still holds. An unresolved chain
answers `toast-unresolved: <refusal>` at once. A gone toast never refuses,
so compare `waited` with `bound` when the timing is the claim. After
`hideAnimated()`, bound the wait by the toast style's
`durationFadeOut * anim::SlowMultiplier() + kToastGoneMargin`, the margin
being 500 ms; a toast left to its own hide timer also needs its `duration`,
which is not scaled, and the scaled `durationFadeIn`.
`ToastGoneWaitDetails` appends `waited=<ms> bound=<ms> gone=<0|1>` and the
refusal to the state's text.

```cpp
struct State {
	Test::ToastStateReading chain;
	Test::ToastGoneWait wait;
	crl::time actedAt = 0;
};
const auto state = std::make_shared<State>();
runner->add({
	.name = u"the toast reads shown before its action"_q,
	.until = [=] {
		state->chain = Test::ReadToastState(Test::FindLiveToast());
		return state->chain.phase == Test::ToastPhase::Shown;
	},
	.timeoutDetails = [=] {
		return Test::ToastStateDetails(state->chain);
	},
});
runner->add({
	.name = u"the toast's own action takes it down"_q,
	.run = [=] {
		state->actedAt = crl::now();
		Test::Check(
			Test::InvokeLiveAction(u"task.toast_view"_q),
			u"the published view action ran"_q);
	},
	.until = [=] {
		state->wait = Test::ReadToastGoneWait(
			state->chain,
			crl::now() - state->actedAt,
			crl::time(st::defaultToast.durationFadeOut)
				* anim::SlowMultiplier()
				+ Test::kToastGoneMargin);
		state->chain = state->wait.state;
		return state->wait.done();
	},
	.then = [=] {
		Test::Check(
			state->wait.gone(),
			u"the toast is gone after its action"_q,
			Test::ToastGoneWaitDetails(state->wait));
	},
});
```

Limits:
- Hiding is read from the first grab measurably below the settled blend. In
  the turn `hideAnimated()` is called the fade has run no frame and the
  reading answers `toast-shown`; for the first part of the linear fade the
  frame stays inside the settled tolerance, about 6% of the fade-out on a
  light palette and about 20% on a dark one, and still reads shown. Poll for
  hiding from `until`; never assert it in the turn of the action.
- A toast shown with `Config::attach` slides at full opacity and reads shown
  until gone; its hide phase is unsupported.
- A toast whose window is hidden leaves the walk and reads gone, the walk's
  own limit.
- Under `anim::Disabled()` a fade has no hiding phase to read.
- A grab of a mid-fade toast runs its `paintEvent` proxy branch and leaves
  its children unpainted until a postponed restore (next section). The
  state reading measures only the background and is unaffected; a content
  reading taken later in the same turn is not.

## Painted-content captures

Signature: the saved frame of a toast holds only its settled rounded
background, with no text, icon or link, while `ReadToastText` matches the
whole phrase and `ToastSubtreeReady` answered true.

Cause. While the shown level is below 1,
`Ui::Toast::internal::Widget::paintEvent` (`toast_widget.cpp`) draws a proxy
at that opacity (`paintToProxy`: the background and each direct child
rendered through `Ui::RenderWidget`) and calls `disableChildrenPaintOnce()`.
That sets `Qt::WA_UpdatesDisabled` on every direct child widget - the one
`Content` wrapper holding the body `Ui::FlatLabel` and the icon - and
schedules the restore through `Ui::PostponeCall`
(`scheduleChildrenPaintRestore`). At full level `paintEvent` draws only the
background and leaves the children to Qt, and
`QWidgetPrivate::paintSiblingsRecursive` (qtbase `qwidget.cpp`) skips a
child whose updates are disabled, both on screen and in `QWidget::render`,
which every toast-rooted grab uses through `Ui::GrabWidgetToImage`. A
full-level paint or grab taken after a fractional-level one and before the
restore runs is therefore background-only. `ReadToastText` reads
`FlatLabel::accessibilityName()`, which is model state, and still matches.
`Core::Sandbox` runs a postponed call only at the unwind of the event-loop
level that queued it, so the restore normally runs when the runner tick
unwinds; inside one `run` or `then` that grabs a mid-fade toast and reads it
again at full level, it has not run yet. Every harness grab of a mid-fade
toast runs that `paintEvent` too, so the instrument itself can arm the
signature. A repeat makes it likely: `Ui::Show::showToast`
(`ui/layers/show.cpp`) calls `hideAnimated()` on the toast it showed last
and shows the next while that one still fades.

Background settledness and painted-content readiness are separate terms. A
content-qualified frame is settled; a settled frame is not necessarily
content-qualified, and the signature is exactly settled and not qualified.

`ReadToastContent(widget, pens, waited, bound)` resolves the root through
`ReadToastSubtree` (same refusals, no grab on a refused root), counts
`childrenPaintDisabled` - the toast's direct child widgets with
`updatesEnabled()` false just before the grab, a diagnostic and never a
term - and takes exactly one grab, on which it measures the settled term and
the content terms together:
- every `Ui::FlatLabel` of the toast with a non-empty `accessibilityName()`
  holds at least `kMinInkPixels` (24) pixels at least `kInkDelta` (45) from
  the settled blend inside its device-pixel rect (`labels`, `labelInk`);
- each pen in `pens` has at least `kMinInkPixels` pixels within
  `kSameTolerance` (40) inside the union of the label rects (`penPixels`).

The first failing term names the state:

| State | Meaning |
| --- | --- |
| `content-refused` | The root was refused; `reason` is `ReadToastSubtree`'s refusal and `image` is null. |
| `content-unsettled` | The sampled background is not `st::toastBg`'s settled blend yet. |
| `content-no-labels` | No labelled `Ui::FlatLabel`, as in a custom-content toast, which is unsupported. |
| `content-not-painted` | A label rect holds less content ink than the floor; `reason` names the label, its rect and its count. |
| `content-pen-missing` | A pen has fewer pixels than the floor. |
| `content-qualified` | Every term holds; `reason` is `none`. |
| `content-unread` | A frame no reading filled. |

The wait is a pure reading of the caller's elapsed time: `waited` 0 never
expires, and once `waited >= bound` (`kToastContentBound`, 2000 ms, by
default) a frame that is not qualified is `expired`, keeps its failing state
and reads
`content-wait-expired after <waited> ms (bound <bound> ms): <state> - <reason>`.
Poll from `until` returning `decided()`, keep the frame, and assert
`qualified()` in `then`, so a toast that never paints ends in that named
diagnostic and not in a stage timeout. `CaptureToastContent(accepted, name)`
saves exactly `accepted.image` and takes no second grab, so readiness and
saving qualify the same pixels; it logs the toast's `GEOMETRY` row while the
toast is alive and the `SCREENSHOT` row, and a frame that is not
`qualified()` or cannot be written is a loud `Fail` with nothing saved.
`ToastContentDetails` prints
`toast content: target=<identity|<none>> frame=<rect> state=<name> settled=<n|none>/1000 childrenPaintDisabled=<n|none> labels=<n> labelInk=[...] floor=24 pens=[<name>=<n|none> ...] waited=<ms> bound=<ms> expired=<0|1> - <reason>`.

Pens: body text is `st::toastFg`, a link is the toast style's
`palette.linkFg` (`mediaviewTextLinkFg` for `st::defaultToast` and
`st::defaultMultilineToast`). Pass the pens the claim is about; the text
term alone does not prove a link was painted.

During a repeat the previous toast is still fading, so `FindLiveToast()`
answers `nullptr`. Resolve the new toast from the `Ui::Toast::Instance` a
`Ui::Show` returned when the overlay shows it, or from the walk as the one
live toast that is not the previous one, never from `FindLiveToast()`. A
repeat often shows the same phrase, so tell the two apart by pointer, not
by text.

```cpp
void AppendRepeatedToastCapture(
		not_null<Runner*> runner,
		const QString &expected) {
	struct State {
		QPointer<QWidget> previous;
		QPointer<QWidget> toast;
		Test::ToastContentFrame frame;
		crl::time actedAt = 0;
	};
	const auto state = std::make_shared<State>();
	const auto repeated = [=] {
		auto result = (QWidget*)nullptr;
		for (const auto widget : Test::FindLiveToasts()) {
			if (widget == state->previous) {
				continue;
			} else if (result) {
				return (QWidget*)nullptr;
			}
			result = widget;
		}
		return result;
	};
	runner->add({
		.name = u"the repeated toast is saved with its link painted"_q,
		.run = [=] {
			state->previous = Test::FindLiveToast();
			state->actedAt = crl::now();
			Test::Check(
				Test::InvokeLiveAction(u"task.repeat"_q),
				u"the published action shows the toast again"_q);
		},
		.until = [=] {
			state->toast = repeated();
			state->frame = Test::ReadToastContent(
				state->toast.data(),
				{
					{ .name = u"text"_q, .color = st::toastFg->c },
					{
						.name = u"link"_q,
						.color = st::defaultToast.palette.linkFg->c,
					},
				},
				crl::now() - state->actedAt);
			return state->frame.decided();
		},
		.then = [=] {
			Test::Check(
				state->frame.qualified(),
				u"the repeated toast's text and link are painted"_q,
				Test::ToastContentDetails(state->frame));
			if (state->frame.qualified()) {
				Test::CaptureToastContent(state->frame, u"repeated_toast"_q);
			}
			Test::CheckToastReads(
				state->toast.data(),
				expected,
				u"the repeated toast reads its phrase back"_q);
		},
		.timeoutDetails = [=] {
			return Test::ToastContentDetails(state->frame);
		},
	});
}
```

Repair by the reading. A settled `content-not-painted` frame with
`childrenPaintDisabled` above 0 is this lifecycle: lib_ui's restore has not
run yet. Poll from `until`, where each tick unwinds and runs it, or call
`Test::SettlePostponedCalls()` before the reading in a `run` or `then` that
grabbed the toast mid-fade. A frame that expires in `until`, or stays
unpainted with `childrenPaintDisabled=0`, is not a pending restore: keep
the frames, record `childrenPaintDisabled` in the diagnosis and treat it as
a finding; never lower the floors.

Limits:
- The icon is not qualified; only labelled `Ui::FlatLabel`s and the pens.
- A custom-content toast without a labelled `Ui::FlatLabel` refuses
  `content-no-labels`.
- Pen counts use `kSameTolerance`. On the shipped toast palette the
  antialiased white text never comes within it of the link pen, nor link
  antialiasing within it of white, so the two counts separate; a palette
  that brings the pens closer makes them overlap. Print both.
- Content ink is counted inside each label's whole rect against the settled
  blend, so any other ink painted inside it counts too.
- `CaptureToastSubtree` stays the settled-only corroborating capture of the
  textual oracle; it saves the missing-paint frame this contract refuses.

## Verification

All three self-tests need a primary window and no session, chats list,
network or account fixture, and none emits a deliberate FAIL: refusals are
read through the pure readings and asserted as passing checks. They share
the process-wide toast walk, so each gates on no other live toast and its
teardown hides every toast it showed.

- `AppendToastSubtreeCaptureSelfTest`, the seven-stage self-test described
  above, over its own sentinel surface. It is the regression control for the
  subtree capture, the walk and the settled-show term.
- `AppendToastStateSelfTest`: a fresh toast reads `toast-unsettled`, then
  `toast-shown`; right after `hideAnimated()` it reads `toast-hiding` while
  the walk still counts it, inside the fade-out, and the same-turn reading
  `toast-shown` is the documented onset limit; it then reads `toast-gone`
  within the fade-out plus `kToastGoneMargin`. A default-duration toast left
  alone reads `toast-shown` until its duration ends, then `toast-hiding`,
  then `toast-gone`, and never hiding before its duration. A toast never
  hidden ends the wait in `toast-not-gone` naming it, and reads gone in the
  turn `Instance::hide()` runs (the control). Null and non-toast widgets read
  `toast-unresolved` with the failure count unchanged. The fixture gate is
  recorded in its first stage and the rest are composed in
  `Runner::addGated`; the three fade stages are `N/A` by name under
  `anim::Disabled()`; teardown is a `Runner::addRecovery` stage.
- `AppendToastContentCaptureSelfTest`: every toast goes through one
  `Ui::Show` from `Window::Controller::uiShow()` over a sentinel surface
  moved beneath each toast. A plain toast is accepted and saved as
  `toast_content_plain`, and asked for the link pen reads
  `content-pen-missing`; the window-mapped grab of the same toast holds
  sentinel pixels, so the saved frame's zero is measured. Three repeated rich
  toasts - bold words, a link and an action - are accepted with text and
  link pens and saved as `toast_content_rich_1` to `toast_content_rich_3`
  while the previous toast still fades in the walk; each saved PNG is read
  back, compared pixel for pixel with the accepted frame, measured again and
  searched for sentinel pixels. The signature is reproduced on the third
  rich toast through lib_ui's own lifecycle - `setShownLevel(0.5)`, one
  toast-rooted grab, `setShownLevel(1.)` - behind a fixture gate that the
  lever left the children with updates disabled: settled,
  `ToastSubtreeReady` true, read-back matching, `content-not-painted`.
  `CaptureToastSubtree` saves that frame as
  `toast_content_settled_only_control` without a failure (the
  removed-qualification control) and the PNG measures `content-not-painted`;
  `toast_content_missing_paint` is saved for inspection only; after
  `SettlePostponedCalls()` the same toast qualifies. Children kept from
  painting with `Qt::WA_UpdatesDisabled` set directly end the wait in
  `content-wait-expired` at its 600 ms bound while `CheckToastReads` still
  passes, and the toast qualifies once they are released. Null and ancestor
  roots refuse with no grab. It calls no toast-state, glyph-background or
  isolated-glyph reading.

## Related guidance

[Capture selection](test_capture.md#capture-and-visual-assertions); [ink thresholds](test_ink.md); [text reads](test_text_reads.md); [failure diagnosis](../diagnostics.md).
