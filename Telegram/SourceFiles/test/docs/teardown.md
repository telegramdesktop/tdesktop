# Scenario teardown before quit

[Harness guide](../README.md) · [Helper index](helpers/README.md)

A scenario that deliberately leaks its `State` so it outlives a stage owns the
release of everything that `State` holds: every `rpl::lifetime`, every
watcher, every raw cross-stage pointer, and every `base::Timer`. Register
that release with `Runner::onFinish`. `finish()` runs those callbacks on
every path that reaches it — stage timeout, the scenario watchdog,
skip-to-end, and normal completion — exactly once, after the ticker and
watchdog are cancelled, before the `kFinishDrainDelay` fuse drain, and
before `Complete()` / `Core::Quit()`. A callback registered after `finish()`
has already run executes immediately and is never silently dropped.

A final teardown stage is still useful for work that must happen while the
session still exists and the runner is still stepping, but it is not the
release point. The stage is not guaranteed to run: a stage that times out,
and the scenario watchdog, make `Runner` finish immediately and skip every
stage after it, while a stage whose assertions `FAIL` does not — that run
still reaches its teardown. `onFinish` still runs in those abort paths, and
it also runs when a teardown stage already ran, so the callback must be safe
to call after teardown.
A main-window geometry change belongs in such a stage, one that waits for
the change to settle, and never in `onFinish`: it releases nothing and
starts an asynchronous chat-background re-cache that must not still be
painting at quit (see the `d->allThreads.isEmpty()` row under “Failure
diagnosis” below).

Skipping the hook costs a whole run. A leaked `State` whose two
`rpl::lifetime`s were never released kept observers subscribed to
`Storage::Facade`'s and `Data::Session`'s streams while `~Main::Session` tore
those streams and their items down. The run reached `TEST_COMPLETE` after a
clean sweep and then died with `Caught signal 11 (SIGSEGV)`, no assertion line
anywhere in the run's logs, and a 0-byte minidump because the runner had to
kill the process. With the release registered, three consecutive runs exited
0, wrote no crash report and left no minidump. After a timeout the same death
can follow `TEST_COMPLETE`, so read it as this signature rather than as a
second product fault, and keep the leaked `State`'s session-observing
subscriptions no wider than the stages that need them.
