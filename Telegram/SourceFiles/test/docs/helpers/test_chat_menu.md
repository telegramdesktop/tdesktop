# test_chat_menu

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_chat_menu.h](../../test_chat_menu.h).

## Use when

Attribute chat-list context menus to the exact message. Read the contract below and the source header before use. A menu the list builds on a later turn - a right click on a widget card - is read with the three-call form: [Menus built on a later turn](#menus-built-on-a-later-turn).

## Contract and usage

The chat-list context-menu reader, `OpenChatListContextMenu(list, target,
point)` ([input helpers](test_widgets.md#input-helpers)), in a module of its own because it depends on history
view types, which `test_menu.h` does not: `ChatListMenuReading` (the
underlying `ContextMenuReading`, with its menu pointer cleared on every
refusal; the requested message and the list's own view of it; the four
`ChatListResolution`s; `enterSent`, `leaveSent`, `menuRead`, `observed`,
`listMenu`, `menuHidden`, `deferred`, `sentAt`, `waited` and `attributed`;
`built()`, never true on default state; `pending()`, a delivered request not
decided yet), the fourteen refusals `ChatListMenuRefusalName` names, and
`ChatListMenuDetails`, which prints recorded values only. The private state it
reads - `_mouseActive`, `_dragStateItem`, `_overElement`, `_overItemExact`,
the visible message area, `_menu` and `HistoryInner::hasPendingResizedItems()`
- goes through the test-only explicit-instantiation accessor
`test_notify_override.cpp` uses, confined to `test_chat_menu.cpp`: no
production accessor, no pixel scan, no registry. Its own self-test,
`AppendChatListMenuSelfTest`, in twelve stages, needs a logged-in session and
a Saved Messages history whose folder is known. It injects two fixed
plain-text outgoing messages A and B there, a private fixture added with
`History::addNewMessage` and ids from `Data::Session::nextNonHistoryEntryId()`
- regular and so selectable, nothing sent, "Select" the only entry ever
triggered - and keeps the main window minimized with
`KeepMainWindowNotMarkingRead` from before the injection until both are
destroyed. On the legacy `HistoryInner`, and then on the
`HistoryView::ListWidget` of a `HistoryView::ChatWidget` opened through the
`use-modern-chat-view` option: a premise arm (A resolved last, the mouse outside
the list, the window inactive through `ClearWindowActive`, and an unprotected
`OpenContextMenu` at B's point that the list resolved to A or to none, never
to B, read at build through the observer); the protected reading for B under
the same conditions, built and attributed to B; "Select" from it leaving
exactly B selected (`HistoryInner::getSelectedItems()`,
`ListWidget::getSelectedIds()`); B requested at A's point refused
`wrong-target` with A in its details, its menu hidden, and "Select" through it
refused `menu-gone` with the selection unchanged; `no-list`, `not-a-chat-list`
and `outside-viewport` controls; and an active control through
`ForceWindowActive`, the window still minimized and not marking read, that
resolves B. Every `ClearWindowActive` and `ForceWindowActive` is undone in the
same turn, back to the activation captured at the start. The teardown destroys
A and B first and proves that neither id resolves to a message or to a
`ListWidget` view, then restores the option, the chat, the window state and
the Qt activation, and the same idempotent release runs from
`Runner::onFinish`. The `History::messages()` index the `ChatWidget` leg seeds
may stay allocated and keep listing both ids: an id above `ServerMaxMsgId`
lies above every slice's `range.till`, where `SparseIdsList::removeOne` cannot
reach it, and every reader of the index looks ids up through
`Data::Session::message()` and skips one with no message behind it; its
`FIXTURE_INDEX` row reports what it lists. Confirm with
`AppendChatListMenuSelfTest`.

## Menus built on a later turn

### When to use it

Over a widget card the list accepts a right click's context-menu event
without building anything: `WidgetTouch` (`tgw_touch.cpp`) records the event
and, once the widget's answer arrives on a later turn, re-sends it to the
list's own `contextMenuEvent` when the widget did not take the press, or at
`tgw_renderer.cpp`'s 150 ms answer deadline; a press the widget took builds
no menu at all. `OpenChatListContextMenu` reads such a click `menu-refused`
(no-menu) in the event's turn and sends its leave at once, which clears
`_mouseActive`, the active click handler and `Element::Hovered()` before the
list resolves the point again for the later menu. Read it in three calls
instead:

- `BeginChatListContextMenu(list, target, point)`: the refusals before
  delivery, the enter, and the context-menu event with the at-build
  observer, exactly as `OpenChatListContextMenu`'s first two steps. No leave,
  so the list's resolution gate stays open for the later menu's own
  `mouseActionUpdate(e->globalPos())`. A refused request is final.
- `PollChatListContextMenu(reading)`, on a later turn: for a `pending()`
  reading whose underlying reading is `no-menu`, a
  [`Test::RetakeContextMenu`](test_menu.md#menus-built-on-a-later-turn), and
  in the poll that finds a fresh menu `deferred`, the at-build resolution and
  the list's own `_menu` read again, in that turn. It answers whether the
  wait is over: true once a menu or a `several-menus` refusal was found, or
  when no menu can come (nothing pending, no event sent, the list gone);
  false otherwise. Each poll of a `no-menu` reading sets `waited`, the ms
  since `sentAt`.
- `EndChatListContextMenu(reading)`: the leave, the decision and the hide of
  a refused menu, exactly as `OpenChatListContextMenu`'s third step and
  decision. A reading that is not `pending()` is left as it is.

`OpenChatListContextMenu` is `Begin` then `End` in one call, so its readings,
its self-test and every caller that reads a menu built inside the event are
unchanged (`deferred` false, `waited` 0).

### Preconditions and limits

- The caller owns the timer and the bound. A menu that has not come within
  the product's deadline never comes for that press, so a bound of the
  deadline plus slack decides it; `Test::ConformancePointer` polls every
  5 ms for 250 ms ([timed pointer
  sequences](test_widget_conformance.md#timed-pointer-sequences)).
- Send the list nothing between `Begin` and `End`: the list stays entered
  (`_mouseActive` true) until `End`, and the attribution of a deferred menu
  rests on the resolution its replayed event left. The product may resolve
  again in between - a widget's cursor answer re-runs `mouseActionUpdate()`
  through `Data::Session::notifyViewLayoutChange` - at the same stored
  point (`_mousePosition`), which keeps that resolution.
- A deferred reading's `atBuild` resolution and `listMenu` are read in the
  poll turn that found the menu, not inside the call that built it; the
  gate is still judged on `afterEnter`, the state the original event was
  delivered into. The underlying reading's `atBuild` lifecycle stays the
  event's turn's (no menu) and its `afterSettle` is the finding poll's.
- A poll sees only a menu that is still alive: one the product built and
  closed between two polls reads `no-menu`, so a deferred `no-menu`
  reading is product evidence only beside a positive control read the same
  way, and on a locked Windows console, where Qt closes a shown menu before
  the next turn, it is undecidable.
- Always call `End`, also after the bound passed with no menu, which
  decides the reading `menu-refused` (no-menu) as the one-call reader would.
  A reading dropped without `End` leaves the list entered.

### Minimal use

```cpp
// The event's turn:
state->menu = Test::BeginChatListContextMenu(list, item, point);
state->until = crl::now() + kMenuBound;
// Each later turn, until it answers true or the bound passed:
const auto over = Test::PollChatListContextMenu(state->menu)
	|| (crl::now() >= state->until);
if (over) {
	Test::EndChatListContextMenu(state->menu);
	Test::Check(
		state->menu.built(),
		u"right click: the message menu, attributed"_q,
		Test::ChatListMenuDetails(state->menu));
	// Trigger an entry or hide the menu here, in this same turn.
}
```

### Readings and refusals

The refusals are `OpenChatListContextMenu`'s. `ChatListMenuDetails` prints
`deferred` and `waited` beside the other fields, and the underlying reading's
`retakes`. A `built()` reading with `deferred` true is a menu the list built
on a later turn while it resolved exactly the target; `deferred` false with
`waited` above zero is a wait that ended with no menu, and both at zero a
reading that was never polled.

### Lifetime and cleanup

`Begin` leaves the list entered and `End`'s leave is the cleanup, so a
caller whose wait can be cut short (a stage timeout, the watchdog) also
ends a pending reading from `Runner::onFinish`; `End` is idempotent. A
`built()` reading's menu is open: trigger an entry or hide it
(`Ui::PopupMenu::hideMenu(true)`) in the turn `End` returned in. A refused
reading's menu was hidden by `End`.

### Verification

`AppendChatListMenuSelfTest` reads only menus built inside the event, through
`OpenChatListContextMenu`, so it covers `Begin` and `End` as one call. The
later-turn path has no self-test of its own: the conformance runner's right
click on a widget card (`auto/16-sdk-taps-b`, whose element claims nothing)
reads a deferred, built and hidden menu with `waited` under the bound. That
run's results belong to its task evidence.

## Related guidance

[Input contracts](test_widgets.md#input-helpers); [failure diagnosis](../diagnostics.md).
