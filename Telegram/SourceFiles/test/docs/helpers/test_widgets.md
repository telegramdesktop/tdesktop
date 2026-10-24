# test_widgets

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_widgets.h](../../test_widgets.h).

## Use when

Typed discovery, a button's own disabled state, live publications and input events. Read the contract below and the source header before use.

## Contract and usage

Safe typed discovery, a `Ui::AbstractButton`'s own disabled flag read beside
`QWidget::isEnabled()` (`ReadButtonState`, the readiness of a button target),
live object/action publication, input, postponed-call settlement,
QPA-injected window activation, wheel delivery along Qt's parent ladder with
a named `WheelDelivery` refusal, clipboard-free text drop into a
`Ui::InputField` with a named `TextDrop` refusal, and a press-point ripple
origin for a click (`ClickWithPressOrigin`, a `PressOrigin` reading with named
refusals, printed by `PressOriginDetails`).

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
Test::PublishLiveWidget(u"gift.send"_q, sendButton);
Test::PublishLiveAction(
	u"gift.confirm"_q,
	box,
	[=] { confirm(); });

// In test_scenario.cpp.
runner->actOnWidget(
	u"click the published send button"_q,
	[] { return Test::ReadLiveWidget(u"gift.send"_q).widget; },
	[](QWidget *widget) { Test::Click(widget); },
	[](QWidget *widget) {
		return Test::ReadButtonState(widget).usable();
	},
	Test::kDefaultStageTimeout,
	[](QWidget *widget) {
		return Test::ButtonStateDetails(Test::ReadButtonState(widget));
	});
