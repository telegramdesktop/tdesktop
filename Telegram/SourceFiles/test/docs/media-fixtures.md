# Media fixtures and fixture gates

[Harness guide](../README.md) · [Helper index](helpers/README.md)

A media document is a fixture only after the run has watched it play. Metadata
does not decide this: an undecodable upload left on the shared test account can
report `song=1 audioFile=1 sharedMusic=1` with a plausible `duration`, so no
predicate over flags, title, performer, filename, or membership in some list can
tell it apart from real music. “The first song not already in profile Saved
Music” is exactly such a predicate, and it accepts an undecodable upload.
Observe playback instead:

1. Order the candidates the account already exposes by playability signals —
   already downloaded, then a longer duration, then a non-empty title or
   performer — and push known-synthetic names behind every other candidate.
   The order is a preference; nothing on it is trusted yet.
2. Really play each candidate in turn, through the ordinary production path
   (`Window::SessionController::openDocument()` with that candidate's own
   message as its `MessageContext`), and sample
   `Media::Player::instance()->getState(AudioMsgId::Type::Song)` around a
   bounded wait. Accept the first candidate whose `TrackState::position`
   strictly advances while `length > 0` and the playing id and context are
   still that candidate's own document and message.
3. Stop the probe with `Media::Player::instance()->stop(AudioMsgId::Type::Song)`
   after every attempt and assert the player is stopped, so no probe state
   leaks into the measurement that follows.
4. Feed the one validated document to every stage that needs it — the fixture,
   any injection, and the negative control — instead of re-deriving it per
   stage.

The reported `length` corroborates a probe's verdict but never decides it.
Until the stream reports its own duration,
`Media::Streaming::Player::prepareLegacyState()` substitutes the document's
declared duration, so a length equal to the declared duration means only that
no stream duration has arrived yet — a healthy candidate sampled early reads
the same way. It is a bad sign only together with a position that never
advances, while a document that really opened reports a stream-measured length
instead (`len=272910` against a declared `272000`). Keep acceptance anchored on
the strict position advance.

When a scenario's subject needs a streaming fixture, a stage immediately before
the measured action must prove the track is already advancing while the subject
still exists. Classify that stage as a fixture gate, not a check: reverting the
diff under test cannot change its reading, so its failure makes the acceptance
criteria `N/A` and the run a test flaw — never a `FAIL`. Without that gate, a
frozen reading taken after the action cannot be told apart from the product
failing.
