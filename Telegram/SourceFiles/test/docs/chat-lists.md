# Resolving a chat message list

[Harness guide](../README.md) · [Helper index](helpers/README.md)

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
