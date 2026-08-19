# The pinned bar's topic root comes from the active thread

`HistoryView::Controls::TopControls` built the `Storage::SharedMediaType::Pinned`
key triple from its own `_repliesRootId` field. For a channel-comments thread —
`ChatWidget` in `Mode::Replies` with no `Data::ForumTopic` — that names
`(discussionGroup, repliesRootId, 0)`, a key no `Storage::SharedMedia` list can
ever hold, while the messages of that thread are indexed under the whole-peer
`(discussionGroup, 0, 0)` list. This takes the root from `activeThread()`
instead, the same expression `HistoryView::PinnedTracker` — constructed two lines
above — and `HistoryView::PinnedWidget` already use.

The change is behaviour-neutral for every mode that can reach it today.
`Data::Thread::topicRootId()` returns `_topic->rootId()` for a forum topic (equal
to `_repliesRootId`, which `ChatWidget::lookupTopic()` resolves the topic from),
and zero for a plain `History` or a `Data::SavedSublist` (every `ChatViewId` that
sets `.sublist` leaves `repliesRootId` at zero). The monoforum half of the key
keeps using `_monoforumPeerId`: that field is
`(_sublist && _sublist->parentChat()) ? _sublist->sublistPeer()->id : PeerId()`,
which deliberately differs from `Data::Thread::monoforumPeerId()` for a Saved
Messages sublist, and shared-media storage supports only the monoforum form —
`ApiWrap::sharedMediaDone` discards any slice whose `monoforumPeerId` does not
resolve through `PeerData::monoforumSublistFor`, and the self peer is not a
channel.

Two things were established while making this change and are worth recording,
because both contradict how the defect was originally described.

The replies-root list is not stale, it is absent. `Storage::SharedMedia` creates
a per-thread list only from `SharedMediaAddExisting` and `SharedMediaAddSlice`;
`SharedMediaAddNew` and `SharedMediaRemoveOne` only `find` one. Every writer that
can pass a non-zero root takes it from `HistoryItem::topic()` or
`ForumTopic::rootId()`, and `ApiWrap::sharedMediaDone` returns before storing
when `peer->forumTopicFor(topicRootId)` is null. A non-forum discussion group has
no forum, so nothing ever creates that list and no removal can decrement it.

A channel-comments thread has no pinned bar at all. `setupPinnedTracker()` runs
only from `subscribeToPinnedMessages()`, which `ChatWidget` calls only for
`Mode::History`, for a topic, and for a sublist — never for `Mode::Replies`
without a topic. `_pinnedTracker` therefore stays null there for the widget's
whole life, and the three other functions that build this key all require it.
That matches the pre-migration code, where `ChatWidget::setupPinnedTracker()`
opened with `Expects(_topic || _sublist)`.

The alternative repair — making `HistoryItem::topicRootId()` report the replies
root for a reply in a discussion thread — was rejected. It would not have fixed
anything, since nothing creates the list either way, and `topicRootId()` also
feeds `FullReplyTo`, the serialized `Data::DraftKey`, and the outgoing
`top_msg_id` and `f_forum_topic` fields, so it cannot move without changing
persisted and protocol behaviour. The repository already carries
`HistoryItem::replyToTop()` for the replies root and discriminates between the
two in `ApiWrap::exportDirectMessageLink`.

Rationale, measurements and follow-ups live in the AI task.
