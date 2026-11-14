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

The watch also times the renderer's own work on that host's widget: a
disposable overlay puts a `WidgetWorkScope` into three `Tgw::RendererCore`
calls, and each scope of the core the watch's attachment registered records
one event into the watch's work log ([Renderer work](#renderer-work)).

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
  paint bound's refusal of the widget's frames, stops the `RendererCore`
  and retires the runtime without the host: the watch keeps `dead` null,
  or `terminated` with that destruction's `diedAtMs` once the retired host
  is destroyed. The runners read such a stop with the fixture's
  `WidgetDesktopStop()` ([test_widget_fixture](test_widget_fixture.md)),
  never from this record.
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

The renderer's own work on one widget, as the stress kit's
`packages/stress/CLIENTS.md` ("The renderer's own work") asks an own
renderer to time it, recorded as raw events; turning them into per-frame
samples is the consumer's (the stress runner groups each `frame()` attempt
with the work since the previous one, counts the attempts that replayed
apart and documents that grouping next to its figures).

### The overlay

No retained code times anything: `tgw_renderer.cpp` has no scope, counter
or condition, and without the harness this module defines no
`WidgetWorkScope` member. A task that measures applies a disposable hunk to
`tgw/tgw_renderer.cpp`, never commits it and restores the file afterwards.
Every added line sits inside `#if defined _DEBUG || defined
TDESKTOP_RELEASE_TEST_HARNESS`, so the hunk compiles in both harness
configurations and nowhere else:

- `#include "test/test_widget_host.h"`;
- `RendererCore::hostMessage()`: right after the stopped, `requestFrame` and
  `frozen` returns, `const auto workScope = Test::WidgetWorkScope(this,
  Test::WidgetWorkKind::Message);`;
- `RendererCore::tick()`: at entry, the same with `WidgetWorkKind::Tick`;
- `RendererCore::frame()`: right after the cached-frame `return _frame;`,
  `auto workScope = Test::WidgetWorkScope(this, Test::WidgetWorkKind::Frame);`,
  right after `const auto list = _renderer->paint();`,
  `workScope.displayList(list.size());`, and right after
  `auto painted = PaintFrame(list, *_backend, size, _renderer->dpr());`,
  `workScope.replayed(!painted.image.isNull());`. The flag reads the image
  this attempt's replay produced, before it is moved into `_frame`, never
  the retained `_frame`: a frame the paint bounds refuse keeps the card's
  last picture that fits it in `_frame`, so `!_frame.isNull()` would count
  that refusal as a replay.

Without the overlay a registered core records nothing, so an empty log for
a widget whose content changes is a missing overlay: an invalid
measurement, never zero renderer work.

### What the events include

All three run on the main thread, from the scope's construction to its
destruction:

- **`Message`**: one host message applied. `beginCanvasBatch()` and
  TwidgetCore's `receive()` (the batch's ops, an `anim` or a query, and,
  when no frame is due, the settle it starts at once: style, layout and the
  display list), the settled viewport answer on the load, then `finish()`:
  forwarding the answers to the host link (a post to the host thread), the
  clock refresh and the synchronous `updates()` handler,
  `Tgw::Runtime::coreUpdated()`, whose repaint or resize request and height
  memory are therefore included. The message's JSON parse on the host
  thread (`HostRenderer::send()`) and its `crl::on_main` hop come before it.
- **`Tick`**: the whole `RendererCore::tick()`, run by `step()` on the
  app's animation timer or by `wake()`: TwidgetCore's `tick()` (its timers,
  animations and a build a due frame deferred), forwarding its output and
  the `frame` message that answers the widget's `requestAnimationFrame`,
  and posting the change notice.
- **`Frame`**: one attempt at a picture past the cache, while the chat list
  paints. `paint()` (the settle: style, layout and the display list, or its
  reuse), `PaintFrame()` (the QPainter replay into an ARGB32 image on the
  CPU), `RoundFrame()` and, after a settle, `flushQueued()`. A size the list
  has not laid out yet runs `paint()` and keeps the earlier picture without
  a replay; that call is a `Frame` event too, with `replayed` false, while
  an attempt whose `PaintFrame()` returned an image has `replayed` true.
  One the paint bounds refused returned none, so it is `replayed` false,
  whatever picture the card keeps.
  `displayList` is the size of the list `paint()` returned.

