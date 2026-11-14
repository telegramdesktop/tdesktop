# test_chat_show_place

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API (includes a harness self-test).
Source contract: [test_chat_show_place.h](../../test_chat_show_place.h).

## Use when

An overlay's oracle depends on whether several messages of one chat list are
on or off screen: a widget that polls only while watched, pauses while
hidden, or changes something while out of view. `AppendPlaceInChatList`
appends one stage that puts every message's whole own view inside the
list's visible message area together, or wholly above or below it, and
accepts only when the geometry and the caller's fresh visibility reading
agree for every message on one settled sample. From then on the placement's
record keeps each message's on/off state, and `AppendStayedInChatList`
appends a later stage that proves from it that each message stayed placed
for a declared window - every sample inside it, not its two ends. Both
refuse by name instead of timing out.

Use it instead of an overlay's own placement or continuity copy. Run 2 of
`2026/10/26/deliver-bot-state-edits-and-poll-watched-shared-state-to-running-widgets`
(its `work/test.md`) lost checks to four flaws of such a copy: four
carriers could not fit at once in 332 px and 415 px viewports (here
`cannot-fit`); one request per period was read from a stage's end state
(here every record sample of the window); fillers appended away from the
lists' bottoms moved nothing off screen (here off is accepted only on
measured geometry); and carriers off by geometry and absent from
`Tgw::Runtime::WatchingOnScreen()` still read the cached `_visible=1` (here
the caller's fresh reading must agree with the geometry).

Use something else when:

- one message must be shown before input - in view, painted, ready and
  optionally hovered: `AppendShowInChatList`
  ([test_chat_show](test_chat_show.md));
- a message is outside the list's loaded slice: navigate first with the
  product's `showThread` / `showAtPosition`
  ([chat-list resolution](../chat-lists.md)); this helper never loads
  history.

## Preconditions and limits

- **One resolved list with loaded own views.** `resolve` runs once, when the
  stage begins, and names one list - the legacy `HistoryInner`, a
  `HistoryView::ListWidget` or Recent Actions' `AdminLog::InnerWidget`, the
  owned widget of its `Ui::ElasticScroll`, resolved as
  [chat-list resolution](../chat-lists.md) says - and the messages'
  `FullMsgId`s. Every sample looks each id up again through the list's own
  session and takes the list's own current view through `test_chat_show`'s
  shared reads, never `item->mainView()`. Every message needs an own view
  when the stage begins, for an off placement too.
- **The unit is the whole own view**, by the lists' own visibility rule:
  the product counts a view as shown while
  `top + height > areaTop && top < areaBottom`, over the range
  `ChatListShowVisibleArea()` reads.
  - `OnScreen`: the view's bounds inside the visible message area and,
    mapped into the owning scroll, inside that area's clip there;
  - `OffAbove`: the view's bottom at or above the area's top;
  - `OffBelow`: the view's top at or below the area's bottom.

  For the off placements a message whose list no longer shows an own view
  counts as off (`viewShown=0`): nothing of it is shown, and the product's
  query answers false too. "Wholly on" is stricter than the product's
  "visible"; "wholly off" is exactly its "not visible".
- **Window visibility is the reading's term, not geometry.** A hidden or
  minimized window, or a hidden application, makes `queryVisible()` false
  whatever the geometry: an on placement or window then reads
  `reading-disagrees`, never an accepted on, while an off one agrees and
  the geometry alone proves off. A leg that hides or minimizes a window
  judges no stay spanning that time on that window's messages: place again
  once it is shown, since each stay's window starts at its placement's
  acceptance.
- **The fresh reading.** `visible` must be the product's fresh answer: for a
  widget card `Tgw::Runtime::queryVisible()` (private; read it with the
  file-private accessor idiom below) or membership in
  `Tgw::Runtime::WatchingOnScreen()`, never `Tgw::Runtime::_visible`. That
  value is a cache: `Runtime::RefreshVisibility()`, run from the posted
  `Runtime::Check()`, flips it only for heavy, stopped, painted or
  display-deferred runtimes, and `HistoryInner::visibleAreaUpdated()`
  unloads the heavy parts of views more than `kUnloadHeavyPartsPages` (2)
  visible-area heights away inside the scroll handler, before the posted
  check, so a runtime released off screen keeps its last value.
  `WatchingOnScreen()` is fresh for off, since it also requires
  `queryVisible()`; for on it also needs a Live, watching host and the
  posted refresh, so give such an on placement a longer `timeout` (the
  placement keeps asking until its deadline). For a message without a
  runtime, `history->owner().queryItemVisibility(item, view)` is the list's
  own term. The header names no `Tgw` type.
- **Pure callbacks.** `visible` and `visibleDetails` run synchronously with
  the list's own current view, or null, valid only during the call: on every
  settled placed sample of the stage, once more on a refusal's deciding
  sample, and on every record sample after an acceptance, across the
  caller's later stages until the record ends. They read only: no layout
  flush, no activation, no input, no pointer kept. Null `resolve` or
  `visible` is a composition error (`Expects`).
- **Units and timing.** Logical pixels: `element`, `viewport` and `band` in
  the list's coordinates, `effective` (the area mapped into the owning
  scroll and clipped to it) in the scroll's; times are `crl::now()` ms.
  Decisions before the deadline need geometry - each view's address and
  bounds, each message alive, the pending layout (`Element::pendingResize()`
  and the list's), the area, its clip, the scroll top and range - that read
  the same for 400 ms.
  The record samples every 100 ms plus one queued sample after each
  `scrolls()`, `innerResizes()` and `geometryChanged()` of the owning
  scroll, queued so it never reads a list mid-update inside a product
  handler. The placement's deadline is `max(timeout, 0)` ms after its run
  (10 s by default); its runner timeout is `kDefaultStageTimeout` more, and
  a stay's is `max(hold, 0)` plus `kDefaultStageTimeout`, so a refusal is a
  named reading, never `stage timed out`.
- **Scroll targets.** On centres the views' band in the area, clamped to
  `[0, scrollTopMax()]`; `OffBelow` asks for the area to end `kOffMargin`
  (8 px) above the topmost view, `OffAbove` to start 8 px below the lowest
  view's bottom, which keeps the views within the two pages the lists keep
  loaded (`kUnloadHeavyPartsPages` in `history_inner_widget.cpp`). The
  margin is the target, not the rule: messages already placed are accepted
  with no scroll, even when off by less than 8 px near the list's end. The
  first scroll is made in the stage's run; later ones come from a
  helper-owned 50 ms timer, each from a fresh sample of a settled layout.
- **It scrolls like a user**, through `Ui::ElasticScroll::scrollToY` and the
  section's synchronous scroll handlers, as
  [test_chat_show](test_chat_show.md) documents: read-only preloads near the
  loaded edges (a request the product makes, never the helper; the settle
  absorbs the shift), heavy-part unloading, read marking in a window that
  marks read. The scroll position is not restored.
- **Not supported:** mixed placements or several lists in one call,
  horizontal scrolling (a view past the area's sides is `cannot-fit`),
  window resizing (`cannot-fit` names the band and the area; enlarge the
  window first) and loading history (`cannot-move-off` names the room; add
  messages on that side or navigate first).

## Minimal use

Two widget cards held wholly on screen through an overlay's own stages for
at least 21 s, read through `Runtime::queryVisible()` with the file-private
accessor idiom `test_chat_show.cpp` uses for list privates:

```cpp
template <typename Tag, typename Tag::type Member>
struct PrivateMember {
	friend typename Tag::type Get(Tag) {
		return Member;
	}
};

struct RuntimeQueryVisibleTag {
	using type = bool (Tgw::Runtime::*)() const;
	friend type Get(RuntimeQueryVisibleTag);
};

template struct PrivateMember<
	RuntimeQueryVisibleTag,
	&Tgw::Runtime::queryVisible>;

// No own view, or no runtime, reads as not visible.
[[nodiscard]] bool CardVisible(
		not_null<HistoryItem*> item,
		const HistoryView::Element *view) {
	const auto runtime = view ? Tgw::FindRuntime(view) : nullptr;
	return runtime && (runtime->*Get(RuntimeQueryVisibleTag()))();
}
```

```cpp
const auto on = Test::AppendPlaceInChatList(runner, {
	.name = u"both cards on"_q,
	.placement = Test::ChatListPlacement::OnScreen,
	.resolve = [=] {
		return Test::ChatListPlaceTarget{
			.list = state->list.data(),
			.items = { state->first, state->second },
		};
	},
	.visible = CardVisible,
});
// The overlay's own stages run here while the record samples.
const auto stay = Test::AppendStayedInChatList(runner, {
	.name = u"both cards on"_q,
	.place = on,
	.hold = crl::time(21000),
});
runner->add({
	.name = u"judge the polls"_q,
	.run = [=] {
		Test::Check(
			stay->stayed(),
			u"fixture gate: both cards stayed wholly on screen"_q,
			Test::ChatListStayText(*stay));
		// The task's own oracle over its product rows between
		// stay->from and stay->till follows here.
	},
});
```

`hold` counts from the placement's acceptance, so the window lasts at least
21 s, longer when the overlay's stages take longer. For a window that ends
at a product event, put the stay right after the stage that waits for it,
with `hold` 0. A refused placement makes the stay `not-placed` with the
placement's refusal in its reason, so the judge's one fixture gate names
it; the overlay's own stages may skip on `!on->placed()`. `visibleDetails`
can print the runtime's state beside every answer. The same reading through
`WatchingOnScreen()`:

```cpp
.visible = [](not_null<HistoryItem*> item, const HistoryView::Element *) {
	return ranges::contains(Tgw::Runtime::WatchingOnScreen(), item);
},
```

## Readings and refusals

Both stages return a `std::shared_ptr<const ...>` reading filled while they
run; read it from a later stage. Neither stage Passes or Fails: a refusal is
a fixture gate, never product success.

**The placement**, stage `place in chat list: <name>`. `placed()` is true
only when it finished with no refusal. Its until accepts on one sample of a
layout that read the same for 400 ms with nothing pending, when every
message is placed by its rule and the caller's reading, asked on that very
sample, agrees for every message (true on, false off). That sample's marks
are the record's first, and `placedAt` is its time.

**The record.** It starts in the accepted stage's then. Each record sample
makes a `ChatListPlaceMark` per message: time, view bounds (`element`, empty
without a view), visible area, scroll top, whether the message is alive,
shown and placed, and the reading with its details (a destroyed message is
not asked; its details read `message destroyed`). `changes` starts with the
accepted mark and gets a mark each time `alive`, `viewShown`, `placed` or
`visible` changes; `last` is the latest sample; `samples`, `maxGapMs` and
`lastSampleAt` count and time the samples since the acceptance. It keeps
sampling while later stages scroll, until `Runner::onFinish`, the list's
destruction or the destruction of every placed message (`recordEnd`:
"released at the run's finish", "the list was destroyed", "every placed
message was destroyed"). So `last` and `changes` keep moving after the
stage: the accepted mark is `changes.front()`, and continuity is judged
only through a stay. A record writes no row.

**The stay**, stage `stayed in chat list: <name>`. `stayed()` is true only
when it finished with no refusal. Its window is
`[placedAt, max(this stage's run, placedAt + hold)]`: a fixed-length window
right after the placement with `hold`, or one spanning the consumer's own
stages with `hold` 0. Its until reads the record on the runner's tick and
decides at the record's first sample at or after `windowEnd`, at once on a
deviation already recorded inside the window, or when the record ended.
`till` is the record's latest sample then, and only marks in `[from, till]`
are judged, since the record outlives the stay. `from` and `till` are
`crl::now()` values, so a consumer brackets its own product rows with them.
A stay never ends the record: several stays may judge one placement, each
window starting at its acceptance. Per message, `window` holds its marks in
`[from, till]`, `changes` the ones after the accepted mark, and `deviation`
the first destroyed, displaced or disagreeing one (`deviation.at` 0: none).

A stayed window proves that no mark of any message in `[from, till]` was
destroyed, left its placement or read against it, and that the record
sampled at or after the window's end; because a mark is appended on every
change, that covers every record sample of the window. A "no change" counts
only with samples through the window, the `DiscriminatingScan` spirit
([test_probe](test_probe.md)): the stay requires only the sample at the end,
so a judge that needs a dense window reads `samples` and `maxGapMs` (the
self-test requires 5 samples in 1.5 s). An excursion shorter than the gap
that no scroll, inner-resize or geometry signal announced can pass unseen.

**Rows**, in ordinary integers, one per stage in its then:

- `CHAT_LIST_PLACE: name= finished= placed= placement= kind= list= viewport=
  effective= band= scrollTop= scrollTopMax= target= scrollRequests= scrolls=
  generation= settled= pending= placedAt= elapsedMs= timeout= refusal=
  samples= recording= recordEnd={...} items=[<peer>:<msg> alive= viewShown=
  element= initialElement= placed= visible=<0|1|none> readings=
  disagreements= changes= details={...}; ...]`, then ` - <reason>` on a
  refusal. `element=none` means no view, `visible=none` never asked;
  `readings` and `disagreements` count the placement gate's answers and
  those against it, `changes` the record's changes after the accepted mark;
  `target` is the last scroll top asked (for `cannot-move-off` the
  unreachable one) and `scrolls` the requests that moved the scroll. An
  accepted row reads `samples=0 recording=1`.
- `CHAT_LIST_PLACE_SCROLL: name= request= scrollTop= asked= now= moved=
  band= viewport=`, per scroll request: the top before, the target, and the
  top, band and area read right after the scroll's handlers ran.
- `CHAT_LIST_STAY: name= place= placement= finished= stayed= from= till=
  windowMs= hold= windowEnd= samples= maxGapMs= refusal= items=[<peer>:<msg>
  expected=on|off changes= stayed=
  window=[<sinceMs>:<alive><viewShown><placed><visible>, ...]
  deviation={sinceMs= alive= viewShown= placed= visible= element= viewport=
  scrollTop= details={...}}|none last={...}; ...]`, then ` - <reason>`.
  `window=` lists every mark inside the window, `sinceMs` from `from`, `-`
  for a reading not asked (a destroyed message).

**Placement refusals.** The `CHAT_LIST_PLACE:` row always carries each
message's last geometry and reading (`element=none`, `visible=none` when
never read), each alive message's reading asked once more on the deciding
sample for every refusal but `no-list`. The reasons of `cannot-fit` and
`cannot-move-off` carry them for every message, that of
`reading-disagrees` for each message that disagreed. Of the other reasons
only `no-target` for a message destroyed while the stage waited and
`no-view` at the deadline carry them, and `out-of-place` naming a message
not placed carries those of the messages against the placement; otherwise
read the row. Before the deadline the stage decides only an acceptance,
the run's resolution refusals, a list or message destroyed while it
waited, `cannot-fit` and `cannot-move-off`; at the deadline the first of
`no-view`, `cannot-fit`, `cannot-move-off`, `out-of-place` and
`reading-disagrees` that applies names the last sample.

| Refusal | Meaning |
| --- | --- |
| `no-list` | No widget, one that is none of the three lists or not the owned widget of its `Ui::ElasticScroll` (nothing scrolled), or a list destroyed while the placement waited. |
| `no-target` | No message named, an id the list's session does not know (nothing scrolled), or a message destroyed while the placement waited. |
| `no-view` | No own view of a message when the stage began, for an off placement too (nothing scrolled), or an on placement's view lost and not shown again by the deadline. |
| `cannot-fit` | On only: on a usable layout that read the same for 400 ms, the views' band is taller than the visible area or a view reaches past its sides; decided at once, without a further scroll. At the deadline the last usable layout is named so too, settled or not, the reason then starting `at the deadline, on a layout that never read the same for 400 ms`. |
| `cannot-move-off` | Off only: on such a layout, with the messages not all placed, the scroll top putting the area's edge 8 px past the nearest view is outside `[0, scrollTopMax()]`; `target` keeps it, and the reason names the room the loaded content leaves on that side. Nothing is scrolled to an edge to load more. |
| `out-of-place` | At the deadline the messages fit or could move, but there was no usable layout, the layout stayed pending, a message was not placed, or the placed geometry had not settled; the reason names the first, with the scroll counts. |
| `reading-disagrees` | At the deadline every message was placed and settled, but some reading disagreed (true off, false on); the reason lists those messages with `readings=` and `disagreements=`. |

**Stay refusals.** The earliest deviation across the messages decides; on
one sample, geometry wins. The reason names the message, what it did (`was
destroyed`, `left the <placement> placement` or `held the <placement>
placement, but its reading disagreed`), its time in the window and its mark.

| Refusal | Meaning |
| --- | --- |
| `not-placed` | The placement had not finished accepted when the stay began: `the placement '<name>' refused: <refusal> - <reason>`, or `the placement stage '<name>' did not finish (skipped or not run)`. |
| `did-not-stay` | A message left its placement or was destroyed inside the window; or, with no deviation, the record ended before it sampled the window's end (the list or every message destroyed, or the run finishing): `the record ended at +N ms, its last sample at +M ms, before the window's end at +W ms: <recordEnd>`. |
| `reading-disagrees` | The geometry held, but a message's reading disagreed with the placement inside the window. |

## Lifetime and cleanup

A placement owns its 50 ms action timer, cancelled in its then and,
idempotently, in the `Runner::onFinish` release the helper registers itself.
After an acceptance it owns the record - a 100 ms timer, one merged
subscription to the owning scroll's three signals and at most one queued
sample guarded by a `std::weak_ptr` - until `Runner::onFinish`, the list's
destruction or the destruction of every placed message; a fixture teardown
that destroys the messages ends their records. The caller's callbacks, with
whatever they capture, live as long as the runner's stages and releases and
are called until the record ends, so whatever they reference must stay
valid that long. A stay owns nothing. No input event, paint request,
account or server write; the list is held by a `QPointer`, each message by
its id, and no element pointer is kept between samples. Lasting effects are
the scroll position left and whatever the scroll handlers loaded or marked.

## Verification

`Test::AppendChatListPlaceSelfTest(runner, chat)`, appended by an overlay's
`SetupScenario` after its session and chats-loaded waits (the repository's
`test_scenario.cpp` stays a no-op). It protects the placement gate, both off
directions, the refusals' order and names, and continuity read from every
record sample rather than a window's ends.

**Fixture.** [test_widget_fixture](test_widget_fixture.md), prefix
`chat-list place self-test: `, marker `CHAT_LIST_PLACE_SELFTEST`, no token or
ports: `chat` is a quiet chat of the Test session (loaded, folder known, no
unread messages), opened in its legacy `HistoryInner` (the fixture turns
`use-new-chat-view` off and restores it) in a window shown normally and
exposed, nothing activated. After the fixture's open gates, a gated group
injects four local outgoing client-id messages at the chat's bottom, top to
bottom: a text filler, two widget cards (a minimal no-script document with a
64 CSS px height anchor) and a text filler, each filler the owning scroll's
height in `st::messageTextStyle` lines plus 8. Nothing is sent. Its
`fixture gate:` Checks: both cards are laid-out widget cards
(`CheckWidgetMessage`), all four views are laid out, each filler is taller
than the visible area plus both off margins, and the cards' band fits it
with both margins. A failed gate makes the rest of the group `N/A`.

**Readings.** `fresh`: `Runtime::queryVisible()` through the self-test's own
accessor for the cards (`source=runtime`), `queryItemVisibility()` for the
fillers (`source=item`). `snapshot`: the cards' fresh readings taken off
below and never refreshed, the cached `_visible` failure's shape under
harness control. `flip`: the fresh reading XOR a flag a later stage sets.
The cached `_visible` is never read.

**Judges.** Each runs in the very next stage and shares nothing with the
helper's seam: the view from the history's blocks (`FixtureOwnView`), its
top by `HistoryInner::itemTop()` and its height, against the owning scroll's
`scrollTop()` and `height()` (`HistoryWidget`'s own visibility range), and
both fresh readings, `fresh` and `listed` (`queryItemVisibility()`). On means
wholly inside that range with both true; above or below, wholly past that
edge with both false. Stage names start with `chat-list place self-test: `,
the helper's own with `place in chat list: ` or `stayed in chat list: `
before that prefix, and each helper stage is followed by
`chat-list place self-test: <leg> - judge`. Stays, their
judges, the excursion and the flip skip with `the placement '<name>'
refused: <refusal>` (or `did not finish`), so a refused positive placement
makes its dependants `N/A` instead of failing them twice.

| Leg | Reading | Provokes | The judge requires |
| --- | --- | --- | --- |
| on together | fresh | The cards start above the area, under the below filler. | Accepted; every message started out of place; at least one scroll request and one that moved; each card on. |
| stayed on (hold 1.5 s) | fresh | Nothing moves. | Stayed; `till - from >= hold`; at least 5 samples; per card `stayed`, 0 changes, no deviation; still on. |
| move the list away, back on the placement | - | A scroll to the list's bottom for 300 ms, then back to the accepted top. | `fixture gate:` both cards wholly above with both fresh readings false while away, then on again at the accepted top. |
| moved away and back (hold 0) | fresh | The same record, its window now holding the excursion. | `did-not-stay`; per card the first deviation strictly inside `(from, till)`, at or after the excursion, `placed=0`; its last mark placed and the card on at the judge, so only continuity refuses; the reason names the deciding card's id, element and reading. |
| off below | fresh | From on. | As on together, each card below. |
| stayed off below (hold 1.5 s) | fresh | Nothing moves. | As stayed on, below. |
| snapshot the cards' fresh readings | - | Taken off below. | `fixture gate:` both known and false. |
| stale reading (3 s) | snapshot | The cards go on; the reading stays false. | `reading-disagrees` at or after the deadline, every card placed and read against; each card on with both fresh readings true, so only the stale reading disagreed. |
| off above | flip | From on, before the flip. | As on together, each card above. |
| stayed off above (hold 1.5 s) | flip | Nothing moves. | As stayed on, above. |
| flip the reading | - | Sets the flip flag, then waits 300 ms. | - |
| reading flipped (hold 0) | flip | The off-above record after the flip. | `reading-disagrees`; no window mark left the placement; per card the first deviation at or after the flip, placed and read against; each card still above. |
| cannot fit (3 s) | fresh | On, for the two fillers. | `cannot-fit` before the deadline with no scroll request, the band taller than `effective`. |
| cannot move off (3 s) | fresh | Off above, for the below filler, the list's last message. | `cannot-move-off` before the deadline with no scroll request, `target > scrollTopMax`. |

Every placement refusal leg also checks that the reason carries each
message's last geometry and reading. Rows:
`CHAT_LIST_PLACE_SELFTEST: leg={<leg>}
type=place|stay expected=<refusal> seen=[{<message>} view= element=
scrollTop= scrollHeight= wholly= above= below= fresh= listed=; ...] |
<CHAT_LIST_PLACE or CHAT_LIST_STAY row>` per judge,
`CHAT_LIST_PLACE_SELFTEST: leg={inject} fillerLines= lineHeight= layout=`
after the injection, the fixture's `CHAT_LIST_PLACE_SELFTEST_ENVIRONMENT`,
and a Check per oracle.

**Expected verdict.** PASS only when every Check passes and no stage is
`N/A` or timed out: three accepted placements, three held stays, and five
refusals by exact name - `did-not-stay` and `reading-disagrees` in a window,
`reading-disagrees`, `cannot-fit` and `cannot-move-off` at placement. A
`fixture gate:` FAIL (a window too small for both cards, widget messages
disabled, a chat with unread messages) is a fixture problem, never a pass.

**Teardown.** `Runner::onFinish` gets the fixture's release before any stage
(Note `chat-list place self-test: onFinish release: ...`), each placement's
release after it. After the group the fixture's `teardown` destroys the four
messages by id and restores `use-new-chat-view`, the chat, the exposure hint
and the window state, and `settle after the teardown` follows. The records
end when their messages or the list are destroyed; the `onFinish` releases
cover any path that skips the teardown.

**Not covered.** `WatchingOnScreen()` (it needs a Live, watching host);
`HistoryView::ListWidget` and Recent Actions, whose list reads are
`test_chat_show`'s, run on all three lists by
[its self-test](test_chat_show_self_test.md), while this module's logic is
list-agnostic; the queued sample after scroll signals apart from the 100 ms
timer (both feed one record); the placement refusals `out-of-place`,
`no-list`, `no-target` and `no-view`, the horizontal `cannot-fit` branch and
`cannot-move-off` below; the stay's `not-placed` and its record-ended
`did-not-stay`. Proposed, not part of the self-test and not run: a
disposable mutation build whose record samples only when a stay decides
(no 100 ms timer and no queued samples; the stay's until takes one record
sample once its window's end has passed), an ends-only record on which
`moved away and back` stays over the excursion, so its judge must FAIL.
Removing the timer and the queued samples alone would not reach that
judge: `stayed on` would never see a sample at its window's end and would
time out.

**Evidence.** Executed on macOS arm64 Debug against the Test account's
Saved Messages in the legacy `HistoryInner`, in the evidence campaign of
`2026/10/27/show-or-scroll-off-several-chat-list-messages-and-prove-they-stayed-in-the-harness`
(its `work/test.md`). All 73 Checks of the run's 36 stages passed, 72 of
them the self-test's and one the overlay's chat gate, with no stage
timeout and no `N/A`, so every leg passed: the accepted `on together`,
`off below` and `off above`; the held `stayed on`, `stayed off below` and
`stayed off above`; `did-not-stay` for `moved away and back` and
`reading-disagrees` for `reading flipped`, each on a record that had just
held; `reading-disagrees` for `stale reading`, `cannot-fit` for
`cannot fit` and `cannot-move-off` for `cannot move off`; and the
excursion's and the snapshot's gates. The cards' readings came from live
runtimes' `queryVisible()` (`source=runtime state=live`), which the
`snapshot` and `flip` readings derive from. The console was locked during
the run, which no oracle depends on: no Check reads pixels or focus. No
consumer uses the helper yet; Windows and the other platforms have not
built it, though the code is portable.

## Related guidance

[Chat-list show before input](test_chat_show.md);
[chat-list resolution](../chat-lists.md);
[widget fixture](test_widget_fixture.md);
[window exposure](test_window_exposure.md);
[gated groups](test_runner.md#gated-groups-and-recovery-stages);
[windowed observations](test_probe.md);
[failure diagnosis](../diagnostics.md).
