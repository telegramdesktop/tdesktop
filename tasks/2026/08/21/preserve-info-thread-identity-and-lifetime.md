# Keep Info pages on their topic or sublist

An Info page is keyed by a peer, a forum topic, or a Saved Messages / monoforum
sublist. Four holes dropped that thread:

- `Controller::validateMementoPeer` compared the sublist but not the topic, so a
  topic media memento could be answered by the whole-forum page of the same type.
- `WrapWidget::returnToFirstStackFrame` compared only the peer, so a topic or
  sublist Profile could unwind onto a whole-chat Profile frame.
- `Polls::ListWidget::doCreateMemento` always used the peer constructor, so Back
  restored the whole-chat polls page.
- The wrap subscribed to `ForumTopic::destroyed()` but not
  `SavedSublist::destroyed()`, so a sublist Info page outlived its subject.

`SavedSublist::destroyed()` already fires for `applySublistDeleted` and for
`MegagroupInfo::takeMonoforumData()` (MonoforumAdmin clear while the taken
object is still alive in `setFlags`). The new subscription mirrors the topic
block and does not retain the sublist.

`_historyStack` frames are not purged. A wrap constructed for that sublist is
removed wholesale, so Back cannot land on its frames. That matches the topic
path: the subscription is constructor-time on the initial key.

The General topic (`ForumTopic::kGeneralId = 1`) is a real `ForumTopic*`. The
new `topic()` term therefore treats General and the whole chat as different
subjects.
