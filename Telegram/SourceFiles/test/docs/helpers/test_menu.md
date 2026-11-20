# test_menu

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_menu.h](../../test_menu.h).

## Use when

Same-turn menu readings, actions and captures. Read the contract below and the source header before use. A custom `Ui::Menu::ItemBase` row whose `QAction` carries no text is activated by its full accessible name and real keys instead: [Accessible custom rows](#accessible-custom-rows). A receiver that accepts the event and builds its menu only on a later turn is read with a retake of the same reading: [Menus built on a later turn](#menus-built-on-a-later-turn).

## Contract and usage

Deterministic `Ui::PopupMenu` capture whose readiness is content identity
only, its `showingContent` reading, and its own self-test. `CapturePopupMenu`
takes an optional trailing `skipReason` that gates both stages it appends, the
opener and the capture; left empty the helper is unchanged. A locked Windows
console is no reason to skip a popup-capture leg: arm `Test::ArmPopupHold`
([popup hold reference](test_popup_hold.md)) before the menu opens and
release it after the last leg that needs it open across turns. The self-test
is the hold's standing check: its `open` stage arms it with the label
`popup-menu-self-test` right before it pops its menu and Notes the
`Test::ReadConsoleLock()` row (`test_console_lock.h`) it read there, so on a
locked Windows console the `popup_menu_open` capture, `close`, and the
`popup_menu_reopened` opener and capture run and decide as on an unlocked
one. Its teardown hides and destroys the fixture, releases the hold and
Notes `popup menu self-test: popup hold released, deactivations stopped=<n> -
CONSOLE_LOCK: ...`; `Runner::onFinish` releases the hold when a timeout skips
the teardown. No Check asserts a nonzero count, which depends on the console.
It holds and releases its own arm, so a campaign appends it outside its own
popup hold. The context-menu reader beside it:
`OpenContextMenu(receiver, point, reason)` snapshots every live
`Ui::PopupMenu` in the application (`QApplication::allWidgets()`, held as
`QPointer`s, so a menu at a reused address never reads as an old one), sends
the `QContextMenuEvent` inside `Settle` and, in that same call, takes the one
menu that was not alive before - a submenu, whose parent is a `Ui::PopupMenu`,
is not counted - into a `ContextMenuReading`: the ordered non-separator texts
(an entry with no text kept in place as an empty string), the action and
separator counts, the parent and `parentIsReceiver`, and a
`ContextMenuLifecycle` at build, inside the settle action right after
`sendEvent`, and again after the postponed calls settled (alive, visible,
hidden, `activePopupWidget()` as self / other / none, geometry, actions,
`PopupMenuReady`, application state, active window, focus window), with the
receiver, the menu and its parent held as `QPointer<QWidget>` and everything
else recorded by value. No event-loop turn runs inside the call, though on
Windows `PopupMenu::showPrepared`'s synchronous repaint can drain
`crl::on_main` work there. A menu that exists but is hidden or empty is
reported, not refused; the refusals are `receiver-gone`, `no-menu` (with
`reshown` counting a kept menu shown again) and `several-menus` (naming every
candidate). The reading keeps its pre-event snapshot (`before`, one
`ContextMenuSnapshotEntry` per menu alive then), which
[`RetakeContextMenu`](#menus-built-on-a-later-turn) takes against on a later
turn. `ReadContextMenuLifecycle` is the pure reading a later poll takes, and
`ContextMenuDetails` prints recorded values only, `retakes` included. An optional trailing
at-build observer, `OpenContextMenu(receiver, point, reason, observeAtBuild)`,
is called once, synchronously, inside the settle action after the event was
delivered and the fresh menu was collected, immediately before the at-build
lifecycle is read - so before any `Ui::PostponeCall` the delivery queued has
drained - only while the receiver is alive (neither `receiver-gone` refusal
calls it), and also when no fresh menu exists; it observes only - no event, no
product mutation, no menu popped or hidden, no event-loop turn - and left
empty the reader behaves exactly as before. On a chat list - `HistoryInner` or
`HistoryView::ListWidget` - this reader alone may read a menu the list built
for another message: read those with `OpenChatListContextMenu`
([chat-list menu reference](test_chat_menu.md)), which reads the list's resolution through
that observer. `TriggerContextMenuAction(reading, text)` triggers the one
entry with exactly that text on the still-live menu and, inside `Settle`,
delivers its queued `Ui::Menu::CreateAction` callback with
`QCoreApplication::sendPostedEvents(action, QEvent::MetaCall)` - that action's
calls and no other receiver's - before returning, without hiding the menu; its
refusals are `menu-gone`, `no-such-action` (listing the menu's texts),
`several-actions`, `action-disabled` (Qt 5 would emit `triggered` for a
disabled entry, Qt 6 would not, a user cannot) and `submenu-entry` (decided by
`action->menu()`, so an entry given a submenu by a bare `ensureSubmenu` is
triggered like an ordinary one), printed by `MenuTriggerDetails`. Neither
grabs pixels, Notes or Fails. Its own self-test,
`AppendContextMenuReaderSelfTest`, runs over hidden harness widgets on the
primary window (a missing one is its named fixture gate): a menu built in the
receiving widget, the Keyboard reason and a reading that outlives its menu, a
menu parented to another widget that a receiver-rooted search misses,
`no-menu` beside those positive controls, the reader and trigger refusals, an
entry's callback run when the trigger returns while a marker queued just
before it has not, beside a raw `QAction::trigger()` premise whose callback
stays queued, and the at-build observer, which runs exactly once, after the
handler ran and before a `Ui::PostponeCall` marker the handler queued drained,
with the marker run by the time the reader returns, and is not called for a
receiver destroyed before the call (`receiver-gone`).

### The opening-turn grab row

`popup menu self-test: open` pops its menu and, in that same call, takes one
`PreparedWidgetCapture::prepare()` of it. Its row `a one-shot prepared grab in
the opening turn refuses the frame instead of saving it` shows why a same-turn
grab is no capture: the show animation's first frame has nothing painted on
it, and the prepared capture's `LooksBlank` refusal turns it away instead of
saving it.

The row decides on `Ui::PopupMenu::showStateValue()`, the menu's own account
of what it painted. The stream has no current value and fires synchronously:
from `PopupMenu::startShowAnimation()` (a computed state, no paint), from every
`PopupMenu::paintEvent()` while the show animation runs (the state that paint
drew, `appearing=1`), and once with a non-appearing state from the paint that
finishes the animation. The open stage subscribes before `popup()` with a
local `rpl::lifetime` that ends with its `run`, keeps the last state emitted
before `prepare()` (`preGrab`) and the last one emitted during it (`grab`) -
the grab is a synchronous `QWidget::render`, so a grab of an animating menu
emits the state it painted - and decides in its `then` on `grab` alone.
`preGrab` is context: an earlier zero-opacity state says nothing about the
frame the grab painted. The details after the readings are `prepare()`'s
refusal reason, or the stage's `PopupMenuDetails` line when it accepted.

- `grab` reads `appearing=1 opacity=0`, the blank first frame: `PASS` when
  `prepare()` refused it, `FAIL` when it accepted it.

  ```text
  TEST_RESULT: PASS: a one-shot prepared grab in the opening turn refuses the frame instead of saving it - grab={appearing=1 opacity=0 widthProgress=0 heightProgress=0 appearingSize=<w>x<h> toggling=0} preGrab={...} accepted=0 - target grab still looks blank: ...
  TEST_RESULT: FAIL: a one-shot prepared grab in the opening turn refuses the frame instead of saving it - grab={appearing=1 opacity=0 ...} preGrab={...} accepted=1 - menu ... visible=1 ...
  ```

- Anything else: a named `N/A` whose reason begins `not reached:` and says
  which, whatever `prepare()` answered.

  ```text
  TEST_RESULT: N/A: <row> - not reached: advanced frame painted - the show animation had already stepped past its first frame when the grab painted, so the opening-turn grab did not paint the blank first frame this row exists to refuse - grab={appearing=1 opacity=<above 0> ...} preGrab={...} accepted=<0 or 1> - <refusal or readiness>
  TEST_RESULT: N/A: <row> - not reached: show animation already finished - the grab painted the menu's final frame, so ... - grab={appearing=0 opacity=1 ...} preGrab={...} accepted=<0 or 1> - <refusal or readiness>
  TEST_RESULT: N/A: <row> - not reached: no state emitted by the grab - it painted no show-animation frame: prepare() refused before grabbing, or no show animation was running when it painted, so ... - grab={none} preGrab={...} accepted=<0 or 1> - <refusal or readiness>
  ```

Why an `N/A` and not a premise `Check`. The stage's `fixture gate:` rows are
hard Checks because a host without translucent popups or with animations
disabled can never certify the shape. A painted opening frame is one launch's
timing on a host that also paints the blank one, so the row neither fails the
capture for the host's clock nor passes a refusal that never happened. No
launch is promised the blank frame: painted launches can come in a row - Runs
D6 and D7 of the diagnosis below were consecutive - and their rate varied
between that diagnostic set's runs (2 of 7, 0 of 8, then 1 of 6 and 0 of 10
under CPU load, Runs D to G), so a later launch can read the same `N/A`. Each
launch decides from its own grab. The row keeps one name across `PASS`,
`FAIL` and `N/A`, and a campaign's run report lists it under `skipped` with
the reading that put it outside the premise, the shape `DecideControl` in
`test_window_exposure.cpp` uses
([window exposure reference](test_window_exposure.md)).

Why a broken blank refusal still FAILs. The gate reads the menu's state and
never `LooksBlank` or the image, so a `PreparedWidgetCapture` that stopped
refusing blank frames accepts the zero-opacity grab and the row FAILs. Only a
refusal broken for a barely advanced, faint frame would read `N/A`; 28 of the
31 diagnosis launches below landed on progress 0, the frame the regression
check decides on.

The observed Windows cause, from
`2026/11/12/keep-the-popup-self-tests-opening-turn-grab-row-from-failing-falsely`
(`work/diagnosis.md`, where the runs live; 3 of 31 launches with the
self-test as the process's first popup). `PopupMenu::popup()` reaches
`PopupMenu::showPrepared()`, which starts the show animation
(`startShowAnimation()`, `_a_show` from progress 0) and then, on Windows only,
calls `Ui::ForceFullRepaintSync`. That sends a synchronous
`QEvent::UpdateRequest`; `Core::Sandbox::notify` fires
`crl::on_main_update_requests()` for it before Qt delivers the event, and
`Ui::Animations::Manager::Manager()` subscribes to that stream
(`ui/effects/animations.cpp` in lib_ui). So this `UpdateRequest` can run
`Manager::update()` and advance an active animation, though it need not: the
subscriber calls `update()` only when more than `kIgnoreUpdatesTimeout` has
passed since the manager's last update, and `update()` returns early when no
animation is active, while it is already updating, or while the manager is
`_scheduled`. A clear `_scheduled` removes one of those guards; it does not by
itself force the step. When `update()` runs there, it steps `_a_show` by the
real time elapsed since `startShowAnimation()`, and the synchronous repaint,
and every grab after it in the same turn, then paints a partly or fully shown
menu. Run F4 stepped about 48 ms, painted
`opacity=0.979796 widthProgress=0.8 heightProgress=0.679869`, and `prepare()`
accepted it. In every passing launch that recorded the manager it was still
`_scheduled`, `update()` was skipped and every state in the turn read
progress 0; Run G1's 71 ms delay alone did not paint. Why `_scheduled` was
clear in a failing launch is not established. Nothing in it is lock-specific -
a slow or loaded host only widens the step - and it is time-based animation
doing its job, not a product defect. `OpenContextMenu`'s Windows delivery
caveat above is the same synchronous repaint.

## Accessible custom rows

### When to use it

A custom row keeps its label and callback on the `Ui::Menu::ItemBase` rather
than on its `QAction`. `AboutAction` (`ui/boxes/peer_qr_box.cpp`) owns a dummy
`QAction` with no text, paints its text and its about line itself, and
answers `accessibilityName()` with `<text>, <about>`. `TriggerContextMenuAction`
matches `QAction::text()`, so it correctly refuses such a row's text as
`no-such-action`. Do not retry it with an empty string: beside another such
row that is ambiguous, and otherwise it triggers the dummy action directly
instead of through the row. Do not pick the row by index or by a prefix of
its name either. Use `ReadMenuItem(popup, fullName)` and
`ActivateMenuItem(reading)` for such a row, and keep
`TriggerContextMenuAction` for ordinary `QAction` entries.

### Preconditions and limits

Read and activate in the turn that opened the popup, in the same stage action.
On an unattended console a popup may be closed before the next turn. The
popup must be open and ready (`PopupMenuReady`); a closed one is refused, and
the helper never reopens it. For a context menu, pass `reading.menu.data()`
from `OpenContextMenu`, or `reading.menu.menu.data()` from a `built()`
`OpenChatListContextMenu` reading. The name is the row's whole
`accessibilityName()`: for `AboutAction` its text, a comma and a space, and
its about line. The helper does not hover, activate a window,
turn the event loop or deliver posted events. Submenu entries and custom rows
that ignore Return are outside its consumers and are not supported.

### Minimal use

```cpp
runner->add({
	.name = u"timer row"_q,
	.run = [=] {
		const auto menu = Test::OpenContextMenu(state->receiver, state->point);
		const auto row = Test::ReadMenuItem(
			menu.menu.data(),
			state->text + u", "_q + state->about);
		const auto key = Test::ActivateMenuItem(row);
		Test::Check(
			key.activated(),
			u"timer row: Return sent to the exact accessible row"_q,
			Test::MenuItemReadingDetails(row)
				+ u" "_q
				+ Test::MenuItemActivationDetails(key));
	},
	.then = [=] {
		// Assert the product's own outcome here, or wait for it in a bounded
		// until of a later stage.
	},
});
```

### Readings and refusals

`ReadMenuItem` walks the popup's current `Ui::Menu`. Each non-separator action
resolves to its row through `Menu::itemForAction`, and `names` lists every
row's name. Exactly one enabled exact match is `found()`. Only then does the
reading hold the row and its `QAction`, beside the popup and the Menu, as
`QPointer`s. Its refusals are `menu-gone` (no live `Ui::PopupMenu`),
`no-such-item` (no row has this full name; an empty name matches none),
`several-items` (an ambiguous name binds no row) and `item-disabled`.

`ActivateMenuItem` sends nothing for a refused reading and returns its refusal.
Before any key and after every Down it checks the binding, in this order:

- `menu-gone`: the popup was destroyed.
- `menu-replaced`: the popup's current Menu is another one. `stashContent`
  does that while the old rows stay alive, so liveness alone never suffices.
- `item-gone`: the row was destroyed, even if a row of the same name exists.
- `item-stale`: the row's `QAction` died, or the row left the Menu, answers
  another action, is no longer mapped from it or was renamed.
- `several-items` or `item-disabled`: a recheck by name. The recheck never
  adopts the target it finds.
- `menu-gone`: the popup is no longer open for input.

It then sends `Qt::Key_Down` through `Test::PressKey` until the captured row is
the Menu's selected row with source Keyboard. It sends at most `bound` Downs,
the Menu's action count with separators. A screen reader preselects the first
enabled row from the keyboard, so `steps` may be zero for that row. A row still unselected at the bound is
`not-selected`, and no Return is sent. Otherwise exactly one `Qt::Key_Return`
follows the last passing check. `MenuItemReadingDetails` and
`MenuItemActivationDetails` print the recorded values and pointer liveness.

### Lifetime and timing

A reading owns nothing and stays printable after its objects die. Nothing is
read after the Return, because the product may destroy the menu from it:
`activated()` means the key was sent, never that a callback ran or a product
state changed. Return reaches the row's `handleKeyPress`, its
`setClicked(Keyboard)`, `PopupMenu::handleTriggered` and finally
`QAction::trigger()`. A callback connected with `ItemBase::setActionTriggered`
is direct, so it has run when the helper returns. One made by
`Ui::Menu::CreateAction` is queued, so it runs a turn later. Assert the
product outcome in a later `then` or a bounded wait. The helper owns nothing
and needs no cleanup; the selection its Downs moved and the menu's close on
Return are the product's own.

### Verification

`AppendMenuItemActivationSelfTest` is the design-level proof of this route.
It runs in five Runner stages over popups of its own, parented to the primary
window, with one empty-`QAction` custom row class modeled on `AboutAction`:

1. Lookup: the custom row's exact binding and full name, an ordinary sibling
   found through the same walk, `no-such-item` for a missing name and for the
   label alone, `several-items` for two identical names, `item-disabled`, no
   key for those refusals, and the old trigger's `no-such-action`.
2. Activation: the target follows an ordinary row, a separator and a disabled
   row. Its callback runs exactly once, inside it the row is selected from the
   keyboard, and no other callback runs.
3. Lifetime before input: a destroyed row (`item-gone`), a destroyed popup
   (`menu-gone`) and a stashed Menu (`menu-replaced`), each beside a fresh
   reading of a same-named replacement and with no key or callback.
4. Across keyboard input: a tripwire row's Keyboard selection queues a
   guarded `Ui::PostponeCall`, which the Down's settlement drains. It
   destroys the popup (`menu-gone`) or stashes its Menu (`menu-replaced`). No
   Return is sent and no callback runs.
5. Teardown, which `Runner::onFinish` repeats.

It needs no account, session or network. A missing primary window fails its
first fixture-gate Check, and the later stages but the teardown are N/A.
Approval needs every Check to PASS with no N/A. This page describes that
design; executed results belong to the run's task evidence.

## Menus built on a later turn

### When to use it

A receiver that accepts the context-menu event and opens its menu only when
an answer arrives reads `no-menu` from `OpenContextMenu`, correctly: nothing
was built in the event's turn. A chat list over a widget card is such a
receiver: `WidgetTouch` (`tgw_touch.cpp`) accepts the event, records it, and
re-sends it to the list's own `contextMenuEvent` once the widget's answer
says the widget did not take the press, or at `tgw_renderer.cpp`'s 150 ms
answer deadline. `RetakeContextMenu(reading)` repeats the fresh-menu take of
that reading on a later turn, against the same pre-event snapshot. On a chat
list use `BeginChatListContextMenu`, `PollChatListContextMenu` and
`EndChatListContextMenu` ([chat-list menus built on a later
turn](test_chat_menu.md#menus-built-on-a-later-turn)), which retake through
it and attribute the menu. A handler that builds its menu inside the event
is still read in that turn with `OpenContextMenu` alone: never poll for such
a menu (the context-menu row of the [diagnostic reference](../diagnostics.md)).

### Preconditions and limits

- The reading comes from `OpenContextMenu`. A default reading has no
  receiver and is never retaken.
- Only a `no-menu` reading whose receiver still lives is retaken. A `built()`,
  `several-menus` or `receiver-gone` reading is left exactly as it is, so the
  first menu found stays the reading's menu.
- The take's rules are the event's: exactly one `Ui::PopupMenu` that was not
  alive in the snapshot, a submenu not counted, a kept menu shown again
  counted in `reshown` only, two new menus refused `several-menus` (which
  ends the retakes).
- It is a poll, so a menu built and closed between two calls is never seen.
  Nothing guarantees that a popup outlives its turn (the diagnostic row
  above), and on a locked Windows console Qt closes a shown menu before the
  next turn (`AppendPopupMenuCaptureSelfTest`'s lock gate in
  `test_menu.h`). A later-turn `no-menu` reading counts as a product result
  only beside a positive control read the same way, and is undecidable on
  a locked console.
- `atBuild` stays the event's own turn's lifecycle, alive=0 for a menu built
  later; `afterSettle` is the latest retake's. A retaken menu has no
  at-build reading: it was built on a turn the helper did not run in.
- The caller owns the timer and the bound; the helper never waits, sends
  nothing, calls no observer, hides nothing and turns no event loop.

### Minimal use

```cpp
runner->add({
	.name = u"deferred menu: deliver"_q,
	.run = [=] {
		state->sentAt = crl::now();
		state->menu = Test::OpenContextMenu(state->receiver, state->point);
	},
});
runner->add({
	.name = u"deferred menu: read"_q,
	.until = [=] {
		const auto built = Test::RetakeContextMenu(state->menu);
		return built
			|| (state->menu.refusal != Test::ContextMenuRefusal::NoMenu)
			|| (crl::now() - state->sentAt >= kMenuBound);
	},
	.then = [=] {
		Test::Check(
			state->menu.built(),
			u"deferred menu: built on a later turn"_q,
			Test::ContextMenuDetails(state->menu));
		// Trigger an entry or hide the menu here, in this same turn.
	},
});
```

`kMenuBound` is the caller's: the product's own deadline plus slack, so a
menu that has not come by then never comes. A Runner `until` is polled on
its 50 ms tick; a caller that needs a finer poll keeps its own timer, as
`Test::ConformancePointer` does with 5 ms
([timed pointer sequences](test_widget_conformance.md#timed-pointer-sequences)).

### Readings and refusals

The answer is `built()`. Every repeat counts in `retakes` and rewrites what
the take fills - `menu`, `parent`, their identities and `parentIsReceiver`,
`fresh`, `freshMenus`, `reshown`, `menusAfter`, `texts`, `actions`,
`separators`, `refusal` and `reason` - so a reading printed with
`ContextMenuDetails` reads as the latest retake found the menus; the
delivery fields (`delivered`, `accepted`, `receiverAlive`, `local`,
`global`, `menusBefore`) stay the event's. `TriggerContextMenuAction`
accepts a retaken reading like any other, while its menu lives.

### Lifetime and cleanup

The snapshot holds `QPointer`s only, so a reading stays safe to keep and
retake after any menu in it died. A retaken menu is open: hide it
(`Ui::PopupMenu::hideMenu(true)`) or trigger it in the turn that found it.

### Verification

No self-test of its own: `AppendContextMenuReaderSelfTest` covers the take
it repeats, and the later-turn path is exercised through the chat-list
reader by a right click on a widget card (`auto/16-sdk-taps-b` of the
[conformance runner](test_widget_conformance.md)). That run's results belong
to its task evidence.

## Related guidance

[Input contracts](test_widgets.md#input-helpers); [failure diagnosis](../diagnostics.md).
