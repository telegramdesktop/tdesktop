# A pin writes a thread shared-media key only in a forum peer

`HistoryItem::setIsPinned(true)` handed `topicRootId()` to
`Storage::SharedMediaAddExisting` unconditionally. `topicRootId()` falls back to
`Data::ForumTopic::kGeneralId`, which is `1`, in every peer, and
`SharedMedia::add(SharedMediaAddExisting&&)` *enforces* the key it is given — it
calls `enforceLists`, which emplaces thirteen `SparseIdsList`s and wires thirteen
`rpl` pipelines into the session-lifetime `SharedMedia::_lifetime`. So one pin in
an ordinary chat created a `(peer, 1, 0)` list that no reader in that peer ever
queries and nothing ever retires.

It also kept filling. `SharedMediaAddNew` — the overload
`HistoryItem::addToSharedMediaIndex()` uses — only *finds* the thread key. Before
the pin that `find` missed and the write was a no-op; after it the `find` hit, so
every later regular message of the peer was mirrored into the dead list across
all thirteen types for the rest of the session.

Gating that one add on `_history->asForum()` removes the only producer that could
create the key in a non-forum peer, so the list is never created and the `find`
never hits it again. `SharedMedia::enforceLists` has exactly three call sites, one
per `add` overload: `SharedMediaAddNew`'s thread key is `find`-only,
`History::addNewToBack`'s topic add is already guarded by `item->topic()`,
`ApiWrap::sharedMediaDone` returns early when `peer->forumTopicFor(rootId)` is
null, and every other `SharedMediaAddSlice` producer passes `MsgId(0)` literally.

The discriminator is `asForum()` and deliberately not `topic()`.
`HistoryItem::topic()` resolves through `Data::Forum::topicFor(rootId)`, a plain
`_topics.find`, so it is null for a real forum message whose `Data::ForumTopic`
object has not been created — while the topic list is still being fetched, or when
the topic arrived only as a message. A `topic()` gate would silently drop a *real*
topic key, which is exactly the defect the unconditional write was introduced to
fix. `asForum()` follows the `Data::Forum` object, which exists for as long as the
peer carries `ChannelDataFlag::Forum`, and is independent of topic loading.

Nothing else moves. A real forum topic still gets `(peer, root, 0)` written once,
including a forum General topic whose root genuinely is `1`, where that key is a
genuine reader key. A monoforum and a Saved Messages sublist still get their
`(peer, 0, sublistPeer)` write. Every reader in a non-forum peer — the pinned bar
and `HistoryView::PinnedTracker`, `Data::ResolveTopPinnedId`, and the Info
shared-media provider — builds its key from `Data::Thread::topicRootId()`, which
is `0` outside a real `Data::ForumTopic`, so all three still read `(peer, 0, 0)`
and still see the pinned id.

The unpin arm was left alone. It still passes `topicRootId()` to
`Storage::SharedMediaRemoveOne`, but `SharedMedia::remove(SharedMediaRemoveOne&&)`
looks the thread key up with `find` and skips an absent one, so the asymmetry is
unobservable: after this change the key cannot exist in a non-forum peer at all.

Rationale, the before/after measurements and follow-ups live in the AI task.
