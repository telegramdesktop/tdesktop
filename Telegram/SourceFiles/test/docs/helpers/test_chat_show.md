# test_chat_show

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API.
Source contract: [test_chat_show.h](../../test_chat_show.h); the self-test is
implemented in [test_chat_show_self_test.cpp](../../test_chat_show_self_test.cpp).

## Use when

An overlay is about to press, drag, right-click or otherwise point at one
message of a chat list - the legacy `HistoryInner`, a
`HistoryView::ListWidget` section or Recent Actions' `AdminLog::InnerWidget` -
and needs that message's own view, or a rectangle inside it such as a widget
card, wholly on screen, laid out and painted at its final geometry, ready by
the caller's own condition, and, optionally, already resolved by the list under
the press point. `AppendShowInChatList` appends one stage that does all of it
and refuses by name instead of timing out. Use it instead of an overlay's own
scroll-then-wait: in `2026/10/21/select-and-copy-widget-text-with-the-mouse`
such copies dragged at a target above the viewport, left a card 9 px out of
view after one scroll and a later resize, pressed a card that a far scroll had
made non-live, and pressed with no hover at a layout not yet painted.

Use something else when:

- the message is not in the list's loaded slice: navigate first with the
  product's `showThread` / `showAtPosition` (see
  [chat-list resolution](../chat-lists.md)); this helper never loads history;
- the input is a context menu: `OpenChatListContextMenu`
  ([test_chat_menu](test_chat_menu.md)) owns its own enter, menu and leave;
- several messages must be wholly in view together, wholly scrolled off
  above or below, or must stay that way for a window:
  `AppendPlaceInChatList` and `AppendStayedInChatList`
  ([test_chat_show_place](test_chat_show_place.md)), gated by the caller's
  fresh visibility reading - for a widget card `Tgw::Runtime::queryVisible()`
  or `Tgw::Runtime::WatchingOnScreen()`, never the cached `_visible`;
- only a frame is needed of something already in view: read
  `ReadMappedTarget` and capture with `CaptureMappedTarget`
  ([test_capture](test_capture.md)).

## Preconditions and limits

- **A loaded own view.** When the stage begins, the list must already show
  its own view of the message: `HistoryInner::viewByItem` with
  `itemTop() >= 0`, `ListWidget::viewByPosition` answering a view whose
  `data()` is the message and whose `delegate()` is that list, or the admin
  log's private `viewForItem` (an event hidden in a collapsed delete group has
  none). `item->mainView()` is never taken on trust. The list must be the
  owned widget of its `Ui::ElasticScroll`, as all three sections build it.
- **A shown, exposed paint owner.** Acceptance needs a real list paint,
  received after the helper's repaint request, that completed at the
  accepted geometry, so the primary window must be shown, not
  minimized, and exposed: keep it with `KeepWindowExposed` and wait for
  `ReadWindowExposure(...).exposed` first ([test_window_exposure](test_window_exposure.md),
  [launch behavior](../launch.md)). A window that cannot paint ends
  `not-ready` with `paints=0` and `windowExposed=0` in the reason; that is a
  fixture gate, not a helper success. This excludes
  `KeepMainWindowNotMarkingRead`, which minimizes the window.
- **Units.** Logical pixels throughout. `rect` and `hover` answer
  element-local values: (0, 0) is the view's top-left as the list's own
  `mapPointToItem()` places it, the selection-mode offset included. A null
  `rect` means the whole view, `QRect(0, 0, view->width(), view->height())`,
  never a list-width row. The reading's `rect`, `element`, `viewport` and
  `hover` are in the list's own coordinates, ready to hand to a press;
  `mapped` is in the owning scroll's.
- **Pure callbacks.** `rect`, `ready`, `readyDetails` and `hover` run
  synchronously on many samples, one from the helper's paint filter included,
  always with the list's current own view. They read only: no layout flush,
  no activation, no input, no pointer kept past the call. The readiness is
  the caller's: for a widget card it is the overlay's Tgw predicate (live
  runtime, its interactive core attached and accepting input, a cached frame
  at the current card size, a clean renderer); this module includes no `tgw/`
  header.
