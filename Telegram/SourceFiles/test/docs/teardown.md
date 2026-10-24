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

The Test-session helper's account restore is such a stage too:
`Test::AppendTestSessionRestore` is the scenario's last stage and waits for
the re-shown window to settle, while the helper's finish verification in
`Runner::finish()` runs after every `onFinish` callback and is only the
backstop of a run whose restore stage did not run or did not finish. When
that backstop does switch the account back, or the scenario watchdog ends
the run while the restore stage is still waiting its drain, `finish()` waits
what is left of that post-switch drain, yielding to the event loop and
waiting again when its timer arrives before the drain's recorded end, and
only then checks a fresh reading before its `kFinishDrainDelay` wait,
`SCENARIO_RESULT`, `TEST_COMPLETE` and `Core::Quit()`; its quit fuse is
armed before that wait, with the deadline extended by it. The drain
mitigates the re-cache and does not prove it finished; `finish()` goes on at
once only when the restore is already settled - nothing was switched back,
or the restore stage waited its drain and checked a fresh reading. Because
the restore stage runs before the `onFinish` releases, every release of
such a scenario holds the exact Test session or object it acts on and never
re-reads `domain().active()` at finish.

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
