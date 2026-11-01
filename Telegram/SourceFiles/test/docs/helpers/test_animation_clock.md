# test_animation_clock

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_animation_clock.h](../../test_animation_clock.h).

## Use when

Deterministic elapsed-time animation frames. Read the contract below and the source header before use.

## Contract and usage

`RunClockedFrames`: captures a short `Ui::Animations`-driven animation at
chosen elapsed times after one synchronous action, or a `ClickHandler` invoked
directly with its context, never through `ActivateClickHandler()`. The action
runs inside `Test::Settle` with `crl::now()` stamped right before the action
and right after its postponed calls were drained (`actionStarted` /
`actionFinished`); then, for each strictly increasing requested time, the main
thread sleeps in 1 ms steps until that many milliseconds after
`actionStarted`, the postponed calls are drained,
`Core::App().animationManager().update()` runs once - the helper's only
manager call - proven by a probe `Ui::Animations::Basic` of the helper's own,
whose received time is the tick, and a synchronous caller render follows. Each
frame reports its reached elapsed bounds `[tick - actionFinished, tick -
actionStarted]`. A requested time at or after the declared `animationEnd`, or
a tick whose upper bound reached it, is a first-class `TEST_RESULT: N/A` gate
with nothing rendered; a malformed request (`clocked frames refused`), a tick
that did not call the probe (`manager-did-not-tick`) and a render that did
(`render-advanced-the-manager`) are named `fixture gate:` FAIL rows, and the
loop goes on. `ClockedRequest`, `ClockedFrame` (`taken()`) and `ClockedRun`
carry the request, each frame and the run; `ClockedFrameText` prints a frame,
and every taken frame is one `NOTE: <name>: kind=frame ...` row. Its frame,
gate and refusal rows follow `Test::RequestTelemetryNumbers()` (`test_log.h`),
read once per call, through `ClockedFrameText`, `ClockedFrameSubject`,
`ClockedEndGateText`, `ClockedReachedGateText`, `ClockedNoTickText`,
`ClockedRenderTickText` and `ClockedRequestRefusal`, each also taking an
explicit `NumberFormat`, and the caller's `elapsed` and `animationEnd` print
among them, so they must never be a fixture secret or a number read from one.
With its own self-test needing only the primary window, as the parent of a
never-shown opaque bar widget, and no session, chats or account: a 200 ms
`anim::linear` `Ui::Animations::Simple` (scaled by `anim::SlowMultiplier()`)
started by a `LambdaClickHandler` is clocked at 25, 50, 75 and 125 % of its
duration in one synchronous stage; the handler runs once inside the action
window, each in-progress value read from the rendered pixels lies within the
transition at its reached bounds (within 1.5 device pixels), the three values
strictly increase, the fourth time is the helper's `N/A` row with no image,
and the bar stays hidden and not visible with no paint outside the renders and
one render paint per taken frame, with the bar's and the window's state
(`minimized`, `hidden`, `exposed`, and `screenLocked` from
`Test::ReadConsoleLock().locked()`) printed beside the frames; the window
itself is read, never changed. Two stages, eight checks plus that `N/A` row,
and an `animation_clock_frames` contact sheet. Its `Simple` lives in the bar's
`lifetime()`, so it needs no `Runner::onFinish`.

## Clocked frame capture

