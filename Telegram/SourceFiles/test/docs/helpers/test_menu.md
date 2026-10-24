# test_menu

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_menu.h](../../test_menu.h).

## Use when

Same-turn menu readings, actions and captures. Read the contract below and the source header before use.

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

## Related guidance

[Input contracts](test_widgets.md#input-helpers); [failure diagnosis](../diagnostics.md).
