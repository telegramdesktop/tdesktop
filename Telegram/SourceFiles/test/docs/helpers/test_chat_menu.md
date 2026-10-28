# test_chat_menu

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_chat_menu.h](../../test_chat_menu.h).

## Use when

Attribute chat-list context menus to the exact message. Read the contract below and the source header before use.

## Contract and usage

The chat-list context-menu reader, `OpenChatListContextMenu(list, target,
point)` ([input helpers](test_widgets.md#input-helpers)), in a module of its own because it depends on history
view types, which `test_menu.h` does not: `ChatListMenuReading` (the
underlying `ContextMenuReading`, with its menu pointer cleared on every
refusal; the requested message and the list's own view of it; the four
`ChatListResolution`s; `enterSent`, `leaveSent`, `menuRead`, `observed`,
`listMenu`, `menuHidden` and `attributed`; `built()`, never true on default
state), the fourteen refusals `ChatListMenuRefusalName` names, and
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

## Related guidance

[Input contracts](test_widgets.md#input-helpers); [failure diagnosis](../diagnostics.md).
