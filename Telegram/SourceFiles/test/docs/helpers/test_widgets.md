# test_widgets

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_widgets.h](../../test_widgets.h).

## Use when

Typed discovery, live publications and input events. Read the contract below and the source header before use.

## Contract and usage

Safe typed discovery, live object/action publication, input, postponed-call
settlement, QPA-injected window activation, wheel delivery along Qt's parent
ladder with a named `WheelDelivery` refusal, and clipboard-free text drop into
a `Ui::InputField` with a named `TextDrop` refusal.

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

Drive a top-level `Ui::RpWindow` through that static type, never through a
`QWidget*` or a `QPointer<QWidget>`. `Ui::RpWindow` (`rp_window.h`)
declares `setMinimumSize()`, `setFixedSize()`, `setGeometry()`,
`showFullScreen()`, `showNormal()` and `close()` without `virtual` and
forwards each to its `Ui::Platform::BasicWindowHelper` (`rp_window.cpp`).
The `QWidget` members of the same names are non-virtual too, so these six
only hide them and the static type picks the function: through a
`QWidget*` the call compiles, runs the `QWidget` member and silently skips
lib_ui's platform window helper. Keep the type the product hands out —
`Calls::Group::Panel::window()`, `Calls::Panel::window()` and
`Calls::Window::window()` return `not_null<Ui::RpWindow*>` — and hold it
across stages as `QPointer<Ui::RpWindow>`. A window found through
`QApplication::topLevelWidgets()` or `QWidget::window()` is a `QWidget*`:
`dynamic_cast<Ui::RpWindow*>` it before the call, and treat a null cast of
the window a stage means to drive as a fixture gate, never as a reason to
call through the `QWidget*`. Every other `Ui::RpWindow` forward, such as
`setTitle()` or `setStaysOnTop()`, has no `QWidget` namesake, so a
`QWidget*` call to it does not compile; a member `Ui::RpWindow` does not
declare, such as `showMaximized()`, `windowState()` or `geometry()`, is
the same `QWidget` member through either type. The full-screen row under
[failure diagnosis](../diagnostics.md) gives the skip's measured consequence on Windows.

## Input helpers

All input helpers synchronously dispatch real Qt events and drain
`Ui::PostponeCall` work after each event. They lifetime-guard the target and
stop if it is destroyed. `AppendClipboardRoundTrip` sits beside `DropText`
below because the two answer the clipboard question from opposite sides, but
it is a stage, not an event helper: it yields to the event loop between its
attempts.