- **It scrolls like a user.** Corrections go through
  `Ui::ElasticScroll::scrollToY`, which runs the section's scroll handlers
  synchronously, as a wheel would: read-only history and admin-log preloads
  near the edges, visibility updates, heavy-part unloading of far views,
  `ChatWidget`'s saved history scroll state, and read marking in a window that
  marks read (keep injected messages client-id local, see
  [test_history_fixtures](test_history_fixtures.md)). Each correction centres
  the rectangle in the visible message area; later layout changes are
  corrected again from a fresh sample until the deadline. It scrolls only
  vertically and shows one message at a time. The scroll position is not
  restored.
- **Settle and deadline.** The view, its geometry, the scroll and the pending
  layout (`Element::pendingResize()`, and on `HistoryInner` its
  `hasPendingResizedItems()`) must read the same for 400 ms. The helper's
  deadline is `max(timeout, 0)` ms after the stage's run (10 s by default);
  the runner's stage timeout is `kDefaultStageTimeout` more, so a slow Debug
  layout ends in a named refusal, never `stage timed out`.
- **Not measured.** Floating siblings drawn over the scroll (the corner
  jump-down buttons, the admin log's scroll-down button) are not part of the
  visible message area or the mapped target, and the synthetic hover goes to
  the list directly, so they never intercept it; a capture may still show them.
  The system pointer path is not exercised: the real cursor is neither moved
  nor attributed (the lists' `enterEventHook` resolves `QCursor::pos()` before
  the helper's move resolves the point again).
- **Platforms.** Portable C++ on every platform; see Verification for what
  has actually run.

## Minimal use

A text selection that starts at the hovered point. The press comes in the very
next stage's run, in the same turn as the hover, with no capture or wait stage
between them. A queued drag needs the overlay's `Window::MainWindow::launchDrag()`
hook ([test_queued_drag](test_queued_drag.md)).

```cpp
const auto shown = Test::AppendShowInChatList(runner, {
	.name = u"target"_q,
	.resolve = [=] {
		return Test::ChatListShowTarget{ state->list.data(), state->id };
	},
	.rect = [](Test::ChatListShowView view) {
		return view->innerGeometry();
	},
	.hover = [](Test::ChatListShowView view) {
		return view->innerGeometry().center();
	},
});
const auto drag = Test::AppendQueuedStartDrag(runner, {
	.name = u"select from the hovered point"_q,
	.resolve = [=] {
		return Test::QueuedStartDragTarget{
			shown->shown() ? shown->list.data() : nullptr,
			shown->hover,
			QPoint(shown->rect.right(), shown->hover.y()),
		};
	},
});
runner->add({
	.name = u"judge the selection"_q,
	.run = [=] {
		Test::Check(
			shown->shown(),
			u"fixture gate: the target was shown, painted and hovered"_q,
			Test::ChatListShowText(*shown));
		Test::Check(
			drag->delivered(),
			u"fixture gate: the drag was delivered"_q,
			Test::QueuedStartDragText(*drag));
		// The task's own oracle on the selection follows here.
	},
});
```

A refused show hands the drag no widget, so the drag refuses `no-target` and
sends nothing; the judge reports both readings. Add `ready` and `readyDetails`
when the content needs more than layout and paint, for example a widget card's
live-and-painted predicate with its runtime state in the details.

## Readings and refusals

The stage is `show in chat list: <name>`. It returns a
`std::shared_ptr<const ChatListShow>`, filled while the stage runs: read it
from a later stage. `shown()` is true only when the stage finished with no
refusal, including the hover when one was asked for. The helper never Passes
or Fails; judge the reading and treat a refusal as a fixture gate, never as
product success.

An accepted reading proves, for the current own view at acceptance: the
requested rectangle lies inside the view's bounds, inside the list's visible
message area and inside its owning scroll (`ReadMappedTarget(scroll, list,
rect).resolved()`); that geometry, the scroll position and the pending layout
had read the same for the settle time with nothing pending; the helper then
requested a repaint of the rectangle, and list paints received after that
request that completed at that same geometry covered the whole rectangle
(`QRegion(rect)` minus their union is empty; a paint received before the
request, such as the natural one right after a scroll, never counts, and
neither does one the list's own early return skips - `contentOverlapped()` on
`HistoryInner` and the admin log, `listIgnorePaintEvent()` on `ListWidget`,
pending resized items on `HistoryInner`); and the caller's `ready` answered
true. So an accepted reading always has `paintRequests` and `paints` above 0.
With a hover, the list then resolved that view at the point:
`isUnderCursor()` and `pointState(local) != Outside` after the enter and the
move drained, at an unchanged geometry.

One row per stage, in ordinary integers:
`CHAT_LIST_SHOW: name= finished= shown= kind= list= item= view= viewShown=
local= element= rect= viewport= mapped= initialRect= initialViewport=
scrollTop= scrollRequests= scrolls= generation= settled= pending= inView=
paintRequests= paints= ignoredPaints= painted= readyChecks= readyFalse= ready=
hover= hoverLocal= hoverSent= hovered= elapsedMs= timeout= refusal=
readiness={<readyDetails>} hoverState={mouseActive= activeWindow= pending=
view= underCursor= pointState=}`, then ` - <reason>` on a refusal. Each scroll
request adds `CHAT_LIST_SHOW_SCROLL: name= request= scrollTop= asked= now=
moved= before= after= effective= view=`, the rectangle in the scroll before
and after, read again right after the scroll's handlers ran.

| Refusal | Meaning |
| --- | --- |
| `no-list` | No list, not one of the three lists, not the owned widget of a `Ui::ElasticScroll`, or destroyed while the helper waited. |
| `no-target` | No message id, no such message in the list's session, or the message was destroyed while waiting. |
| `no-view` | The list showed no own view when the stage began (zero scrolls), or lost it after a scroll and did not show it again by the deadline; the last geometry is kept. |
| `cannot-fit` | With a usable layout that held for the settle time, the rectangle is outside the view's bounds or taller or wider than the visible message area. Never decided on zero or unsettled geometry. |
| `out-of-view` | The rectangle fits but was not wholly in view at the deadline, for example a scroll that does not move (`disableScroll`). |
| `not-ready` | In view at the deadline, but the layout stayed pending, the geometry had not settled, no paint received after the repaint request covered it (with paint counts and the window's state), or the caller's readiness stayed false (with its counts and details). |
| `hover-outside` | The hover point is outside the shown rectangle; no input was sent. |
| `hover-unresolved` | After the enter and the move the list did not resolve the view's content at the point, or the view or its geometry changed while their postponed calls drained; a leave was sent. |

## Lifetime and cleanup

The stage owns a repeating timer that makes the scroll corrections and the one
repaint request per geometry generation, and an event filter on the list that
watches its paints and queues their confirmation to itself. Both are released
in the stage's then and, idempotently, through `Runner::onFinish`; deleting the
filter drops any confirmation still queued. No element pointer is kept between
samples; the list is held by a `QPointer` and the message by its id.

Lasting effects: the scroll position the helper left, whatever the scroll
handlers loaded or marked, and, when a hover was asked for and resolved, the
list's hover state (its `_mouseActive`, the resolved view and the active click
handler). That hover is meant for the next stage's press in its run. A caller
that does not press right away must leave the list itself
(`QEvent::Leave` to `shown->list` inside `Test::Settle`), or the hover carries
into later stages; a failed hover is already left by the helper. No clipboard,
account or server state is written by the helper itself.

## Verification

The module's behavioral self-test is
`Test::AppendChatListShowSelfTest(runner, channel)`, appended by an overlay's
`SetupScenario` after its chats-loaded wait. Its fixture, legs and controls,
rows, expected verdict and teardown are on its own page,
[test_chat_show_self_test](test_chat_show_self_test.md).

**Evidence.** Executed on macOS arm64 Debug against a Test-account megagroup,
in the evidence campaign of
`2026/10/24/show-a-list-message-fully-in-view-ready-and-hovered-in-the-harness`
(its `work/test.md`). All thirteen self-test legs passed with no stage timeout:
far above and far below in each of the three lists, the later-layout
correction, `no-view` in each list, and `cannot-fit`, `out-of-view` and
`not-ready` on the ListWidget. The first consumer showed an out-of-view widget
card in a History-mode ChatWidget, with its live-and-painted readiness and a
hover at the press point, then took a queued-start drag that selected and
copied exactly `sentence, this bold word too,`. Windows and all other
platforms have not run it: the code is portable, and the window exposure
helper is a no-op there.

## Related guidance

[Chat-list resolution](../chat-lists.md);
[queued drags](test_queued_drag.md);
[input helpers](test_widgets.md#input-helpers);
[mapped targets and captures](test_capture.md);
[window exposure](test_window_exposure.md);
[launch and background painting](../launch.md);
[failure diagnosis](../diagnostics.md).
