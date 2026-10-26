# test_menu

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_menu.h](../../test_menu.h).

## Use when

Same-turn menu readings, actions and captures. Read the contract below and the source header before use. A custom `Ui::Menu::ItemBase` row whose `QAction` carries no text is activated by its full accessible name and real keys instead: [Accessible custom rows](#accessible-custom-rows).

## Contract and usage

Deterministic `Ui::PopupMenu` capture whose readiness is content identity
only, its `showingContent` reading, and its own self-test. `CapturePopupMenu`
takes an optional trailing `skipReason` that gates both stages it appends, the
opener and the capture; left empty the helper is unchanged. The self-test
reads `Test::ReadConsoleLock()` (`test_console_lock.h`) right before it pops
its menu and when each stage that needs the menu open across turns begins,
keeping the first locked reading, so on a locked Windows console the
`popup_menu_open` capture, `close`, and the `popup_menu_reopened` opener and
capture are `TEST_RESULT: N/A` naming the lock while `open`, `refusal text`
and the teardown run and decide. The context-menu reader beside it:
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
candidate). `ReadContextMenuLifecycle` is the pure reading a later poll takes,
and `ContextMenuDetails` prints recorded values only. An optional trailing
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

## Related guidance

[Input contracts](test_widgets.md#input-helpers); [failure diagnosis](../diagnostics.md).