| Helper | Use |
| --- | --- |
| `Click` | Left press and release at the center or a supplied local point, then a `QEvent::Leave` to that same widget, so `Ui::AbstractButton::isOver()` is false on it and a `RoundButton` repaints its normal `textBg`. Send it the widget that accepts the press — an ignored press propagates to the ancestor that takes it and the leave does not, so a click aimed at a non-accepting child leaves that ancestor hovered. |
| `TypeText` | Key press/release per Unicode grapheme; surrogate pairs and joined emoji stay intact. Send it to the widget that owns the key route: for a `Ui::InputField` that is `rawTextEdit()`, never the wrapper. |
| `CommitText` | One `QInputMethodEvent` commit when insertion, custom emoji, or IME semantics matter more than physical keys. Same target rule as `TypeText`, and the fallback when key events left the field empty. |
| `DropText` | Hand a text to a `Ui::InputField` the way a paste does, with no system clipboard: a `QMimeData` carried by a `QDragEnterEvent` and then a `QDropEvent`, both sent to `rawTextEdit()->viewport()`, which ends in `canInsertFromMimeDataInner` / `insertFromMimeDataInner` and so in the field's `setMimeDataHook` (or its own insertion when the hook leaves it), the insertion Ctrl+V ends in. The drop is sent only when the field's own editor accepted the enter. An ignored enter climbs to the first enabled ancestor that accepts drops, such as a chat's file-drop area, so for the pair an application filter withholds every drag event Qt would hand any other widget, and the returned `TextDrop` lists them in `shielded`. It settles once after the pair, not after each event. A miss is a named `refusal`: `viewport-refuses-drops`, `enter-not-accepted` or `drop-not-accepted`. The text goes in at the caret and does not replace a selection. It does not exercise the clipboard read of a Paste accessory or of Ctrl+V. `InputField::dropEventInner` raises and activates the field's window afterwards (on macOS this asks for the app to come forward, which the window server may grant or refuse after a default background `test-run` launch; a never-shown top level is unaffected). `TextDropDetails` never prints the text. Confirm with `test_text_drop.h`. |
| `AppendClipboardRoundTrip` | A check whose subject is the system clipboard itself - a copy entry's result, or a Paste accessory's or Ctrl+V's own read - first needs a host whose clipboard reads back. One stage (`test_clipboard.h`) writes a sentinel carrying the caller's label (one token, no spaces) and reads it back, up to 10 times 300 ms apart across event-loop turns, logging every attempt as a `CLIPBOARD_ROUND_TRIP_ATTEMPT` row; read the returned `ClipboardRoundTrip` from a later stage: `succeededAt`, or a named refusal saying what the last read held (`read-empty`, `read-stale-sentinel`, `read-foreign`). Judge a product copy afterwards with `ReadClipboard(expected)`, one classified reading (`expected` / `empty` / `harness-sentinel` / `foreign`) with its `owns` reading, never a wait. Text the harness did not write is never printed, measured or hashed, and the owner's clipboard is overwritten and never saved or restored. For what a field does with pasted text, `DropText` stays the route. Confirm with `AppendClipboardRoundTripSelfTest`. |
| `PressKey` | Escape, Return, arrows, shortcuts, and other key behavior. Send to the widget that owns the event route, often the raw editor (`Ui::InputField::rawTextEdit()`) or the top `Ui::BoxLayerWidget`, not a wrapper. |
| `Drag` | Left-button press, interpolated moves with `buttons()==LeftButton`, and release, then the same `QEvent::Leave` as `Click`. |
| `AppendQueuedStartDrag` | A drag whose start the target queues - both message lists, the shared-media list and the chats list's drag onto a folder queue `performDrag()` with `InvokeQueued`, `Ui::FlatLabel`'s link and selection drag queues `executeDrag()` the same way, and `Drag` releases before that call runs (Failure diagnosis). One stage (`test_queued_drag.h`) resolves the target and its two points when it begins, delivers the press and the interpolated moves exactly as `Drag` does, posts an `InvokeQueued` marker to the target and a `crl::on_main` marker, yields to the event loop until both ran - so every normal-priority queued call and every `crl::on_main` call posted on the main thread while the press and moves were handled has run - and only then delivers the release and the same `QEvent::Leave`. It is a stage, not an event helper. Its wait is bounded (`kQueuedStartDragBound`, 3000 ms, inside the stage's timeout), and a miss is a named refusal on the returned `QueuedStartDrag`, read from a later stage: `no-target`, `target-destroyed` (no release is sent) or `bound-expired` (the release and the leave are still sent, so nothing stays pressed). A call those calls post in turn, a timer and a network answer still need a condition wait. It intercepts no platform drag, and every queued start that reaches `QDrag::exec()` starts a real drag that drops at the real cursor: before launching a scenario, hook the function whose `QDrag::exec()` its queued start reaches - `Window::MainWindow::launchDrag()` or `Ui::FlatLabel::executeDrag()` - as its first statement, returning before `new QDrag`, as the Failure diagnosis row and the header say. `Drag` itself is unchanged. Confirm with `AppendQueuedStartDragSelfTest`. |
| `Wheel` | A real wheel event at the named widget's local point (`QPoint(0, -120)` is one conventional downward step). A stack-built `QWheelEvent` is not spontaneous, so Qt 6 does not propagate it; the helper re-sends up `parentWidget()` until a widget handles and accepts — the scroll area's viewport, never the `QAbstractScrollArea` itself — honouring `isWindow()` and `Qt::WA_NoMousePropagation`. Returns `WheelDelivery`; a miss is a named `refusal`, not a silent no-op. Confirm with `test_wheel.h`. |
| `OpenContextMenu` | A context menu (`test_menu.h`): deliver a `QContextMenuEvent` (Mouse by default, Keyboard allowed) at a point in the receiving widget's coordinates and, in the same call, read the `Ui::PopupMenu` it built - its ordered non-separator action texts and its lifecycle at build and after the postponed calls settled - as a `ContextMenuReading`, or a named refusal: `no-menu`, `several-menus`, `receiver-gone`. The snapshot spans the whole application, so a menu the handler parents to another widget is found, and the reading holds the menu as a `QPointer`, so it stays printable after the menu dies. The helper does not hover: hover first when the product's handler reads the hovered link. On a chat list - `HistoryInner` or `HistoryView::ListWidget` - read with `OpenChatListContextMenu` (the next row) instead: in an inactive window this helper alone may read a menu the list built for another message. `TriggerContextMenuAction(reading, text)` triggers the one entry with exactly that text and delivers its queued `Ui::Menu::CreateAction` callback before it returns, without hiding the menu; it refuses `menu-gone`, `no-such-action`, `several-actions`, `action-disabled` and `submenu-entry`. Neither grabs pixels: a frame stays with `CapturePopupMenu` while the menu is still open. Never poll for a menu on later turns, and count a `no-menu` reading only beside a positive control the same helper built in the same widget (Failure diagnosis). Confirm with `AppendContextMenuReaderSelfTest`. |
| `OpenChatListContextMenu` | A context menu on a chat list (`test_chat_menu.h`), attributed to its target message: `OpenChatListContextMenu(list, target, point)` takes a `HistoryInner` or `HistoryView::ListWidget`, the target `HistoryItem` and a point in the list's coordinates, and returns a `ChatListMenuReading` that is `built()` only when the menu was built while the list resolved exactly that message - otherwise a named refusal, never a reading whose texts merely look valid. Both lists resolve the point in `mouseActionUpdate()`, which returns at once while `!_mouseActive && !window()->isActiveWindow()` (on `HistoryInner` also while `hasPendingResizedItems()`), so in an inactive window `OpenContextMenu` alone reads a menu built for whatever message the list resolved last (Failure diagnosis). The reader activates no window - activation can mark the shown messages read - and opens the gate's other operand for its own call only: a `QEnterEvent` to the list inside `Settle` (`enterEventHook` sets `_mouseActive` and resolves the real `QCursor::pos()`, which is never attributed), then `OpenContextMenu` with an at-build observer that reads the list's resolution inside the delivery, after `showContextMenu`'s own `mouseActionUpdate(e->globalPos())` and before any postponed call drained, then always a `QEvent::Leave` while the list survives, after which `_mouseActive` must read false. Each `ChatListResolution` - before the enter, after it, at build and after the leave - holds the resolved view's message and identity (`Element::Moused()` / `_overElement`), `Element::Hovered()` and `Element::HoveredLink()` on `HistoryInner`, the exact item (`_dragStateItem` / `_overItemExact`), the message the menu takes its entries from, `mouseActive`, `activeWindow`, `pendingResize` and the gate, read through file-private read-only accessors. A reading is accepted only when the gate was open at delivery, at build the resolved view is the list's own view of the target and every one of those candidates names the target or nothing, and the fresh menu is the list's own `_menu`. Refused before anything is sent: `no-list`, `not-a-chat-list` (also a `ListWidget` with no window controller, the chat preview's), `no-target`, `foreign-target`, `outside-viewport`, `pending-resize` (in a window that gets no paints, the minimized one `KeepMainWindowNotMarkingRead` keeps, nothing recounts that layout until the next `Data::Session::sendHistoryChangeNotifications()`, and `History::addNewItem` leaves one waiting: process it first, outside any `until` and only while the window cannot mark read, with `notifyHistoryChangeDelayed(history)` and `sendHistoryChangeNotifications()`, which `HistoryWidget::handleHistoryChange` answers by recounting it, as the self-test does) and `ambiguous-target` (an album, or a selection already in the list); after the delivery: `list-gone`, `target-gone`, `menu-refused`, `unresolved`, `wrong-target` (the resolved message is in the details), `ambiguous-target`, `foreign-menu` and `mouse-still-active`. A refusal after the delivery hides the one menu this call built and clears `reading.menu.menu`, so `TriggerContextMenuAction(reading.menu, text)` refuses `menu-gone`; trigger only from a `built()` reading, in the same turn, where "Select" selects exactly the target. After the leave `Element::Moused()` and `_overElement` keep the last resolved view: that is not hover, and nothing claims to clear them. `ChatListMenuDetails` prints the reading; neither function Notes or Fails. Confirm with `AppendChatListMenuSelfTest`. |
| `Settle` | Wrap programmatic mutations such as `InputField::setText`; drains postponed fixups after the action. |
| `ForceWindowActive` | Inject window activation through the QPA seam before any focus-routed action, and re-assert it on every poll of a bounded wait. On a locked console, or on macOS after a default `test-run` launch until something activates the client, the window is never active in Qt's sense, so `setFocus()` never reaches `QApplication::focusWidget()` and every `hasFocus()`/`isActiveWindow()` branch silently reads false. |
| `KeepMainWindowNotMarkingRead` | Minimize the primary window and poll until `MainWindow::markingAsRead()` is false with the window minimized and not hidden, or until `kNotMarkingReadBound` returns a named refusal. Call it again after `Window::Controller::activate()` or any `showHistory` that is not `anim::activation::background`, which clear the minimized bit. A control that cannot read true within `kNotMarkingReadBound` - a still-unexposed window, or a locked console that keeps it false - must not be reported as success; the self-test exposes the primary window itself with `KeepWindowExposed` from its control until its restore stage. Confirm with `test_marking_read.h`. |
| `KeepWindowExposed` | Stack a covered top-level window above ordinary windows - on macOS `Qt::WindowStaysOnTopHint` on its `QWindow`, never `QWidget::setWindowFlag` - without activating the app, changing the focus window or taking focus, so a window another application covers is exposed and paints again. It never waits: wait for `ReadWindowExposure(...).exposed` (`isExposed()`) in a pure `until`. Undo with `RestoreWindowExposure` before teardown. It refuses a minimized window by name (stacking cannot expose it), so it cannot undo `KeepMainWindowNotMarkingRead`; off macOS it is a no-op that still returns its reading. Confirm with `test_window_exposure.h`. |
| `ClickBoxButton` | Click a box's shell footer button by label. `addButton` re-parents the button onto the `Ui::BoxLayerWidget`, so a content-rooted `FindAll<Ui::RoundButton>` never sees it; this roots the search at `Test::PaintingLayerRoot(box)`, keeps only its direct children, and refuses by name when nothing matches. |

The harness has no pointer. No helper leaves a hover behind, and every
measurement is taken as if no cursor exists — assert a hovered state by
setting it deliberately, not by relying on a click's residue. The one
helper that enters a widget, `OpenChatListContextMenu`, does so only for
its own call: its `QEnterEvent` sets the chat list's `_mouseActive` (the
list's `enterEventHook` also resolves the real `QCursor::pos()`, which the
reader never attributes), and its closing `QEvent::Leave` clears
`_mouseActive`, the active click handler and, on `HistoryInner`,
`Element::Hovered()` again, while `Element::Moused()` and `_overElement`
keep the last resolved view, which is not hover. A press also
starts a `RippleButton` ripple that decays over its style's
`showDuration + hideDuration`; finish it with `finishAnimating()` or wait
it out before measuring a fill.

Timers, queued invokes, animations, network callbacks, and reactive streams
still require condition waits. `Settle` is not a replacement for readiness.

## Related guidance

[Launch behavior](../launch.md); [chat-list resolution](../chat-lists.md); [failure diagnosis](../diagnostics.md).
