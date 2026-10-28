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

## A chat's separate window

An experimental option, not the window, decides which list a chat's
separate window shows. For an available History chat in an unlocked session
with no window of its own yet, `Core::Application::ensureSeparateWindowFor`
(`core/application.cpp`) constructs the section before it returns: the new
window's `MainWindow::setupMain` (`mainwindow.cpp`) shows the chat through
`SessionNavigation::showByInitialId`, `showThread` and
`SessionController::showPeerHistory` (`window/window_session_controller.cpp`)
to `MainWidget::showHistory` (`mainwidget.cpp`), where
`if (peerId && OptionUseNewChatView.value())` picks the stack. With
`use-new-chat-view` on, the window shows a `HistoryView::ChatMemento`
section, a `ChatWidget` whose constructor `ChatWidget::ChatWidget`
(`history/view/history_view_chat_section.cpp`) builds its own `ListWidget`.
With it off, the window's own `HistoryWidget` builds a `HistoryInner` in
`HistoryWidget::showHistory` (`history/history_widget.cpp`).

The option is off by default and the same gate serves every window, the
primary one included. It decides for an ordinary History only: forum topics
and sublists take their own `ChatMemento` routes, `SessionNavigation::showTopic`
and `showSublist`, whatever its value, and `SessionController::showPeerHistory`
opens a community's info instead. `MainWidget::showHistory` returns
before the gate for a locked window, an unavailable chat, a message the
current section shows itself or a show routed to another window, so a later
show need not rebuild the section. Run 1 of
`2026/10/26/deliver-bot-state-edits-and-poll-watched-shared-state-to-running-widgets`
looked for a `ChatWidget` in such a window built with the option off and lost
every two-host check when its fixture gate read `list=0 bottom=1 pending=0`.

Two legacy lists of one `History` are not two hosts. `History::blocks`
(`history/history.h`) owns the legacy views: `History::addItemToBlock`
(`history/history.cpp`) creates each one with the history's single
`delegateMixin()->delegate()`, and `HistoryView::Element::attachToBlock`
(`history/view/history_view_element.cpp`) makes it its message's
`mainView()`, so every `HistoryInner` of that history uses the same view of
a loaded message. That delegate forwards to one `HistoryInner` at a time:
`HistoryInner::HistoryInner` binds it to itself through
`HistoryMainElementDelegateMixin::setCurrent`, a plain assignment
(`history/history_inner_widget.h`), and `HistoryInner::~HistoryInner` clears
it without checking which list holds it (`history/history_inner_widget.cpp`).
The list constructed later takes the binding, and whichever list is destroyed
first leaves the other unbound. A `ListWidget` owns its views instead:
`ListWidget::enforceViewForItem` (`history/view/history_view_list_widget.cpp`)
creates them with `item->createView(this)`, the views the owner proof above
accepts.

To get a `ListWidget` in a separate window, turn the option on for that
window's construction only. Given a live `history` whose id passes
`Window::CanShowSeparateWindow` (`ensureSeparateWindowFor` asserts it) and
has no window yet (`Core::App().separateWindowFor(id)` is null; an existing
window is reused, not constructed), read the option, set it, open the window
and restore the value found before any other navigation:

```cpp
const auto id = Window::SeparateId(not_null<Data::Thread*>(history));
auto &option = base::options::lookup<bool>(kOptionUseNewChatView);
const auto found = option.value();
option.set(true);
const auto window = Core::App().ensureSeparateWindowFor(id, ShowAtTheEndMsgId);
option.set(found);
```

`kOptionUseNewChatView` is declared in `mainwidget.h` and
`base::options::lookup` in `Telegram/lib_base/base/options.h`. The section
was constructed inside the call, so restoring does not rebuild it, and
restoring at once keeps the option from deciding what any window, the
primary one included, builds on its next show. `test_chat_menu.cpp` reads
and restores the option the same way (`CaptureBaseline`, `OpenSavedMessages`,
`Release`), but holds it through a whole leg of the primary window and
restores it only in `Release`. A `set` also schedules a delayed write of
`tdata/experimental_options.json`, unless one is pending, that keeps the
options differing from their defaults at write time and removes the file
when none do (`base::options::details::BasicOption::set` and `Write` in
`Telegram/lib_base/base/options.cpp`); read the live value with `value()`,
never from that file.

Resolve the list inside that window: walk `window->widget()->sessionContent()`,
accept the `ChatWidget` whose `id()` has this `history`, no `repliesRootId`
and no `sublist`, take its `ListWidget`, and check that the list's `window()`
is `window->widget()`, so a list in another window cannot answer. Prove the
message with `viewByPosition`, `data()` and `delegate()` as above. For two
live views of one message, show the chat in the primary window first, then
open the separate window as above: once that window is registered, a show of
the chat from another window that reaches
`MainWidget::showHistoryInDifferentWindow` is sent to it, because
`Core::Application::windowForShowingHistory` prefers the chat's separate
window. Close only a window the overlay opened, in a settling stage and in
`Runner::onFinish` for abort paths, as
[controller readiness](helpers/test_controller_ready.md#lifetime-and-cleanup)
describes.