```

The readiness is the button's own state, not `isVisible() && isEnabled()`:
a button the product blocked with `setDisabled()` still answers
`isEnabled()` true ([below](#a-buttons-own-disabled-state)). Publish the
button itself, not a wrapper around it, because `ReadButtonState` refuses a
widget that is not a `Ui::AbstractButton`.

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

## A button's own disabled state

`Ui::AbstractButton::setDisabled(bool)` (`ui/abstract_button.h`) sets the
button's own `StateFlag::Disabled`, which only `isDisabled()` reads back.
`QWidget::isEnabled()` is untouched, so a button the product blocks - a
Move button behind `setDisabled(busy || !pressable)` - still answers
`isEnabled()` true, and `isVisible() && isEnabled()` reads it ready. The
member is not virtual and does not override Qt's `QWidget::setDisabled`
slot; it hides it, as the `Ui::RpWindow` members above hide theirs. Called
through a `QWidget*`, the Qt slot runs instead: it moves `isEnabled()` and
leaves the own flag clear. Drive a button through its own static type and
read it with `Test::ReadButtonState(widget)` (`test_widgets.h`):

| Field | Reading |
| --- | --- |
| `disabled` | `isDisabled()`, the product's own block |
| `enabled` | `QWidget::isEnabled()`, the Qt-level control read beside it |
| `visible` | `QWidget::isVisible()` |
| `identity` | The handed widget's `WidgetDescription`, taken in that call |
| `refusal` | Empty on a read button |

The three flags are read independently in one call. `usable()` needs a read
button that is visible, enabled and not disabled; it is the readiness of a
button target in `until` or `actOnWidget`, as in the example above. A
disabled but visible button is a product state: assert `disabled` in
`then`, with `isEnabled` printed beside it, rather than waiting on it.

Refusals make `read()` false and the reading never `usable()`, and on them
`disabled` is not read:
`no widget was handed to the button-state reading` and
`not a Ui::AbstractButton, so it carries no own disabled flag: <identity>`.
`ButtonStateDetails` prints
`button=<identity> ownDisabled=<0|1|none> isEnabled=<0|1> visible=<0|1> usable=<0|1>`,
then ` - <refusal>` on a refused reading; pass it as a check's `details` so
both verdicts print it. The reading logs nothing, holds no pointer and needs
no cleanup. `ReadBoxButtons` reads each shell footer button through it
([test_box_button](test_box_button.md)); for those, keep using
`BoxButtonReady` / `BoxButtonDisabled` / `BoxButtonHidden`.

Verification: `AppendButtonStateSelfTest` (`test_box_button.h`) toggles a
real `Ui::RoundButton` on the primary window. `setDisabled(true)` reads
`ownDisabled=1 isEnabled=1 usable=0` and `setDisabled(false)` reads
`ownDisabled=0 isEnabled=1 usable=1`; `QWidget::setEnabled(false)` moves
only the control column (`ownDisabled=0 isEnabled=0`); a null widget and
the primary window's widget refuse by name. It has no deliberate FAIL.

## Input helpers

All input helpers synchronously dispatch real Qt events and drain
`Ui::PostponeCall` work after each event. They lifetime-guard the target and
stop if it is destroyed. `AppendClipboardRoundTrip` sits beside `DropText`
below because the two answer the clipboard question from opposite sides, but
it is a stage, not an event helper: it yields to the event loop between its
attempts.

Every helper that delivers input does so inside `Settle`, which holds a
`Test::HarnessInputScope` around its action and closes it before the drain.
While the opt-in [console shield](test_console_shield.md) is armed, that
scope, not an event's spontaneous flag, makes input harness input: whatever is
delivered inside it, at its event-loop level, passes, whether a helper's sent
events, the shortcut or input-method events they produce, or the spontaneous
deliveries of [test_qpa_mouse](test_qpa_mouse.md). Outside `Settle`, or in an
event loop nested inside the delivery, a spontaneous event of a class the
shield lists ([classes](test_console_shield.md#preconditions-and-limits)), and
any shortcut, input-method or drop event, is console input and vetoed. Wrap an
overlay's own raw `sendEvent` or `QWindowSystemInterface` delivery in
`Settle`, and call `ForceWindowActive` outside it. Nothing changes while the
shield is not armed.

| Helper | Use |
| --- | --- |
| `Click` | Left press and release at the center or a supplied local point, then a `QEvent::Leave` to that same widget, so `Ui::AbstractButton::isOver()` is false on it and a `RoundButton` repaints its normal `textBg`. Send it the widget that accepts the press — an ignored press propagates to the ancestor that takes it and the leave does not, so a click aimed at a non-accepting child leaves that ancestor hovered. |
| `ClickWithPressOrigin` | `Click` whose press starts the pressed button's own ripple at the press point, as a real pointer press there would - for press-feedback frames. A plain `Click` cannot: `Ui::RippleButton::onStateChanged` starts the press ripple at `prepareRippleStartPosition()`, which every class computes as `mapFromGlobal(QCursor::pos())` on itself plus its own offset and guard, so the ripple starts where the owner's real mouse is - usually far outside the button, so frames about 40 and 120 ms after the press see nothing inside it - or the class's guard (`Ui::IconButton`'s ripple area) refuses that point and starts none. Inside one `Settle` action the helper reads `QCursor::pos()`, moves the target alone with `QWidget::move` by its floating `mapFromGlobal(QPointF(cursor))` minus the point (each axis rounded, because a displacement taken from the already rounded `QPoint` mapping lands a pixel off on a half-pixel high-DPI window), verifies that the target's own `mapFromGlobal(QCursor::pos())` now reads the point, delivers `Click`'s own press (local the point, global the target's real `mapToGlobal(point)`, read before the move) and moves the target back - all before `Settle` drains the postponed calls; the release, at the restored position, and the closing `QEvent::Leave` are `Click`'s. A non-native child's own geometry is part of its global mapping up to the first `QWindow`, so this works whatever a platform sources `QCursor::pos()` from, and the real cursor is only read, never set. Only the origin is substituted: the ripple's mask, colour, timing and paint and the class's own offset and guard stay the button's, and nothing is recomputed per class, so an `IconButton` pressed inside the button but outside its ripple area starts no ripple, as a pointer press there would. It returns a `PressOrigin` and neither Notes nor Fails. A refusal is named and sends nothing; the first four move nothing: `top-level-target` (moving a window moves a real OS window), `native-target` (the target, or a descendant inside its window, has `internalWinId()` or `Qt::WA_NativeWindow` - a separate window parented under the target is skipped, since it does not move with it - and `reason` names that widget), `disabled-target` (its mouse events would propagate to an ancestor), `point-outside` (no pointer press there reaches this widget) and `origin-unreachable` (after the move the target still does not map the cursor to the point - a transformed ancestor, a cursor that moved between the two reads, or a point coordinate of 0 on an exact half pixel; the target is moved back first). Judge the reading `PressOriginDetails` prints on one line: `refusal=none`, `originSeen` (the target's `mapFromGlobal(QCursor::pos())` right before the press, the expression the classes evaluate) equal to `point`, `restored=1`, `cursorStable=1` (`cursorBefore` equals `cursorAfter`), `pressAccepted=1`, `delivered=1` (press, release and leave each reached a live target), `geometryChangedByPress=0` (1 when the press handler moved or resized the target: a moved target keeps the handler's position, its `Qt::WA_Moved` and `Qt::WA_PendingMoveEvent` included, and a resized one keeps its new size and still gets its position back), `moveEventsWithheld` (2 for a visible displaced target) and `paintsWhileDisplaced=0` (1 or more only when the press handler itself calls `repaint()` or `processEvents()`). For the press only, an event filter installed on the target last withholds the target's own two `QEvent::Move` - so `RpWidget::geometryValue()` owners and earlier object filters never see the transient move, while a move the press handler makes passes - `Qt::WA_Moved` and `Qt::WA_PendingMoveEvent` are restored, and `Qt::WA_NoMousePropagation` is set, so a press the target does not accept stops at the target instead of reaching its parent. Caveats: during the press the target's and its descendants' own `pos()`, `geometry()`, `mapToGlobal()` and `mapFromGlobal()` read the displaced frame - that is the substitution - so a handler that places a popup from the target's `mapToGlobal()` on press places it at the real cursor; application-level event filters still see the two moves, an object filter the press handler installs on the target during the press runs before the facility's and sees the move back, and accessibility gets two `LocationChanged` notifications while a client is connected. Hover caveat: a harness press shows the ripple but no hover highlight, because the helper's closing leave cancels it before it paints - `IconButton`'s hover animation restarts from 1 to 0 at its still-zero value and `RoundButton`'s `isOver()` is false again before any paint - so press frames measure the ripple alone. Not for drags: `Drag` and `AppendQueuedStartDrag` keep the cursor-derived origin, because a drag start can reach a nested `QDrag::exec()` loop that must never run while a target is displaced. Confirm with `AppendPressOriginSelfTest` (`test_press_origin.h`). |
| `TypeText` | Key press/release per Unicode grapheme; surrogate pairs and joined emoji stay intact. Send it to the widget that owns the key route: for a `Ui::InputField` that is `rawTextEdit()`, never the wrapper. |
| `CommitText` | One `QInputMethodEvent` commit when insertion, custom emoji, or IME semantics matter more than physical keys. Same target rule as `TypeText`, and the fallback when key events left the field empty. |
| `DropText` | Hand a text to a `Ui::InputField` the way a paste does, with no system clipboard: a `QMimeData` carried by a `QDragEnterEvent` and then a `QDropEvent`, both sent to `rawTextEdit()->viewport()`, which ends in `canInsertFromMimeDataInner` / `insertFromMimeDataInner` and so in the field's `setMimeDataHook` (or its own insertion when the hook leaves it), the insertion Ctrl+V ends in. The drop is sent only when the field's own editor accepted the enter. An ignored enter climbs to the first enabled ancestor that accepts drops, such as a chat's file-drop area, so for the pair an application filter withholds every drag event Qt would hand any other widget, and the returned `TextDrop` lists them in `shielded`. It settles once after the pair, not after each event. A miss is a named `refusal`: `viewport-refuses-drops`, `enter-not-accepted` or `drop-not-accepted`. The text goes in at the caret and does not replace a selection. It does not exercise the clipboard read of a Paste accessory or of Ctrl+V. `InputField::dropEventInner` raises and activates the field's window afterwards (on macOS this asks for the app to come forward, which the window server may grant or refuse after a default background `test-run` launch; a never-shown top level is unaffected). `TextDropDetails` never prints the text. Confirm with `test_text_drop.h`. |
| `AppendClipboardRoundTrip` | A check whose subject is the system clipboard itself - a copy entry's result, or a Paste accessory's or Ctrl+V's own read - first needs a host whose clipboard reads back. One stage (`test_clipboard.h`) writes a sentinel carrying the caller's label (one token, no spaces) and reads it back, up to 10 times 300 ms apart across event-loop turns, logging every attempt as a `CLIPBOARD_ROUND_TRIP_ATTEMPT` row; read the returned `ClipboardRoundTrip` from a later stage: `succeededAt`, or a named refusal saying what the last read held (`read-empty`, `read-stale-sentinel`, `read-foreign`). Judge a product copy afterwards with `ReadClipboard(expected)`, one classified reading (`expected` / `empty` / `harness-sentinel` / `foreign`) with its `owns` reading, never a wait. Text the harness did not write is never printed, measured or hashed, and the owner's clipboard is overwritten and never saved or restored. For what a field does with pasted text, `DropText` stays the route. Confirm with `AppendClipboardRoundTripSelfTest`. |
| `PressKey` | Escape, Return, arrows, shortcuts, and other key behavior. Send to the widget that owns the event route, often the raw editor (`Ui::InputField::rawTextEdit()`) or the top `Ui::BoxLayerWidget`, not a wrapper. |
| `Drag` | Left-button press, interpolated moves with `buttons()==LeftButton`, and release, then the same `QEvent::Leave` as `Click`. |
| `AppendQueuedStartDrag` | A drag whose start the target queues - both message lists, the shared-media list and the chats list's drag onto a folder queue `performDrag()` with `InvokeQueued`, `Ui::FlatLabel`'s link and selection drag queues `executeDrag()` the same way, and `Drag` releases before that call runs (Failure diagnosis). One stage (`test_queued_drag.h`) resolves the target and its two points when it begins, delivers the press and the interpolated moves exactly as `Drag` does, posts an `InvokeQueued` marker to the target and a `crl::on_main` marker, yields to the event loop until both ran - so every normal-priority queued call and every `crl::on_main` call posted on the main thread while the press and moves were handled has run - and only then delivers the release and the same `QEvent::Leave`. It is a stage, not an event helper. Its wait is bounded (`kQueuedStartDragBound`, 3000 ms, inside the stage's timeout), and a miss is a named refusal on the returned `QueuedStartDrag`, read from a later stage: `no-target`, `target-destroyed` (no release is sent) or `bound-expired` (the release and the leave are still sent, so nothing stays pressed). A call those calls post in turn, a timer and a network answer still need a condition wait. It intercepts no platform drag, and every queued start that reaches `QDrag::exec()` starts a real drag that drops at the real cursor: before launching a scenario, hook the function whose `QDrag::exec()` its queued start reaches - `Window::MainWindow::launchDrag()` or `Ui::FlatLabel::executeDrag()` - as its first statement, returning before `new QDrag`, as the Failure diagnosis row and the header say. `Drag` itself is unchanged. Confirm with `AppendQueuedStartDragSelfTest`. |
| `QpaMouseSequence` | Mouse press, move and release through Qt's platform layer (`test_qpa_mouse.h`) when Qt's routing is the subject: Qt picks the receiver and sends the held moves and the release to the widget that called `grabMouse()` or took the press, where `Click` and `Drag` send every event to the named widget. Points are in the target's logical pixels and converted to native ones; each delivery runs inside `Settle`, is spontaneous and returns a reading or a named refusal; the caller's `end()` releases what the sequence holds, its grab and its hover. It never activates a window. Read [test_qpa_mouse](test_qpa_mouse.md). |
| `AppendQpaMouseDrag` | The same delivery as one drag stage for a widget that grabs the mouse, such as `CollectiblesGrid`: press, interpolated held moves (optionally paced), the queued-work wait of `AppendQueuedStartDrag` before the release (bounded; `bound-expired` still releases), `end()` and one `QPA_MOUSE_DRAG` reading. The real-`QDrag` hook obligation above applies. Confirm with `AppendQpaMouseSelfTest`. |
| `Wheel` | A real wheel event at the named widget's local point (`QPoint(0, -120)` is one conventional downward step). A stack-built `QWheelEvent` is not spontaneous, so Qt 6 does not propagate it; the helper re-sends up `parentWidget()` until a widget handles and accepts — the scroll area's viewport, never the `QAbstractScrollArea` itself — honouring `isWindow()` and `Qt::WA_NoMousePropagation`. Returns `WheelDelivery`; a miss is a named `refusal`, not a silent no-op. Confirm with `test_wheel.h`. |
| `TouchSequence` | Touch input through Qt's platform layer (`test_touch.h`), from a touchscreen and a touchpad the harness registers once per process: the first `HarnessTouchDevice(TouchDeviceKind::Screen \| Pad)` call registers both - after `QPointingDevice::primaryPointingDevice()`, as lib_ui's `TouchpadDevice()` does, so the pad never becomes the default mouse - and they are never replaced, not after a cancel and not for another leg. A `TouchSequence(target, kind)` is one gesture bound to `target`, whose local logical coordinates every position is in. `send(changes)` delivers one complete frame: the listed `TouchContact`s in the caller's order, then every other contact of the sequence still down, in press order, as `Stationary`; a `Released` contact without a position is released where it last was. Positions go through `QWidget::mapToGlobal(QPointF)`, `QWindowSystemInterfacePrivate::toNativeTouchPoints` and `QWindowSystemInterface::handleTouchEvent<SynchronousDelivery>` to the target's top-level `QWindow`, so Qt's own routing, implicit grabs and mouse synthesis apply and nothing is scaled or rounded by hand; `cancel()` is `handleTouchCancelEvent` with no points. Each submission runs inside `Settle`; the target and its `QWindow` are `QPointer`s, and losing either - in the dispatch, its drain, the call's own cleanup or between two calls - stops the sequence (`stopped()`), which never sends a frame again; every call reads `targetAlive` and `windowAlive` last, after all of its cleanup and drains, so a target or window that a cleanup `Leave` destroyed already reads dead, and has stopped the sequence, when the call returns. The terminal cleanup is the one thing the helper submits unasked: a target that refuses the touch can take the press Qt synthesizes from it and then die, or another contact's widget accepts the frame that releases every contact, so `QGuiApplicationPrivate::processTouchEvent` synthesizes no release for that press at all; neither a `Leave` nor the widget's destruction releases it, so `QGuiApplication::mouseButtons()` would stay `Qt::LeftButton`, and a `Ui::AbstractButton` that took it would stay down and hovered, while the ended sequence refuses the release or cancel that would. So when the sequence stops, or its gesture ends with the last release or a cancel, while its own rows hold a `Qt::MouseEventSynthesizedByQt` press from its device at a `QWindow` that no release from that device followed, and its `QWindow` lives, the call submits one point-free `handleTouchCancelEvent<SynchronousDelivery>` inside `Settle` - for a stop before the ending's `Leave`, for a gesture that only ended after it - for which `processTouchEvent` sends that window the balancing mouse release (`Qt::MouseEventNotSynthesized`, the screen device), handed on to the widget still holding the press or to none, and once every contact was released Qt holds no point to cancel and sends no window a `TouchCancel`, only that release; a `cancel()` of a gesture holding the press makes `processTouchEvent` send that release itself to every window it synthesized mouse in, so the cleanup then finds no press held. It is tried once per sequence; `cleanupCancel` and `cleanupRows` record it; a gesture that ended without a loss stays `finished()` and not `stopped()`, and its next call is refused `finished`; a frame refused for a loss stays unsent, no widget gets an event the helper made up and no other input state is reset. With the window gone as well, Qt has no window to release in and the press stays. A malformed frame is refused `invalid-frame` before anything is sent. Each `TouchFrame` holds the points sent and, from an application filter that never vetoes, every touch event with its receiver (the `QWindow`'s included), every mouse event of any source and device, and every widget `Enter`; `submitted` says only that Qt Gui took the frame, never that a widget accepted it. `owner()` is the accepted-Begin owner, read from the initial Begin climb right after the QPA call and cached past End: `TouchOwnerStatus::Accepted` with the climb's last offered widget when Qt marked it `Qt::WA_WState_AcceptedTouchBeginEvent`, `Refused` when it did not, `None` while no Begin reached a widget, and `Ambiguous` - no widget, its `label` naming the shared one - when a later Begin group of the same frame was offered that widget too: `QApplicationPrivate::translateRawTouchEvent` builds every Begin group of a frame before it sends any and each offer rewrites the mark, so two contacts on two refusing siblings whose common ancestor refuses one Begin and accepts the other leave it set whichever group it refused, and when it accepts first, its second answer unmarks it, so Qt sends it no Update or End of the group it accepted. Never walk up from the first receiver to the first marked widget: after an ancestor accepts, `QApplicationPrivate::translateRawTouchEvent` marks the original target too, so that walk names a widget that refused. The synthesis control: a target with `Qt::WA_AcceptTouchEvents` that ignores every touch makes Qt synthesize `Qt::MouseEventSynthesizedByQt` mouse from the screen, release included, while an accepted screen sequence gives no widget a mouse event or an `Enter`; count a "no synthesized mouse" only beside that control. An all-`Stationary` frame reaches the `QWindow` and no widget, so from the screen Qt synthesizes a `MouseMove` with the left button held from it, which `QApplicationPrivate::pickMouseReceiver` gives to no widget while no button is down; the window then stays in Qt's synthesized-mouse set, and the next touch cancel sends it a mouse release (`Qt::MouseEventNotSynthesized`). A cancel leaves the device's points and their grab targets in `QPointingDevicePrivate::activePoints`, and `QGuiApplicationPrivate::processTouchEvent` drops every frame of any device that is not a `TouchBegin` until the next one. A fresh sequence of the same device and ids on the same receiver recovers; a same-id Begin on another widget is offered there while its Update and End still go to the earlier receiver (`2026/10/20/swipe-the-qr-box-emoji-and-gift-layer-by-touch`), and a held pad point captures every later pad Begin; the helper prevents neither. On macOS `translateRawTouchEvent` drops a one-contact touchpad event for a target without `Qt::WA_TouchPadAcceptSingleTouchEvents`, so drive the pad with two contacts. When a gesture ends - its last release, a cancel or a stop, after a stop's cleanup - every surviving widget that got a mouse event or an `Enter` gets a `QEvent::Leave` (`left`), so no hover stays behind; a widget holding a press keeps its hover through that `Leave` (`Ui::AbstractButton` ignores a `Leave` while down), so when the terminal cleanup's release comes after those `Leave`s, because the gesture ended with the synthesized press still held or their drain destroyed the target while it was, every surviving receiver gets the `Leave` once more after that release, and `left` names it once per pass; Qt's own last-mouse-receiver is not reset. Spontaneity: every frame reaches the `QWindow` spontaneously; a directly hit accepting widget reads a spontaneous Begin, while a widget offered the Begin after the climb passed another reads it non-spontaneous; Update, End and Cancel reach widgets spontaneously; mouse Qt synthesizes is spontaneous and carries the screen device, while `Ui::ScrollArea`'s own tap and scroll mouse carries the primary pointing device and `Qt::MouseEventSynthesizedByApplication`; `Enter` and `Leave` are never spontaneous. No console input shield is on this branch (`2026/10/20/shield-console-input-and-drive-mouse-grabbing-widgets-through-qpa`): an application-wide filter tells this helper's touch, and the mouse Qt synthesizes from it, apart with `IsHarnessTouchDevice(event->device())`, never with `spontaneous()`; `Qt::MouseEventSynthesizedByApplication` says only that a widget made the event, so it identifies neither harness input nor an `Enter`. While `QApplication::activePopupWidget()` is set, `QWidgetWindow::handleTouchEvent` ignores touch and Qt synthesizes mouse instead, and a window a modal window blocks gets no touch (its End arrives as a `TouchCancel`). Preconditions: the GUI thread, no nested delivery (never from an event handler or from a postponed call a delivery triggered), at most one unfinished sequence per device, and every gesture released or cancelled before it is abandoned - a destroyed sequence sends nothing, and Qt keeps holding what it left down. The helper neither Notes nor Fails; `TouchFrameDetails` prints a frame. Confirm with `AppendTouchSelfTest`. |
| `OpenContextMenu` | A context menu (`test_menu.h`): deliver a `QContextMenuEvent` (Mouse by default, Keyboard allowed) at a point in the receiving widget's coordinates and, in the same call, read the `Ui::PopupMenu` it built - its ordered non-separator action texts and its lifecycle at build and after the postponed calls settled - as a `ContextMenuReading`, or a named refusal: `no-menu`, `several-menus`, `receiver-gone`. The snapshot spans the whole application, so a menu the handler parents to another widget is found, and the reading holds the menu as a `QPointer`, so it stays printable after the menu dies. The helper does not hover: hover first when the product's handler reads the hovered link. On a chat list - `HistoryInner` or `HistoryView::ListWidget` - read with `OpenChatListContextMenu` (the next row) instead: in an inactive window this helper alone may read a menu the list built for another message. `TriggerContextMenuAction(reading, text)` triggers the one entry with exactly that text and delivers its queued `Ui::Menu::CreateAction` callback before it returns, without hiding the menu; it refuses `menu-gone`, `no-such-action`, `several-actions`, `action-disabled` and `submenu-entry`. A custom `Ui::Menu::ItemBase` row whose `QAction` has no text, such as `AboutAction`, correctly reads `no-such-action` there: read it by its full accessible name with `ReadMenuItem(reading.menu.data(), name)` and send it real Down/Return keys with `ActivateMenuItem` ([accessible custom rows](test_menu.md#accessible-custom-rows)). Neither grabs pixels: a frame stays with `CapturePopupMenu` while the menu is still open. Never poll for a menu on later turns, and count a `no-menu` reading only beside a positive control the same helper built in the same widget (Failure diagnosis). Confirm with `AppendContextMenuReaderSelfTest`. |
| `OpenChatListContextMenu` | A context menu on a chat list (`test_chat_menu.h`), attributed to its target message: `OpenChatListContextMenu(list, target, point)` takes a `HistoryInner` or `HistoryView::ListWidget`, the target `HistoryItem` and a point in the list's coordinates, and returns a `ChatListMenuReading` that is `built()` only when the menu was built while the list resolved exactly that message - otherwise a named refusal, never a reading whose texts merely look valid. Both lists resolve the point in `mouseActionUpdate()`, which returns at once while `!_mouseActive && !window()->isActiveWindow()` (on `HistoryInner` also while `hasPendingResizedItems()`), so in an inactive window `OpenContextMenu` alone reads a menu built for whatever message the list resolved last (Failure diagnosis). The reader activates no window - activation can mark the shown messages read - and opens the gate's other operand for its own call only: a `QEnterEvent` to the list inside `Settle` (`enterEventHook` sets `_mouseActive` and resolves the real `QCursor::pos()`, which is never attributed), then `OpenContextMenu` with an at-build observer that reads the list's resolution inside the delivery, after `showContextMenu`'s own `mouseActionUpdate(e->globalPos())` and before any postponed call drained, then always a `QEvent::Leave` while the list survives, after which `_mouseActive` must read false. Each `ChatListResolution` - before the enter, after it, at build and after the leave - holds the resolved view's message and identity (`Element::Moused()` / `_overElement`), `Element::Hovered()` and `Element::HoveredLink()` on `HistoryInner`, the exact item (`_dragStateItem` / `_overItemExact`), the message the menu takes its entries from, `mouseActive`, `activeWindow`, `pendingResize` and the gate, read through file-private read-only accessors. A reading is accepted only when the gate was open at delivery, at build the resolved view is the list's own view of the target and every one of those candidates names the target or nothing, and the fresh menu is the list's own `_menu`. Refused before anything is sent: `no-list`, `not-a-chat-list` (also a `ListWidget` with no window controller, the chat preview's), `no-target`, `foreign-target`, `outside-viewport`, `pending-resize` (in a window that gets no paints, the minimized one `KeepMainWindowNotMarkingRead` keeps, nothing recounts that layout until the next `Data::Session::sendHistoryChangeNotifications()`, and `History::addNewItem` leaves one waiting: process it first, outside any `until` and only while the window cannot mark read, with `notifyHistoryChangeDelayed(history)` and `sendHistoryChangeNotifications()`, which `HistoryWidget::handleHistoryChange` answers by recounting it, as the self-test does) and `ambiguous-target` (an album, or a selection already in the list); after the delivery: `list-gone`, `target-gone`, `menu-refused`, `unresolved`, `wrong-target` (the resolved message is in the details), `ambiguous-target`, `foreign-menu` and `mouse-still-active`. A refusal after the delivery hides the one menu this call built and clears `reading.menu.menu`, so `TriggerContextMenuAction(reading.menu, text)` refuses `menu-gone`; trigger only from a `built()` reading, in the same turn, where "Select" selects exactly the target. After the leave `Element::Moused()` and `_overElement` keep the last resolved view: that is not hover, and nothing claims to clear them. `ChatListMenuDetails` prints the reading; neither function Notes or Fails. Confirm with `AppendChatListMenuSelfTest`. |
| `Settle` | Wrap programmatic mutations such as `InputField::setText`, and any raw input delivery an armed console shield must pass; drains postponed fixups after the action, outside its harness-input scope. |
| `ForceWindowActive` | Inject window activation through the QPA seam before any focus-routed action, and re-assert it on every poll of a bounded wait. On a locked console, or on macOS after a default `test-run` launch until something activates the client, the window is never active in Qt's sense, so `setFocus()` never reaches `QApplication::focusWidget()` and every `hasFocus()`/`isActiveWindow()` branch silently reads false. |
| `KeepMainWindowNotMarkingRead` | Minimize the primary window and poll until `MainWindow::markingAsRead()` is false with the window minimized and not hidden, or until `kNotMarkingReadBound` returns a named refusal. Call it again after `Window::Controller::activate()` or any `showHistory` that is not `anim::activation::background`, which clear the minimized bit. A control that cannot read true within `kNotMarkingReadBound` - a still-unexposed window, or a locked console that keeps it false - must not be reported as success; the self-test exposes the primary window itself with `KeepWindowExposed` from its control until its restore stage. Confirm with `test_marking_read.h`. |
| `KeepWindowExposed` | Stack a covered top-level window above ordinary windows - on macOS `Qt::WindowStaysOnTopHint` on its `QWindow`, never `QWidget::setWindowFlag` - without activating the app, changing the focus window or taking focus, so a window another application covers is exposed and paints again. It never waits: wait for `ReadWindowExposure(...).exposed` (`isExposed()`) in a pure `until`. Undo with `RestoreWindowExposure` before teardown. It refuses a minimized window by name (stacking cannot expose it), so it cannot undo `KeepMainWindowNotMarkingRead`; off macOS it is a no-op that still returns its reading. Confirm with `test_window_exposure.h`. |
| `ClickBoxButton` | Click a box's shell footer button by label. `addButton` re-parents the button onto the `Ui::BoxLayerWidget`, so a content-rooted `FindAll<Ui::RoundButton>` never sees it; this roots the search at `Test::PaintingLayerRoot(box)`, keeps only its direct children, and refuses by name when nothing matches. |

The harness has no pointer. No helper leaves a hover behind, and every
measurement is taken as if no cursor exists — assert a hovered state by
setting it deliberately, not by relying on a click's residue. The one
deliberate exception is `AppendShowInChatList` asked for a `hover`: it
leaves its enter and move on the chat list for the next stage's press
([test_chat_show](test_chat_show.md)). The one other
helper that enters a widget, `OpenChatListContextMenu`, does so only for
its own call: its `QEnterEvent` sets the chat list's `_mouseActive` (the
list's `enterEventHook` also resolves the real `QCursor::pos()`, which the
reader never attributes), and its closing `QEvent::Leave` clears
`_mouseActive`, the active click handler and, on `HistoryInner`,
`Element::Hovered()` again, while `Element::Moused()` and `_overElement`
keep the last resolved view, which is not hover. The one helper that
lends a press a pointer's origin, `ClickWithPressOrigin`, does so without
a pointer: for that one synchronous press it moves only the pressed
widget, so the widget's own `mapFromGlobal(QCursor::pos())` reads the
press point, it never moves the real cursor, and it ends with `Click`'s
leave, so it leaves no hover behind either. QPA delivery
(`QpaMouseSequence`, `AppendQpaMouseDrag`) does not move the OS pointer
either, but it changes Qt's own pointer state as real input does: the
pressed buttons (`QGuiApplication::mouseButtons()`), the mouse grab, the
pressed and last mouse receiver, the mouse window and Qt's last cursor
position. A hover a sequence's move or release leaves lasts until its
`end()`, which releases the buttons it holds and a grab it caused and
delivers a leave, so the sequence leaves no hover behind; Qt's last cursor
position stays on the last delivered point. A press starts a
`RippleButton` ripple at the origin `prepareRippleStartPosition()`
derives from the real `QCursor::pos()` - with `Click`, `Drag`,
`AppendQueuedStartDrag` and QPA delivery (on macOS, Windows and X11
`QCursor::pos()` reads the OS pointer) where the owner's mouse is,
usually outside the widget, or none when the class's guard refuses that
point; with `ClickWithPressOrigin` at the press point - and the ripple
decays over its style's `showDuration + hideDuration`; finish it with
`finishAnimating()` or wait it out before measuring a fill.

Timers, queued invokes, animations, network callbacks, and reactive streams
still require condition waits. `Settle` is not a replacement for readiness.

## Related guidance

[Launch behavior](../launch.md); [chat-list resolution](../chat-lists.md); [failure diagnosis](../diagnostics.md);
[QPA mouse delivery](test_qpa_mouse.md); [console shield](test_console_shield.md).
