# test_chat_show_self_test

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness self-test.

Implementation: [test_chat_show_self_test.cpp](../../test_chat_show_self_test.cpp).
Entry contract: [test_chat_show.h](../../test_chat_show.h).

## Use when

Verifying the [chat-list show helper](test_chat_show.md),
`AppendShowInChatList`: that it shows one message of the legacy
`HistoryInner`, a `HistoryView::ListWidget` and Recent Actions'
`AdminLog::InnerWidget` wholly in view, settled, painted, ready and hovered
from far above and far below, scrolls again after a later layout change, and
refuses `no-view`, `cannot-fit`, `out-of-view` and `not-ready` by name before
the runner's stage timeout. Run it after changing the helper or the list
scrolling, layout, paint or hover paths it reads. This suite does not need
routine calls from product overlays; an overlay that needs a message shown
calls the helper itself.

## Fixture and verification

Entry point: `Test::AppendChatListShowSelfTest(runner, channel)`, appended by
an overlay's `SetupScenario` after its chats-loaded wait (it has no pack of
its own; the repository `test_scenario.cpp` stays a no-op). It reuses the
helper's own list reads - kind, owning scroll, visible message area, pending
layout, mouse-active gate and view identity - through the header's self-test
access functions, which are not for overlays. Every stage name starts with
`chat-list show self-test: `, the helper's own stages with
`show in chat list: chat-list show self-test: `. In order: capture the
baseline and show the window normally, keep the window exposed, then for the
legacy `HistoryWidget`, a `HistoryView::ChatWidget` and the channel's Recent
Actions in turn open the channel, inject that list's fixtures and run its
legs (the HistoryInner's fixtures are destroyed before the ChatWidget opens),
then the teardown and a settle after it. A leg's stages are
`chat-list show self-test: <leg> - prepare`, the helper's stage and
`chat-list show self-test: <leg> - judge`.

**Fixture needs.** The Test account (the session gate refuses others), a
megagroup or channel it administers - its creator or an admin, so Recent
Actions open - with its folder known and no unread messages, and a primary
window that is shown and exposed: the self-test calls `showNormal()` on a
minimized window and `KeepWindowExposed`
([test_window_exposure](test_window_exposure.md)), and activates nothing.
Missing any of these is a named `fixture gate:` FAIL, after which the
dependent stages are `N/A`; an `N/A` leg proves nothing and never counts as
an accepted one. Every fixture is local and nothing is sent: outgoing
client-id text messages (`isLocal() && !isRegular()`), added with
`History::addNewLocalMessage` while their list is shown (the HistoryInner's
are destroyed again before the ListWidget opens), one unattached local
message made with `History::makeMessage` (`MessageFlag::FakeHistoryItem`, so
no history block, slice or log shows it), and local ChangeAbout events
delivered through the admin log's own `flushNewEvents()` once its first page
has loaded. Scrolling may still send the lists' ordinary read-only preload
requests.

**Legs.** Each leg is a prepare, the helper's stage and a judge in the very
next run. The prepare scrolls once only when its premise does not hold yet,
waits for a settled layout and gates the leg on the measured premise; the
judge re-resolves the message, the list's own view (from the history blocks
or the list's own element vector), its origin (`itemTop()` plus the
selection offset), the visible message area and the owning scroll, and never
just remaps the reading's rect. The success legs ask for readiness through a
self-test probe that answers false on its first eligible call and true only
300 ms later, and for a hover at the target's `Element::innerGeometry()`
centre; their judge reads first, then captures the owning scroll's visible
message area through its section (`chat_show_<leg>.png`), then leaves the
list.

| List | Leg | What the judge requires |
| --- | --- | --- |
| HistoryInner (`use-new-chat-view` off), ListWidget (on), Recent Actions | far above, whole view | Started at least one visible-area height above it; at least one scroll; the current own view, origin and rect equal the accepted ones and lie wholly inside the visible message area and the owning scroll (`ReadMappedTarget`), nothing pending; completed paints covered that geometry; the probe answered false first, true only after its delay, on that view at its current height; the list resolves exactly that view's content at the hover point (`isUnderCursor()`, `pointState() != Outside`). |
| The same three | far below, `innerGeometry()` | The same, from at least one height below. |
| The same three | no view (the unattached message) | `no-view`, naming the kind, the message and the visible area; no scroll; decided before the deadline. |
| ListWidget | later layout | After the helper's first scroll a queued one-shot `HistoryItem::setText` growth of a short target, read once the product's layout ran, made it taller, still shorter than the visible area and out of view; the helper scrolled again from that geometry and accepted the grown height after a fresh paint and readiness; then the success checks above. |
| ListWidget | cannot fit | `cannot-fit` on a settled full view taller than the visible area (its line count from the measured area and the message font). |
| ListWidget | out of view | `out-of-view` at the 3 s deadline for a fitting target one height away while its scroll is `disableScroll(true)`: corrections asked, the scroll never moved; the judge enables it again at once. |
| ListWidget | not ready | `not-ready` at the 3 s deadline for an in-view, settled, painted target whose readiness is always false, the probe's details in the reason. |

Rows: one `CHAT_LIST_SHOW_SELFTEST: leg= type= kind= target=
scrolledForPremise= prepared={...} judge={...} probe{...} | CHAT_LIST_SHOW: ...`
per leg, `CHAT_LIST_SHOW_SELFTEST_GROWTH:` for the growth, `FIXTURE_SHOW:` for
the unattached message, and a Check per oracle. A judge runs only after its
helper stage finished, so a refusal leg's judge also proves the refusal came
without a runner `stage timed out`.

**Expected verdict.** PASS only when every Check passes and no stage is
`N/A` or timed out: all thirteen legs ran, the seven success legs (far above
and far below in each list, and the later layout) were accepted and confirmed
by their judges, and the six refusal legs (no view in each list, cannot fit,
out of view, not ready) refused by exactly that name. A `fixture gate:` FAIL
says the account, channel, window or a leg's measured premise was not as
required; fix the fixture and run again, it is no verdict on the helper.

**Teardown.** The release is registered with `Runner::onFinish` before
anything changes and is idempotent; the teardown stage runs it first: a leave
on every list the self-test opened, the scroll enabled again, the growth
subscription ended, every local and unattached message destroyed by id, the
option and the found chat restored (replacing Recent Actions lets its
`AdminLog::OwnedItem` owners destroy its items; the self-test never destroys
them itself), the exposure hint undone and a window it un-minimized minimized
again. A bounded settle stage then waits for the admin-log items to be gone
and the window state to hold for 1.5 s, and checks both and the stacking hint.

What has actually run is recorded in the module reference's
[Verification](test_chat_show.md#verification); record executed self-test
results in the change's evidence.
