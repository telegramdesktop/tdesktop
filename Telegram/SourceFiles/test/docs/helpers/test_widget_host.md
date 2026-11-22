# test_widget_host

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API.
Source contract: [test_widget_host.h](../../test_widget_host.h).

## Use when

A scenario needs what a widget message's `twidget::WidgetHost` was built with
and what it reported, exactly: the options as they entered the `Job` that
builds the host, after an optional override of the measurement options (the
stress kit's measure pass), and the host's own `onChange` readings: its
`SandboxStats`, the validator's node count, `loaded` with the host's
`loadedMs`, the first `dead` reason verbatim with when it arrived, the diag
kinds with their counts and the first 20 console texts of level `error`.
Nothing else gives these: `WidgetHost` keeps its options private,
`Tgw::Runtime::Host` is incomplete outside `tgw_runtime.cpp`, the runtime log
(`Runtime::logText()`, `_summary`) rounds and clips the readings, and the
validated-ops tap sees only what reaches the renderer.

The watch also times Telegram's share of the renderer's work on that
host's widget: a disposable overlay puts a `WidgetWorkScope` into the
core's worker, `Tgw::RendererWork`, and its platform end, which run on the
widget render thread (`widget.render`, one thread for every widget's
worker), each given its `Tgw::RendererCore`'s address, and each scope of
the core the watch's attachment registered records one event into the
watch's work log, which the main thread reserves and reads under the lock
the scopes take ([Renderer work](#renderer-work)). The renderer itself runs
in the widget helper ([The helper's share](#the-helpers-share)).

Callers: the widget fixture arms a watch for a record whose `watchHost` is
set ([test_widget_fixture](test_widget_fixture.md)). Its consumers are the
stress-kit runner ([test_widget_stress](test_widget_stress.md)), which reads
the applied options for `config.applied`, the host readings for each run
entry and the work log for its renderer figures, and the widget conformance
runner ([test_widget_conformance](test_widget_conformance.md)), which arms
an empty-override watch on every run, so every option stays the product's,
and reads it once, in the run's log stage before the removal, for each
construction's `applied` `contentSafeArea` (its `safeArea` extra).

Use something else when:

- the validated ops, their counts or a widget's published result matter:
  [test_widget_ops](test_widget_ops.md);
- whether a card is live, interactive or painted now matters: the fixture's
  card readings and `Tgw::Runtime::state()`;
- the runtime's own log text is enough: the fixture's `RequestRuntimeLog()`.

## Preconditions and limits

- **Arm before construction.** `ArmWidgetHostWatch(id, measure)` acts on the
  constructions `Tgw::Runtime::construct()` starts from then on, matched
  against `HistoryItem::globalId()`: the message's `FullMsgId` plus its
  session's `uniqueId()`, as for the tap. The fixture arms it next to the tap,
  before `History::addNewLocalMessage()`. Arming an armed id returns its watch
  with its first override.
- **Active harness.** Both seams return the options unchanged or do nothing
  unless `Test::Active()` and the id is armed.
- **What the override changes.** Only the fields it gives: `policy`,
  `features`, `memoryLimitBytes`, `cpuLimitMs`, `cpuQuotaMs`,
  `cpuQuotaWindowMs`, `mainThreadQuotaMs`, `maxTimers`. `cpuKillAfterMs`,
  `visible`, `devicePixelRatio`, `locale`, `contentSafeArea`, `storage`,
  `data` and `context` stay what `DefaultHostOptions()` and
  `Runtime::construct()` set. In a Debug build the product multiplies
  `cpuLimitMs`, `cpuQuotaMs`, `cpuQuotaWindowMs` and `mainThreadQuotaMs` by
  `kCpuLimitScale` (4); that scale stays in every field the override does not
  give, and `applied` shows it.
- **What `applied` is.** The options right after the override: exactly what
  the `Job`'s `.options` holds, which `Build()` passes to the `WidgetHost`
  constructor unchanged. Only the measurement fields are kept; storage, data,
  context and any id never are.
- **One host per measurement.** Each armed options call adds one `applied`
  entry and each attachment one `hosts` entry, in order, never merged. A
  construction whose build the runtime discards (`Runtime::constructed()`'s
  early returns: a newer generation, a failed build, quitting or no current
  content, crashes, a changed theme, storage or data) leaves an `applied`
  entry without a host. A consumer that measures one host
  accepts a reading only with exactly one entry of each. More name a rebuilt
  or replaced host: that reading is invalid for it, and the helper does no
  replacement-host recovery. An older host's callback keeps writing only its
  own record.
- **Freshness.** `stats` are the last stats the host delivered. The engine
  posts them at most every 200 ms, with a trailing post, and refreshes
  `memoryBytes` at most once a second; `statsChangedAt` is when a merge last
  found them changed, not a fresh observation at read time.
- **Load and death times.** `loadedMs` is the host's own, from its creation
  to `loaded`. `diedAtMs` is steady-clock time from the host's creation to the
  merge that first saw `dead`, on the host thread right after the host stored
  it: the same clock and origin as `loadedMs`. The creation is
  `WidgetHost::createdAt_`, read once by value with the private-member
  accessor of [test_chat_show](test_chat_show.md) and the fixture; the options
  call runs before the `crl::async` build and is not the creation.
- **Death reason.** `dead` is the first reason seen, verbatim (a counted
  strict cap reads like `Ops limit exceeded: 12000 operations in one batch
  (max 10000)`), not the renderer's localized stopped label. Destroying a host
  that is still alive ends its engine with `terminated`, which arrives as one
  more change: read the widget's own death before the removal; `terminated`
  after it is the destruction. `dead` comes only from the host's
  `WidgetDiagnostics::dead`. A stop the desktop decides itself, such as a
  paint bound's refusal of the widget's frames (a paint that ran out of
  memory for its own workspace, an emoji tile request's preparation among
  it, included; found on a work turn, in `RendererWork::hostedFrame()` or
  `paintFrame()` or the platform end's
  checks of the helper's traffic, published to the chat by its posted
  answer), stops the
  `RendererCore` and retires the runtime without the host: the watch keeps `dead` null, or `terminated` with that
  destruction's `diedAtMs` once the retired host is destroyed. The runners
  read such a stop with the fixture's `WidgetDesktopStop()`
  ([test_widget_fixture](test_widget_fixture.md)), never from this record.
- **Logs and diags.** `errors` keeps the first 20 console texts of level
  `error`, verbatim; `errorLogs` counts them all. The host keeps at most 5000
  console entries (`logs`), so later ones reach neither. `diags` is kind to
  count, as the host's diagnostics have them.
- **Not measured.** No window frame, presentation or host-thread timing, no
  JS time beyond `stats.jsMs`, and no renderer timing at all unless the
  disposable overlay places the work scopes ([Renderer work](#renderer-work)).

## Minimal use

Through the fixture, with the stress kit's measure options; the fixture's
gates, its `Runner::onFinish` release and the stage that opens the chat come
first ([test_widget_fixture](test_widget_fixture.md)):

```cpp
const auto message = Test::AddWidgetFixtureMessage(*fixture, u"stress"_q);
message->watchHost = Test::WidgetHostOverride{
	.policy = "compat",
	.memoryLimitBytes = size_t(64) << 20,
	.cpuQuotaMs = 10000.,
	.cpuQuotaWindowMs = 1000.,
	.mainThreadQuotaMs = 10000.,
	.maxTimers = 256,
};
runner->add({
	.name = u"inject the watched widget"_q,
	.run = [=] {
		const auto error = Test::InjectWidgetMessage(
			*fixture,
			*message,
			state->html,
			390,
			0);
		Test::Check(error.isEmpty(), u"the widget is injected"_q, error);
	},
});
runner->add({
	.name = u"wait for loaded or dead"_q,
	.run = [=] { state->started = crl::now(); },
	.until = [=] {
		const auto r = message->host
			? Test::ReadWidgetHost(*message->host)
			: Test::WidgetHostReading();
		const auto done = !r.hosts.empty()
			&& (r.hosts.front().loaded || r.hosts.front().dead);
		return done || (crl::now() - state->started > kLoadBound);
	},
	.then = [=] {
		const auto r = message->host
			? Test::ReadWidgetHost(*message->host)
			: Test::WidgetHostReading();
		Test::Check(
			(r.applied.size() == 1) && (r.hosts.size() == 1),
			u"one options call and one host"_q,
			u"applied=%1 hosts=%2"_q.arg(
				QString::number(int(r.applied.size())),
				QString::number(int(r.hosts.size()))));
	},
	.timeout = kLoadBound + Test::kDefaultStageTimeout,
});
// With the renderer overlay applied, time one level's renderer work:
runner->add({
	.name = u"start the measuring window"_q,
	.run = [=] {
		Test::ReserveWidgetWork(*message->host, 65536);
		state->windowStart = crl::profile();
	},
});
// ... after the window:
//     const auto work = Test::ReadWidgetWork(
//         *message->host,
//         state->windowStart);
//     Test::Check(
//         !work.dropped && (work.registered == 1) && !work.events.empty(),
//         u"the renderer work was timed whole"_q);
// ... then remove the message and assert the barrier:
runner->add({
	.name = u"remove the widget and wait for its host"_q,
	.run = [=] { Test::RemoveFixtureMessage(*fixture, *message); },
	.until = [=] { return Test::FixtureBarrierDecided(*message); },
	.then = [=] {
		Test::Check(
			message->tap
				&& Test::WidgetTapFinished(Test::ReadWidgetOps(*message->tap)),
			u"the widget's hosts are gone"_q);
	},
	.timeout = Test::kWidgetBarrierBound + Test::kDefaultStageTimeout,
});
```

## Readings and refusals

`ReadWidgetHost(watch)` returns a `WidgetHostReading` snapshot on the main
thread: `applied`, one `WidgetHostApplied` per armed options call, and
`hosts`, one `WidgetHostSample` per attached host, each copied under its own
record's lock. The helper refuses nothing; it records, and the consumer
decides.

| `WidgetHostApplied` field | Meaning |
| --- | --- |
| `policy`, `features` | The policy and opt-in features the host was built with. |
| `memoryLimitBytes`, `cpuLimitMs`, `cpuQuotaMs`, `cpuQuotaWindowMs`, `cpuKillAfterMs`, `mainThreadQuotaMs`, `maxTimers` | The limits, as the `Job` got them. |
| `visible`, `devicePixelRatio`, `locale`, `contentSafeArea` | The product's values, never overridden; recorded so a consumer can show they were kept. |

| `WidgetHostSample` field | Meaning |
| --- | --- |
| `core` | The `RendererCore` address `WatchWidgetHost()` got, compared with a card reading's `core`, never dereferenced. |
| `attachedAt` | `crl::profile()` at the attachment. |
| `stats`, `statsChangedAt`, `statsChanges` | The last `SandboxStats`, when a merge last found them changed and how many merges did. |
| `nodes` | The validator's node count of the last merge. |
| `loaded`, `loadedMs`, `loadedAt` | The host reported `loaded`; its own `loadedMs`; `crl::profile()` of the first merge that saw it. |
| `dead`, `diedAtMs`, `deadAt` | The first dead reason, verbatim; steady-clock ms from the host's creation to the merge that saw it; that merge's `crl::profile()`. |
| `diags` | Diag kind to count. |
| `errors`, `errorLogs`, `logs` | The first 20 `error` console texts; every `error` entry; every console entry the host kept. |
| `changes` | `onChange` calls merged, the attachment's snapshot not counted. |

Every `...At` stamp is 0 until its event. A reading with more than one
`applied` or `hosts` entry is diagnostic only for a single-host consumer.

## Renderer work

Telegram's share of the renderer's own work on one widget, as the stress
kit's `packages/stress/CLIENTS.md` ("The renderer's own work") asks an own
renderer to time it, recorded as raw events; turning them into per-frame
samples is the consumer's (the stress runner groups each raster attempt
with the work since the previous one, counts the attempts that replayed
apart and documents that grouping next to its figures). The host messages,
the twidget renderer's `receive()`, `tick()`, style, layout and display-list build,
the settle before a paint and every layout query run in the widget helper
([Remote engines](test_widget_fixture.md#remote-engines)), and
`RendererWork` drives the shipped platform end (`RendererPlatform`,
`tgw/tgw_renderer_platform.cpp`): the scopes time its answers to the
helper's requests and its replay of each list the helper painted
([The helper's share](#the-helpers-share)).

### The overlay

No retained code times anything: none of `tgw_renderer_work.cpp`,
`tgw_renderer_platform.cpp` and `tgw_renderer.cpp` has a scope, counter or
condition, and without the harness this module defines no
`WidgetWorkScope` member. A task that measures applies a disposable hunk
to `tgw/tgw_renderer_work.cpp` and `tgw/tgw_renderer_platform.cpp`, never
commits it and restores the files afterwards. Every added line sits inside
`#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS`, so the hunk
compiles in both harness configurations and nowhere else:

- `#include "test/test_widget_host.h"`;
- **`Frame` in `RendererWork::raster()`**, right after its unchanged-picture
  return under "A released core shows its last picture": it covers only the
  request for a paint (`requestPaint()`) while the route lives and the
  work has not stopped before its detach, with no replay: a hosted work
  keeps no list of its own, so one whose route is gone paints nothing, and
  a turn whose draw keys (the CSS height among them) already match returns
  before it, unless a paint was refused for its device size or layout
  since they were painted. The picture it owes (`RendererReflow`) is
  served when a current paint's picture goes into its mailbox, so a later
  dirty state waits for the chat's draw; a paint refused for its device
  size or layout keeps it until a change lets it ask once more; a new size
  that withdraws that picture asks once more, and a kept document's geometry
  moving after a demanded
  picture asks once more only for that demand, later moves waiting for the
  chat's next draw;
- **`Frame` for a list the helper painted**: in `RendererPlatform::pump()`,
  for an entry whose bytes are a `Frame` or a `FrameDelta`
  (`twlite::remote::kind_of()`), opened right after `QtBackend::workUnlessHeld()` returned an admitted
  `Work` (the entry's outer Work, its admission and charge one meter
  sample) and closed when `consume()` returned, so an entry the hold
  keeps queued records nothing. It covers the decode, `hostedFrame()`'s
  checks (the request's sequence and kind, the state, the size serial and
  epoch, the finite device size) and `paintList()`'s
  replay of the whole list (the helper's damage is ignored), with
  `displayList(frame.list.size())` and `replayed()` from `paintList()`'s
  `layout`; a frame that is no longer current, of another device size,
  with a layout the card cannot take (`FitsLayout()`) or answering a
  work that stopped and has not detached yet has no replay
  and stays `replayed` false;
- **`Message`** at the same place for every other entry: one backend
  request answered (a font, a text width, an image or a canvas read) or one
  message received (a canvas run, the state, pointer and selection reports,
  the glue's controls).

The platform end has no core of its own: the hunk hands it the work's
`_chat` (one more member that `RendererWork::makeHosted()` sets) and, so
that `paintFrame()` can set the frame's two values, keeps a pointer to the
scope it opened for that entry, cleared when the scope closes. `consume()`
itself opens no Work: the place is `pump()`'s loop, where the Work is.
A tick has no Telegram work to time: `askTick()` sends a request, and the
helper's answer is a `Frame` entry like any other. No such hunk has been
compiled or run yet.

Every scope gets `_chat.get()`, the `RendererCore` whose worker it is:
`WatchWidgetHost()` registers that address, and the worker's own `this`, a
different object, is never registered, so a scope given it records nothing.
`_chat` is a `base::weak_ptr` the core made on the main thread, and `get()`
reads it atomically on the render thread: while the core lives it is the
registered address, and once the core is destroyed it is null, so a scope
opened after that records nothing and takes no lock (the worker's `gone`
checks leave few such calls before the render thread destroys it). A
scope already open when its core is destroyed on the main thread keeps
its log and lands its event; the address is only compared, never
dereferenced.
Without the overlay a registered core records nothing, so an empty log for
a widget whose content changes is a missing overlay: an invalid
measurement, never zero renderer work.

### What the events include

Every event runs on the widget render thread, from the scope's
construction to its destruction: none runs on the main thread, so none
inside the chat list's paint
([The delivery to the chat](#the-delivery-to-the-chat)). The thread runs
every widget's worker in one queue, so an event of one widget
never overlaps another widget's render-thread work, while the main
thread's work, and every host and JS thread's, can overlap any event.

Not included: the chat list's own painting around the card and its
`drawImage()` of the held picture, `RendererCore::apply()` and the chat's
handling of each answer, the window's backing-store flush (BitBlt) and DWM
composition, `freeze()` and `RendererCore::hostFrozen()`, input
forwarding (the chat's posts and the worker's input calls), the rest of a
work turn (the deferred detach cleanup `finishDetach()`, the size
request's `setWidth()` and `setMaxHeight()` to the helper, and the
answer's post), every early return named above (an unchanged picture's
`raster()` included), the time a posted call, message or turn waits in the
render thread's queue behind its own widget's or other widgets' work, any
host-thread or JS-thread work (`stats.jsMs`, the ops tap's parse and
scan), the helper's work ([The helper's share](#the-helpers-share)) and
other widgets' cores.

Overhead: an event runs from the constructor's last act, its clock read, to
the destructor's first, so it holds about one `crl::profile()` read
(QueryPerformanceCounter on Windows, `mach_absolute_time()` on macOS) and
the locked lookup of any scope nested in it. The lookup and the log
reference copy before the start and the append after the end, each under
the work lock, are outside every event but are render-thread time that
every widget's work shares. The main thread holds that lock only for a
registration change, a reservation's swap and a reading's copy, so a
scope's wait for it lies outside its event and is short, unless it meets
the copy of a long log (read once a level, after its window). A scope
given a null core returns at once; one of a core nobody registered costs
the lock and a walk of the registered addresses, an empty walk while
nothing is registered.

### The delivery to the chat

The worker's results reach the chat at one posted delivery, outside every
event. A work turn applies the pending size request, asks for a paint when
wanted and sends one `RendererAnswer` (the content and load state, a stop)
through `RendererWork::deliver()`, a `crl::on_main` hop from the render
thread to the main thread, guarded by the core's `base::weak_ptr` so that a
gone core gets nothing, to `RendererCore::apply()`. The picture waits in
the work's own mailbox (`PicturePost`), put there as soon as it is painted
and published by the next work turn: the answer, or for a turn with only a
picture and no later post of its work still queued one wake, carries a
serial, and `apply()` takes the newest picture at the answer it came with
or at a post after the last answer before it, a newer one having replaced
any the chat had not taken yet. That call publishes the chat's height, held
picture and state and fires `updates()`, whose
handlers, `Tgw::Runtime::coreUpdated()`, `stillUpdated()` or
`coreStopped()`, make the repaint or resize request, the height memory and
any retirement there. What an event changes thus reaches the chat at the
earliest two posted hops after it (its work turn, in the render thread's
queue, then the delivery, in the main thread's), and only a later list
paint draws it.

The chat list's paint draws the picture `RendererCore::frame()` selects
from what was delivered. Its calls into the core (`frame()`'s demand and
drawn notices, `setCut()`, a size request, a retirement's detach) each post
a call to the render thread, where the worker only stores the value and
posts its work turn, and a detach leaves its cleanup to that
turn's `finishDetach()`, so no scope runs inside a list paint and a list
paint's time holds no renderer work. The two run on different threads and
can overlap in time: a reading that pairs events with list paints must use
their stamps, not nesting.

### The helper's share

What these events leave out is the helper's work: the engine, the
validator, the served host's glue, the twidget renderer's whole receive, tick and
paint, the encoding of each Frame, both stream hops and the time an entry
waits in the render thread's queue or for its widget's hold to end. The
one suite field whose definition holds that work is the stress kit's
`renderer` (`packages/stress/CLIENTS.md`, "The renderer's own work":
applying the batch, style, layout, building the display list and replaying
it): the work log gives Telegram's replay share of it only, and a run that
reports it there must say so.

The helper's share comes from the ops tap, not from this log: in Debug,
`Test::TimeWidgetHelperWork()` before the construction makes the helper's
tap time each outermost renderer operation of the glue thread (a served-host
message, a theme, a tick, a paint with its display-list build and Frame
encoding, other platform messages, the glue's controls) and the parts of it
spent waiting for Telegram's answers, and `ReadWidgetHelperWork()` returns
them on this log's clock ([Helper renderer work](test_widget_ops.md#helper-renderer-work)).
Those waits overlap Telegram's own events answering the requests, so a
total of both processes counts each once. The stress runner asks for it on
every level and writes, per level, `desktop.rendererScope`:
`telegram-replay` (no helper records: a Release harness, whose helper is a
Release build with no Debug code) or `telegram-replay+helper` (with them),
and `desktop.helperWork`, the window's helper events
per kind with their wall, waited and own time, summed with checked
additions; a dropped or malformed record, and an event whose time would
overflow a sum, is a level error
([test_widget_stress](test_widget_stress.md#hosted-levels)). The measured passes are Release builds, so their
`renderer` is Telegram's replay share and is labelled so; a Debug run
measures the two shares side by side. An
attribution needs a known nontrivial workload with both readings nonempty
and an unasked control level. Every other reading of this module (the
options, stats, nodes, load, death, diags and logs) is the hosted host's
own `WidgetHost::diagnostics()`, which the hosted client fills from the
helper's reports.

### Registration, nesting and storage

- **Which core.** `WatchWidgetHost()` of an armed watch (so only with an
  active harness) registers the `core` address it got, for its attachment's
  index in `hosts`; scopes compare the `core` they were given (the
  worker's `_chat.get()`, null once that core is gone) with the registered
  addresses and never dereference either. `DisarmWidgetHostWatch()`
  unregisters every core of the watch; a scope already open on the render
  thread at that moment still lands its event in the log it holds. An
  address belongs to one live core, so a newer attachment of the same
  address, by any watch, replaces an older registration.
- **Replaced hosts and address reuse.** Each event carries its attachment's
  `host` index, so a second host's work is never merged silently, and a
  single-host consumer refuses a reading with more than one host. A core
  destroyed while its watch stays armed leaves its address registered:
  another message's core later allocated there would be timed into this log
  until the watch is disarmed or that core attached. Disarm when the
  level's message is removed (the fixture does) and measure one widget
  alive at a time.
- **Nesting.** Only the outermost open scope of a watch records; one opened
  inside it is inert, its time already in the outer event, so no work is
  counted twice. Every scope runs on the one render thread, so the
  outermost is the call stack's. Scopes of different watches record
  independently.
- **Storage.** `ReserveWidgetWork(watch, capacity)` starts the log over for
  one level: it forgets earlier events and drops, allocates room for
  `capacity` events at once, so recording never allocates, and stamps
  `reservedAt`; it allocates the new room before taking the work lock,
  swaps it in under the lock and frees the old room after it. The first
  `capacity` events to close after it are kept; each later one only counts
  in `dropped`, and a nonzero `dropped` makes the level invalid. Before the
  first reservation the capacity is 0 and every event is dropped. Size the
  capacity for the whole period the level reads: the worker's frame timer
  ticks every 8 ms, lib_ui's animation cadence (about 125 ticks a second),
  and messages and rebuilt frames come at most about as often, so 65536
  events (2 MiB at 32 bytes each) cover more than two minutes at that pace.
- **Reading.** `ReadWidgetWork(watch, since)` copies, under the work lock,
  the events whose `finished` is at or after `since`, in the order they
  finished, and counts the watch's registered cores in the same hold: with
  one open scope per watch at a time, all on the render thread, `finished`
  never decreases.

| `WidgetWorkEvent` field | Meaning |
| --- | --- |
| `kind` | `Message` or `Frame`, by the scope's place; [the overlay](#the-overlay) places no `Tick` scope. |
| `started`, `finished` | `crl::profile()` microseconds at the scope's construction and destruction, read on the render thread with the process clock of the main thread's stamps. |
| `displayList` | The display list size a `Frame` scope was given; -1 otherwise. |
| `replayed` | A `Frame` scope whose replay of a helper's list gave a picture (`paintList()`'s `layout`); never the retained canvas or held picture. False for a paint request, for a frame with no replay, for one the paint bounds refused, and for every other kind. |
| `host` | The index in `WidgetHostReading::hosts` of the attachment that registered the core. |

| `WidgetWorkReading` field | Meaning |
| --- | --- |
| `events` | The recorded events finished at or after the stamp asked for. |
| `reservedAt`, `capacity` | The last reservation's stamp (0 before one) and room. |
| `dropped` | Events that found the log full since that reservation; nonzero: incomplete. |
| `registered` | Cores of this watch's attachments still registered. |

## Production seams

- `.options = Test::OverrideWidgetHostOptions(_item->globalId(),
  std::move(options))` is the `Job`'s options initializer in
  `Tgw::Runtime::construct()`, on the main thread, before the `crl::async`
  build: one call per construction.
- `Test::WatchWidgetHost(_item->globalId(), *_host->pointer, _live.get())` in
  `Tgw::Runtime::constructed()`, on the main thread, after the stale-build
  checks, the `Runtime::Host` creation and `syncHostVisibility()`, immediately
  before `RendererCore::bindHost()` and `WidgetHost::start()`.
- Inactive harness: one `Active()` check each. An active harness with an
  unarmed id: one lookup of the armed map each, no clock read, lock,
  insertion, diagnostics copy or callback change. The options identity still
  costs a call and a move of the options; it is not free.
- Without the harness, a build that is neither Debug nor configured with
  `TDESKTOP_RELEASE_TEST_HARNESS`
  ([Release builds with the harness](../launch.md#release-builds-with-the-harness)),
  `test_widget_host.cpp` defines only these two functions, the identity and a
  no-op; every other declaration of the header is left undefined there.
- The work scopes are not production seams: they exist only in the
  disposable overlay ([The overlay](#the-overlay)), and no retained product
  file calls anything of the timing API.

## Threads and lifetime

- Arming, disarming, reading and both seams run on the main thread; the armed
  map, keyed by `GlobalMsgId`, and each watch's `applied` and host list are
  main-only.
- The attachment runs one synchronous `WidgetHost::onHostThread()` call from
  main: it merges the host's `diagnostics()` and sets `onChange` there, so no
  change falls between the snapshot and the callback, and a diag the host
  posted from its constructor (a mismatched validator bundle does) cannot race
  the callback's assignment. Before `start()` the host thread runs at most
  such a diag and no task of it waits on main, so the hop is short and cannot
  deadlock. The product makes its own synchronous host calls off main
  (`ApplyTheme()` runs on a worker, because its `probe()` can wait on the
  engine); this one is the harness's, for armed hosts only.
- Each host's record has its own mutex. The callback merges into it on the
  host thread (`twidget-host`), reading its two clocks before taking the lock
  and calling nothing while holding it; reads copy it on main.
- The callback captures the record, the host's creation time by value and the
  product's earlier `onChange`, which this tree never sets. It holds no
  `WidgetHost` pointer, no `HostPointer` or weak pointer and no `Reservation`
  slot, so it never prolongs the host or its slot. It is destroyed with the
  host in `~WidgetHost` on the `HostDeleter` worker, after the host thread was
  joined, so no merge follows it.
- Observer cost: with `onChange` set, `WidgetHost::changed()` copies the whole
  diagnostics (up to 5000 console entries, the actions and bridge calls) on the
  host thread for every change, which the product deliberately avoids
  (`HostLink` reads the resize anchor in place for this reason). Stats changes
  come at most about five times a second, plus each log, diag, load and death.
  It is host-thread work, not JS or main-thread work, and a measurement
  discloses it.
- The work registry and every work log share one mutex, the work lock.
  Registration in `WatchWidgetHost()`, unregistration in
  `DisarmWidgetHostWatch()`, `ReserveWidgetWork()` and `ReadWidgetWork()`
  take it on the main thread. The scopes, in `RendererWork` and
  `RendererPlatform` calls on the widget render thread, take it for their
  lookup and their append only,
  never across the timed work, and `displayList()` and `replayed()` touch
  only the scope. No other harness or product lock is taken while it is
  held, so it has no lock order to keep. The lock and the registry are
  never destroyed: a render thread that `Tgw::Runtime::Finish()`'s bounded
  join left running at quit cannot meet them destroyed by static
  destructors.
- A scope keeps a reference to its log (a `std::shared_ptr`, its count
  atomic), so a disarm or a released watch on the main thread while the
  scope is open on the render thread cannot leave it writing freed memory;
  its event still lands in that log, and whichever thread drops the last
  reference frees it.

## Lifetime and cleanup

`ArmWidgetHostWatch()` returns a shared watch that the armed map keeps until
`DisarmWidgetHostWatch()`; keep the caller's copy (the fixture's
`WidgetFixtureMessage::host`) until its last reading. Disarming stops later
constructions and unregisters the watch's cores, so their scopes opened
after it record nothing (one open on the render thread at that moment still
lands its event); hosts already attached keep merging into their records
until they are destroyed. A work log keeps its reserved room until the
watch and every scope holding the log are released. The fixture disarms in `RemoveFixtureMessage()` and
`ReleaseWidgetFixture()`; a direct caller disarms on its `Runner::onFinish`
path. Nothing is written to the account, the disk or the clipboard.

## Verification

Executed in the app: the stress runner's Release dry runs and both measured
passes (one options call and one host per level, unscaled production
defaults, the measure override with the product's visibility, locale, DPR
and safe area kept, `loaded` with `loadedMs`, verbatim `dead` with
`diedAtMs`, and with the overlay nonempty work logs without drops). Those
runs timed a renderer that ran in Telegram, with the scopes in
`RendererCore`'s own calls, while `frame()` still rastered inside the list
paint and `updates()` fired inside a message's event; the `replayed` flag
was compiled (Debug, Win32, MSVC) on that renderer's replay, not run in
the app, and the stress self-check counts it on synthetic events. The work
is `RendererWork`'s, on posted turns, since
`2026/11/16/deliver-widget-heights-and-finished-pictures-as-messages`, and
runs on the widget render thread, where `_chat` is a `base::weak_ptr` and
the scopes share the work lock with the main thread, since
`2026/11/14/run-the-widget-renderer-on-a-thread-of-its-own`; the hunk of
[The overlay](#the-overlay) has not been compiled or run yet.

Executed by the conformance task
`2026/10/29/report-the-desktop-conformance-and-run-the-stress-kit-at-widget-sandbox-v0-3-0`,
recorded in its `work/smoke.md`: the watch's first in-app run, on macOS
arm64 in a Release build with `TDESKTOP_RELEASE_TEST_HARNESS` and no LTO,
through the conformance runner's empty-override watch. Every loaded run of
`auto/04-values-units`, `auto/31-stop-text`, `auto/36-safe-area` and
`visual/17-time-corner` had one `applied` entry with the product's
`contentSafeArea`: all 0 for the two messages with a caption below the
widget, `bottom` 26 with `cornerRight` 118 for the two `safeArea` entries,
equal to `auto/36-safe-area`'s own `contentSafeAreaInset` reading.

## Related guidance

[Validated-ops tap](test_widget_ops.md);
[widget fixture](test_widget_fixture.md);
[Release builds with the harness](../launch.md#release-builds-with-the-harness);
[scenario teardown](../teardown.md).