Not included: the chat list's own painting around the card and its
`drawImage()` of the returned picture, the window's backing-store flush
(BitBlt) and DWM composition, `hostTheme()`, `hostLoaded()`, input
forwarding, every early return named above (a cached `frame()` included),
any host-thread or JS-thread work (`stats.jsMs`, the ops tap's parse and
scan) and other widgets' cores.

Overhead: an event runs from the constructor's last act, its clock read, to
the destructor's first, so it holds about one `crl::profile()` read
(QueryPerformanceCounter) and the registry lookup of any scope nested in it.
The lookup and the log reference copy before the start and the append after
the end are outside every event but are main-thread time the app's frames
see. A scope of a core nobody registered costs a walk of the registered
addresses, none while nothing is registered.

### Registration, nesting and storage

- **Which core.** `WatchWidgetHost()` of an armed watch (so only with an
  active harness) registers the `core` address it got, for its attachment's
  index in `hosts`; scopes compare their `this` with the registered
  addresses and never dereference either. `DisarmWidgetHostWatch()`
  unregisters every core of the watch. An address belongs to one live core,
  so a newer attachment of the same address, by any watch, replaces an
  older registration.
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
  counted twice. Scopes of different watches record independently.
- **Storage.** `ReserveWidgetWork(watch, capacity)` starts the log over for
  one level: it forgets earlier events and drops, allocates room for
  `capacity` events at once, so recording never allocates, and stamps
  `reservedAt`. The first `capacity` events to close after it are kept;
  each later one only counts in `dropped`, and a nonzero `dropped` makes the
  level invalid. Before the first reservation the capacity is 0 and every
  event is dropped. Size the capacity for the whole period the level reads:
  the app's animation tick runs every 8 ms (about 125 ticks a second), and
  messages and rebuilt frames come at most about as often, so 65536 events
  (2 MiB at 32 bytes each) cover more than two minutes at that pace.
- **Reading.** `ReadWidgetWork(watch, since)` copies the events whose
  `finished` is at or after `since`, in the order they finished: with one
  open scope per watch at a time, `finished` never decreases.

| `WidgetWorkEvent` field | Meaning |
| --- | --- |
| `kind` | `Message`, `Tick` or `Frame`, by the scope's place. |
| `started`, `finished` | `crl::profile()` microseconds at the scope's construction and destruction. |
| `displayList` | The display list size a `Frame` scope was given; -1 otherwise. |
| `replayed` | A `Frame` scope whose `PaintFrame()` ran and returned an image; false for an attempt that returned before the replay, for one the paint bounds refused, and for every other kind. |
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
  posted from its constructor (a stale validator bytecode does) cannot race
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
- The work registry, each work log and every scope are main-thread only,
  with no lock: registration in `WatchWidgetHost()`, unregistration in
  `DisarmWidgetHostWatch()`, `ReserveWidgetWork()`, `ReadWidgetWork()` and
  the `RendererCore` calls that hold the scopes all run there. A scope keeps
  a reference to its log, so a disarm or a released watch while it is open
  cannot leave it writing freed memory; its event still lands in that log.

## Lifetime and cleanup

`ArmWidgetHostWatch()` returns a shared watch that the armed map keeps until
`DisarmWidgetHostWatch()`; keep the caller's copy (the fixture's
`WidgetFixtureMessage::host`) until its last reading. Disarming stops later
constructions and unregisters the watch's cores, so their scopes record
nothing more; hosts already attached keep merging into their records until
they are destroyed. A work log keeps its reserved room until the watch is
released. The fixture disarms in `RemoveFixtureMessage()` and
`ReleaseWidgetFixture()`; a direct caller disarms on its `Runner::onFinish`
path. Nothing is written to the account, the disk or the clipboard.

## Verification

Executed in the app: the stress runner's Release dry runs and both measured
passes (one options call and one host per level, unscaled production
defaults, the measure override with the product's visibility, locale, DPR
and safe area kept, `loaded` with `loadedMs`, verbatim `dead` with
`diedAtMs`, and with the overlay nonempty work logs without drops); the
`replayed` flag and its overlay line are compiled (Debug, Win32, MSVC), not
yet run in the app, and the stress self-check counts it on synthetic events.
The overlay line on `PaintFrame()`'s `PaintedFrame` result above is a
description: the retained tree has no such line, so it compiles only in
the overlay of a task that applies it.

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