Capture a short `Ui::Animations`-driven animation — a slide transition, a
reveal, a button or card animation of a few hundred milliseconds — at chosen
elapsed times with `Test::RunClockedFrames` (`test_animation_clock.h`), not
at whatever runner tick comes next. Its value changes only on an
animation-manager tick: in Run 2 of
`2026/10/06/round-rich-message-edge-media-like-ordinary-media` twelve 200 ms
transitions sampled at the next 50 ms runner tick were already over. A
`ClockedRequest` names its rows (`name`) and carries exactly one of an
`action` or a `handler` with its `context`, strictly increasing `elapsed`
times in milliseconds after the action, an optional `animationEnd` and a
synchronous `render`. The helper starts a probe `Ui::Animations::Basic` of
its own, runs the action inside `Test::Settle` with `crl::now()` stamped
right before the action and right after its postponed calls were drained,
and calls a handler's `onClick(context)` directly, never through
`ActivateClickHandler()`, whose `crl::on_main` call would run only after the
stage returned. For each time it sleeps on the main thread until then,
drains the postponed calls — `Manager::update()` skips while the manager's
own `schedule()` callback is pending, and the drain runs it — calls
`Core::App().animationManager().update()` once, its only manager call, and
renders. The probe proves each tick and supplies its time: a tick that did
not reach it is refused as `manager-did-not-tick`, a render that did as
`render-advanced-the-manager`, each a `TEST_RESULT: FAIL: fixture gate:
<name>: frame <k> at <ms> ms` row, and that frame is not taken. An animation
started inside the action or in a postponed call it posted stands, at a tick
`T`, somewhere in the reached bounds
`[T - actionFinished, T - actionStarted]`, so judge a painted value against
the transition at both bounds. A time at or after the declared
end, or a frame whose upper bound reached it, is a first-class `TEST_RESULT:
N/A: <name>: frame <k> at <ms> ms - <reason>` gate with nothing rendered,
never a FAIL; a malformed request is one `TEST_RESULT: FAIL: fixture gate:
<name>: clocked frames refused` row, and nothing runs. Each taken frame is
one row, `NOTE: <name>: kind=frame index=<k> requested=<ms>
reached=[<lo>,<hi>] tick=<ms> waited=<ms> probeTicks=<n> renderTicks=<n>`
(`ClockedFrameText`). Every number in these rows is an ordinary integer
until `Test::RequestTelemetryNumbers()` (`test_log.h`) and a
`TelemetryNumber` after it, read once per call, so one call's rows share a
format: the frame row (`index=37p requested=4821p reached=[3764p,4829p]
tick=37645p ...`), the `frame 37p at 4821p ms` subject, the gate and
refusal reasons (`requested 4821p ms is at or after the declared animation
end 3764p ms`, `the tick reached [5937p,6148p] ms`, `probe calls 59p before
and after it, 7293p ms after the action started, waited 8352p ms`, `called
41p time(s) during the render`) and the request refusal's details
(`exactly one of action and handler must be set (action=1p handler=1p)`,
`elapsed times must be non-negative and strictly increasing, read
4821p,3764p`). A judge reads a value of either grammar with
`float(v.replace('p', '.'))`. `ClockedFrameText`, `ClockedFrameSubject`,
`ClockedEndGateText`, `ClockedReachedGateText`, `ClockedNoTickText`,
`ClockedRenderTickText` and `ClockedRequestRefusal` also take an explicit
`NumberFormat`; a gate or refusal text kept in a `ClockedFrame` or
`ClockedRun` keeps the format it was formatted in. The caller's `elapsed`
times and `animationEnd` are among those numbers, so under the request they
print in a format that hides digits from the secrecy scan: they must never
be a fixture secret or a number read from one.
Render with `Test::GrabWidget` / `Test::GrabRect`,
which are `QWidget::render`, never with `repaint()` or `processEvents()`;
keep the render cheap and save images after the run: Run 4 of the same
campaign took a ~140 ms pair capture per frame, and its +100 / +160 ms ticks
landed at 181 / 324 ms. Gate `anim::Disabled()`, which makes the first tick
jump to the end, and scale times by `anim::SlowMultiplier()`. Choose by the
subject: when the product's own paint sequence is measured — back-to-back
paints, a bound from a paint — use `PostPaintSampler`, which samples only
after real product paints and so needs a shown, exposed, painting window
(`Test::KeepWindowExposed` keeps one that another application covers
exposed without activating anything);
when a `Ui::Animations` value at a known elapsed time is measured, use the
clock, which awaits no paint and so runs the same in a minimized window or
on a never-shown widget. Neither advances a streamed video player, which
paces itself with its own `base::Timer` (see the failure row for an inline
video or GIF that stays on one frame). Confirm with
`AppendAnimationClockSelfTest`, which clocks a 200 ms linear bar started
from a click handler on a never-shown child of the primary window.
