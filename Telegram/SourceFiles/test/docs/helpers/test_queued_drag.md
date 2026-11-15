# test_queued_drag

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_queued_drag.h](../../test_queued_drag.h).

## Use when

Deliver queued drag starts before the synthetic release. Read the contract below and the source header before use.

## Contract and usage

The queued-start drag: `AppendQueuedStartDrag(runner, { name, resolve,
skipReason, steps, modifiers, bound })` appends one stage, `queued-start drag:
<name>`, built from `Test::DragPressAndMoves` and `Test::DragRelease`, the two
halves `Test::Drag` is made of (`test_widgets.h`), and returns the
`QueuedStartDrag` reading it fills, which the stage Notes as one
`QUEUED_DRAG:` row (`QueuedStartDragText`); a skipped stage leaves it
`finished=0`, and the helper never Passes or Fails. The header states the
consumer's safety hook at `Window::MainWindow::launchDrag()` and
`Ui::FlatLabel::executeDrag()`, whichever the queued start reaches. With its
own self-test, `AppendQueuedStartDragSelfTest`, over purpose-built probe
widgets in a never-shown container on the primary window: a probe whose move
handler, once past `QApplication::startDragDistance()`, posts one
`InvokeQueued` and one `crl::on_main` call that each record whether the
release has arrived reads both before the release under the helper and both
after it under `Test::Drag`, the control; `Test::Drag` delivers exactly the
press, interpolated moves, release and leave its contract names, and the
helper delivers the same records; a probe destroyed by a queued call while the
helper waits is refused `target-destroyed` with no release; an absent target
is refused `no-target`; and a probe that swallows the queued calls aimed at it
is refused `bound-expired` after the bound and still gets its release and
leave. It needs no session, network or account, and starts no `QDrag`.

## Shared queued-work markers

The two markers the stage waits for are exported for other helpers that need
the same wait: `PostQueuedWorkMarkers(context)` posts one
`InvokeQueued(context, ...)` and one `crl::on_main(context, ...)`, in that
order, and returns a `QueuedWorkMarkers` whose `postedAt` is the posting time
and whose `qtQueueRan` and `mainQueueRan` flags each marker sets when it runs;
`ran()` is both. Posted after an input's delivery and drain, `ran()` means what
the header's "The guarantee" paragraph states, with the exclusions it names.
The context's death drops both markers unrun, and since they hold only the
returned object, a marker that runs late cannot change a reading its stage
already froze. Callers: `AppendQueuedStartDrag`, and `Test::AppendQpaMouseDrag`
([test_qpa_mouse.h](../../test_qpa_mouse.h)), which posts them to its target
after the last QPA move's settle and waits for them before the release.

## Related guidance

[Input delivery and the required safety hook](test_widgets.md#input-helpers);
[failure diagnosis](../diagnostics.md).
