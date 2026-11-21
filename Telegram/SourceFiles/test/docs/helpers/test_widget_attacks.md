# test_widget_attacks

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API (a permanent stage pack).
Source contract: [test_widget_attacks.h](../../test_widget_attacks.h).

## Use when

A scenario runs widget-sandbox's `corpus/attacks` through the app's own widget
view and needs, per file, whether it ran, loaded, died, what its validated ops
held under the `--attacks` leak definition, and how one real click through the
bubble went, plus loopback controls the app itself sends for an external
network listener. `Test::AppendWidgetAttackAudit` appends the whole linear
audit: each HTML file becomes a local outgoing `messageMediaWidget` message
in the legacy
`HistoryInner` of a Test chat, its hosts are tapped with
[test_widget_ops](test_widget_ops.md), it is shown with
[test_chat_show](test_chat_show.md), clicked once when live and watched for
1500 ms, as the reference `--attacks` loop in widget-sandbox's
`packages/desktop/harness/main.cpp` does. Its fixture gates, message
injection, card readings, runtime log, network controls and release are the
shared [widget fixture](test_widget_fixture.md)'s; the corpus, the verdicts,
the stages per file and the rows are the pack's own.

Caller: the evidence campaign of
`2026/10/24/audit-the-widget-attack-corpus-through-a-validated-ops-tap`, and
reruns of it at a widget-sandbox or twidget pin move.

Use something else when:

- one widget's validated ops are enough: arm a tap yourself
  ([test_widget_ops](test_widget_ops.md));
- the question is conformance (manifests, gestures, pixels): that is the
  conformance runner's ([test_widget_conformance](test_widget_conformance.md)),
  not this pack's;
- the claim is about network requests: this pack only sends the positive
  controls; the listener that observes them, and the source audit of the
  routes a loopback listener cannot see, are the campaign's.

## Preconditions and limits

- **Session and chat.** Append it after the overlay's `waitEvent`,
  `waitForSessionReady` and `waitForChatsLoaded`, with `Test::UseTestSession`
  and `Test::AppendTestSessionRestore` around it when the copy may open on
  another account ([test_session_gate](test_session_gate.md)). `chat` must be
  loaded in the Test session the primary window shows, with its folder known
  and no unread messages; the pack hardcodes no chat, path or port.
- **Fixture gates.** The window's session is on the Test environment,
  no layer is shown, the window is not hidden, no proxy the
  account can use (the selected one, and the rotation list when rotation is on)
  points at a loopback control port, the window is shown normally and exposed
  ([test_window_exposure](test_window_exposure.md)), and the chat opens in its
  own `HistoryInner` of the legacy `HistoryWidget`, loaded at its bottom. A
  failed gate is one `fixture gate:` FAIL; every file is then not run, still
  with its `ATTACK` row, and the release still runs.
- **Corpus.** `corpus` is absolute. HTML files are the entries whose
  extension is `.html`, in the byte order of their UTF-8 names (the reference
  `htmlFiles()` `std::sort`). `only` selects names in that order; an unknown
  name, an unreadable or empty file is a named FAIL. Every other entry not
  starting with a dot gets an `ATTACK_NON_WIDGET` row; it is known only when no
  HTML file contains its name and every non-empty line of it is a line of the
  HTML file of its stem (at `v0.1.2`: `mem-cap-probe.js`, inlined in
  `mem-cap-probe.html`). No other dependency is looked for.
- **One file at a time.** A file's message is destroyed, and the removal
  waits up to 10 s for its tap to finish (`WidgetTapFinished()`: every
  selected factory released, every built wrapper destroyed and every
  hosted work selected for the runtime's accounting final)
  ([test_widget_fixture](test_widget_fixture.md#readings-and-refusals)).
  A file whose reading taken after that wait is not finished stops the
  audit: the remaining files are not run, by name, the abort giving the
  tap's counts (`WidgetTapCounts()`).
- **Time.** Per file about 4-8 s in Debug for a healthy widget, and about
  70 s for one that reaches every bound (8 s layout, two 10 s shows, 25 s
  load, the 1.5 s watch, 3 s log, 10 s barrier). The runner's watchdog (`TDESKTOP_TEST_WATCHDOG`, at most 3600 s)
  is a real process-lifetime limit: split the corpus over launches with
  `only`, each with its own controls and teardown, and keep one accepted
  attempt per file name.
- **The click.** The press point is the reference click's `clientX 5,
  clientY 5`, in CSS px from the card's top-left. The click show accepts a
  live runtime whose interactive core is its current core, attached and
  accepting input, with a cached frame cut for the current card size; it asks
  no renderer quiescence. A card taller than 75 % of the list's scroll is
  shown by a band at its top that holds the press point; the row says whether
  the whole card was shown (`wholeCard`).
- **Hosted widgets.** Each file's widget runs in the widget helper
  ([Remote engines](test_widget_fixture.md#remote-engines)) and the tap
  sits on its hosted host
  ([test_widget_ops](test_widget_ops.md#hosted-widgets)): its batches are
  scanned at the host's `onRenderOps`, its load is the mirrored ack pair or
  the host's own rendered acknowledgment
  (`renderedHosts`), and its click the renderer's click decision for the
  marked interaction: the generation the mark before the press found newest
  and the press the click played, the primary press the chat dispatched to
  a work of that generation while the click's own mouse-down was being
  delivered, bound to its work and pointer serial at that dispatch
  (`markedWork`, `markedSerial`) and to the number its work sent it with
  (`markedPress`). The audit plays `Click()`'s press, release and leave
  itself (`PlayMarkedClick()`), the press's synchronous `sendEvent()`
  inside `DeliverMarkedPress()`, which seals the binding when it returns,
  before the settle drains the postponed calls and before the watch: a
  mouse-down the chat never dispatched leaves the mark unbound, and no
  later press can take it. The audit takes the work of the core it clicks
  (`click.playedWork`) and counts a decision only for a press played to it.
  The helper's glue reports that decision for the press's up (the
  `Clicked` control), and only the marked press's report, after its up's
  reply and while the marked generation is still the newest, counts
  (`clicksCompleted`); with the Debug helper's tap the mirrored trusted
  click of the marked generation, until a newer one is selected, must say
  so too. The host's retirement on main revokes a mark whose decision has
  not been reported (`markDetached`), since the render thread can still
  report the up and the decision of a reply it took before the work's
  detach reached it. A down or up, sent or answered, is never a click: a
  withheld up, a route retired between the down and the up, a target the
  down's handler
  replaced (both replies, no decision), an earlier press or an older
  generation that completes after the mark, a press the chat dispatched
  before the mark that the work sends and clicks only after it, a genuine
  click of the same work after a mouse-down that reached no core, and a
  decision that comes after a newer generation was selected all stay
  `click-not-observed`.
- **Network controls.** Before the first file and after the last, per port, one
  `QTcpSocket` connection and one `QUdpSocket` datagram to 127.0.0.1, both
  `QNetworkProxy::NoProxy`, each carrying one line
  `{"phase","pid","port","protocol","token"}` (compact JSON, then `\n`). An
  empty token, no port or a port out of range makes both control stages N/A
  (`network controls unavailable: ...`), never a pass. A written control is
  not a received one: only the external listener's record of it is the
  positive control.
- **Not measured.** No network request is observed here. The log fields come
  from the runtime's bounded summary and never prove an absence. Renderer
  `violation` refusals are counted apart and never change the scanner's
  leak count, but a file with one is never clean. Only macOS arm64 Debug
  has built it; the code is portable.
- **One session.** Each file's tap is armed with the message's
  session-qualified id (`GlobalMsgId`: its `FullMsgId` and the fixture
  session's `uniqueId()`), kept with the file and used for every disarm,
  so another logged-in session's message with the same `FullMsgId` is never
  tapped into a file's record.

## Minimal use

```cpp
void SetupScenario(not_null<Runner*> runner) {
	const auto session = Test::UseTestSession(runner);
	runner->waitEvent(u"launch_finished"_q);
	runner->waitForSessionReady();
	runner->waitForChatsLoaded();
	Test::AppendWidgetAttackAudit(runner, {
		.corpus = u"<absolute path of widget-sandbox's corpus/attacks>"_q,
		.chat = <the Test chat's PeerId>,
		.only = qEnvironmentVariable("<SUBSET>").split(
			QChar(','),
			Qt::SkipEmptyParts),
		.token = qEnvironmentVariable("<TOKEN>"),
		.ports = { <port>, <port> },
	});
	Test::AppendTestSessionRestore(runner, session);
}
```

## Stages

`widget attack audit: tap, scanner and verdict self-check` (every
`RunWidgetOpsSelfCheck(WidgetOpsExchangeLeg::Apart)` row as a Check, then
the verdict controls of Verification; it then waits up to
`kWidgetOpsExchangesTimeout`, 30 s, with the event loop running, for the
exchange leg its `WidgetOpsExchanges` plays on a thread of its own and
Checks the leg's rows, normally seven, or one failed row when its thread did
not start or the leg threw. Taking them sets `selfChecked` whatever they
read, so that flag says only that the stage got that far, never that the
rows passed), `report the corpus`, `capture the
baseline and show the window normally`, `keep the window exposed`, `open the
chat in the legacy HistoryWidget`, `network control before the first file`,
then per file `<name>: arm the tap and inject the widget message`, `show in
chat list: widget attack <name> load`, `<name>: wait for load, death or
failure`, `show in chat list: widget attack <name> click`, `<name>: click once
and watch`, `<name>: read the runtime log`, `<name>: remove the message and
wait for its hosts`, then `network control after the last file`, `totals`,
`teardown` and `settle after the teardown`. Every bound ends inside its
stage's until with a named reading; each stage timeout is its bound plus
`kDefaultStageTimeout`. The press is in the run of the stage right after the
click show, which revalidates the accepted view, core and geometry first.

## Readings and refusals

Every row is `NOTE: <marker>: <compact JSON>`, written through `Test::Note`
and escaped by `QJsonDocument`.

| Row | Fields |
| --- | --- |
| `ATTACK_ENVIRONMENT` | `pid`, `wallMs`, `token`, `ports`, `proxy` (`mode` enabled/system/disabled, `selected` and `list` as `type`/`host`/`port`, `rotation`; never credentials), `proxyConflicts`. |
| `ATTACK_CORPUS` | `dir`, `error`, `html`, `nonWidget`, `ignoredDotEntries`, `requested`, `unknown`, `unreadable`, `selected`, `network` (`available` or the unavailability). |
| `ATTACK_NON_WIDGET` | `name`, `file`, `bytes`, `lines`, `referencedBy`, `inlinedIn`, `known`, `disposition`. |
| `ATTACK_NET_CONTROL` | `phase` (before/after), `protocol` (tcp/udp), `port`, `connected`, `written`, `lineBytes`, `error`, `localPort`, `startedMs`, `doneMs` (ms since the epoch), `line`. |
| `ATTACK_LOG` | `file`, `source` (`requestLog`, `logText` when the retired runtime keeps no callback, `logText-at-bound`, `unavailable`), `summaryWork`, `summaryOwn`, `diedText`, `pending`, `waitedMs`, `chars`, `truncated`, `text` (its first 32768 characters). |
| `ATTACK` | One per selected file, see below. |
| `ATTACK_TOTALS` | `path`, `files`, `ran`, `loaded`, `complete`, `clean`, `leaks`, `notes`, `gaps`, `batches`, `violations`, `liveAtClick`, `clicksAttempted`, `clicksObserved`, `clicksDecided`, `clickSource` (as `click.source` below), `layers`, named lists (`count`, `files`) `dead` (`DeadFiles()`: each file whose row died, `<file>: <reason>`, its row's `deathReason`), `notLoaded`, `notRun`, `incomplete` (`<file>: <reasons>`, with a fixture refusal's reason in parentheses), `leaking`, `violating`, then `unknownRequested`, `selfChecked`, `selfCheckControl`. |

`ATTACK` fields: `file`, `index`, `bytes`, `sha256`, `item`, `path` (`final`
from the remove stage, `totals` for a file never started, `onFinish` after an
abort), `startMs` / `endMs` (ms since the epoch, injection to final reading),
`ran`, `notRun`, `loaded`, `notLoaded`, `died`, `deathReason`,
`deathMeasured`, `complete`,
`incomplete`, `fixtureRefusal`, `clean`, `fixture` (local shape, widget
media, payload),
`loadShow` and `clickShow` (`finished`, `shown`, `kind`, `refusal`, `reason`,
`local`, `card`, `wholeCard`, `rect`, `viewport`, `hover`, `hovered`, `paints`,
`elapsedMs`, `readiness`), `loadOutcome` (`loaded`, `died` (a charge the
tap counted, `WidgetTapCharged()`, never a raw death), `stopped`,
`failed`, `no-runtime`, `bound`), `loadWaitMs`, `stateAfterLoad`,
`liveAtClick`, `click` (`outcome`, `details`, `attempted`, `pressMs`,
`source`, `playedWork` (the work id of the core the click hit), then
`markedGeneration`, `markedWork`, `markedSerial`, `markedPress`,
`markSuperseded`, `markDetached`, `downsConfirmed`, `upsConfirmed`,
`clicksCompleted`, `pressStepsElsewhere`, `trustedClicks`, `trustedEvents`
and `eventTypes`
since the mark taken right before the press), `watchMs`, `stateAfterWatch`, `log` (`source`, `chars`,
`sandboxHeader`, `diedLine`, `diedText`, `summaryWork`, `summaryOwn` and
`sampledRun`, the sampled work's record as `WidgetHostedRunText()` gives
it, null without one), `layers`, `barrierClosed`, `readingFinal` (a
`final` row whose barrier closed: the only row whose reading, its death
included, is final), `barrierMs`,
`itemGone`, `examplesTruncated`, and `tap`: the reading's counts, with
`renderedHosts`, `hostedSelected`, `hostedFinal`, `desktopCharges`,
`sparedRuns`, `unaffectedRuns` and `WidgetTapCounts()` as `counts`, and
its `leakExamples`, `noteExamples`, `gapExamples` and
`violationDetails`. `click.source` is the build's own reader
(`ClickSource()`), written in every row and in the totals, a file refused
before any renderer of it was selected included: `helper-mirror` (the
Debug helper's mirrored trusted click with the marked press's click
decision) or `click-decision` (a Release build with the harness, the
marked press's click decision alone). Only a positive outcome
(`observed`, `decided`) is bound to the played press.

The statuses are independent, so a death with a leak is in both lists:

- `ran`: at least one wrapper was built (`wrappersBuilt >= 1`); otherwise
  `notRun` names why (gate, abort, injection, a fixture check, no renderer
  built). A fixture check that refuses the file after a renderer was built
  leaves it ran, with the batches seen so far, its reason in
  `fixtureRefusal` and `fixture-refused` among `incomplete`: its load wait,
  click, watch and log stages were skipped;
- `loaded`: `ackQueries > 0 && ackResults > 0`, or `renderedHosts > 0` (a
  hosted host's own rendered acknowledgment, the one a Release harness
  has); `notLoaded` is ran and not loaded;
- `died` and `deathReason` (`Judge()`): for a file whose tap selected
  hosted works (`WidgetTapHosted()`), the shared reading of the final
  charges, `ReadHostedDeath()`
  ([test_widget_ops](test_widget_ops.md#hosted-deaths-and-final-charges)):
  dead when any selected work was charged, read from the `final` row's
  reading, so a charge after a healthy sample kills the file and no
  record's eviction revives it. `ParseLog()` keeps the first line of the
  summary the log took only when it is a `Died: ` line (`diedText`,
  verbatim; empty after any other first line, such as a healthy
  summary's `Loaded: ...`) and pairs it with the work that summary was
  sampled from (`summaryWork`) and `summaryOwn` (a `Fresh` or `Final`
  summary, `WidgetLogSummaryOwn()`). Only an own summary's work and line
  are read, so the reason is measured (`deathMeasured`) only from that
  work's own final charged record, and the log stage's reading
  (`ReadLogDeath()`) keeps a reason so measured for that work once its
  record is evicted. Otherwise a dead file reads
  `charged, reason unmeasured`, a stale summary's line and work staying in
  the row as diagnostics. No raw `Died: ` line is a death by itself: every
  host of an ended helper prints `Died: the widget engine process ended`,
  spared or charged, and the display log's text before its sandbox header
  (a renderer's multiline stop reason) can neither forge nor hide the
  summary's line. The display log's own readings, a sandbox header
  (`log.sandboxHeader`) and a `Died: ` line right after its first one
  (`log.diedLine`), are row diagnostics no verdict reads. A file whose tap
  selected no hosted work (refused, skipped or finished before any
  renderer of it was selected) is judged nothing: no host was sampled for
  it, so its `diedText` is empty and its `summaryWork` 0, nothing charged
  it, and it keeps the verdict `Judge()` starts from, alive, `deathMeasured`
  false and `deathReason` empty. Its `notRun` reason (see `ran`) and its
  `click.source` (above) are read apart from its death, and the totals
  list it under `notRun`, never under `dead`;
- `click.outcome`: `observed` (with the Debug helper's tap,
  `trustedClicks >= 1` of the marked generation and `clicksCompleted >= 1`
  of the played press, its click decision: a nonzero `markedWork ==
  playedWork` with a bound `markedSerial` and `markedPress`),
  `decided` (without the Debug helper's tap, `clicksCompleted >= 1`
  with `markedWork == playedWork` and a bound `markedSerial` and
  `markedPress`; an answered down or up alone never), `not-observed`,
  `show-refused`,
  `revalidation-failed`, `died-before-press` (not live at the press and
  charged, `WidgetTapCharged()`, stopped or failed), or `not-live` (not
  live after the load wait, so no click was owed);
- `incomplete`: `fixture-refused` (see `ran`), `aborted`, `hosts-pending`,
  `watch-missing` (the 1500 ms watch did not run to its end), `scan-gaps`,
  or `click-<outcome>` for a card live after its load wait whose click was
  neither `observed` nor `decided` and that was not dead before the
  press;
- `complete`: ran and nothing incomplete; `clean`: complete, loaded, not
  dead, no leak and no renderer violation. A refusal is never clean, though
  it is not a leak.

Checks, each prefixed `widget attack audit: `: the self-check rows and the
`verdict self-check:` rows; `the corpus directory lists its HTML files`,
`every requested file name is a corpus HTML file`, `every selected HTML file
was read`, `every non-HTML corpus entry has a known disposition`; the
`fixture gate:` rows; one per network control (`network
control <phase> - the <tcp|udp> control to 127.0.0.1:<port> was ...`);
`validated-ops leak scan discriminates` (`Test::DiscriminatingScan` over every
batch, the self-check control's leaks as its control: the same copied scanner
must have matched known leaks in the same run), `no attack file leaks a URL
into the validated ops`, `every host message of every file was scanned: no
scan gaps`, `every loaded file's validated batches were observed`, `every
requested file ran`, `every file that ran was observed completely: ...` (no
fixture refusal, every watch run, every card live after its load wait
either clicked with its click `observed` or, without the Debug helper's
tap, `decided`, or `died-before-press` with no press played, every
barrier closed, no scan gap), `every file's factories and wrappers finished
before its final reading` (its barrier, `WidgetTapFinished()`, its hosted
works' finals included), `no
attack file opened a layer`, `CheckNoBlockedLaunches(no widget handed
anything to the operating system)`, and the teardown and settle rows. Deaths,
not-loaded files and renderer violations are Noted, not failed, and never
counted clean.

A pass proves, for the files of that launch: each ran in the product's own
`HtmlWidget` view of a local message of the fixture session, no fixture
check refused it and its watch ran, every batch every host reported was
scanned at its `onRenderOps` with zero leaks and no gap, every card live
after its load wait either took one real press and release whose click
was `observed` or, without the Debug helper's tap, `decided`, or was
charged, stopped or failed before the press and got none
(`died-before-press`), every host
finished and every hosted work had its final before its final reading, so
each row's death was read from final charges, no layer opened, no OS
hand-off, and each network control stage that ran wrote its controls (an
empty token, no port or a port out of range makes both N/A, and none is
sent). It does
not prove that no request was made (that needs
the listener and the source audit), that a control was received, that a
card was clicked, which element the click hit, or anything about files of
other launches.

## Lifetime and cleanup

`Runner::onFinish` is registered before any stage: on every exit path it
stops the self-check's exchange leg without waiting for it
([test_widget_ops](test_widget_ops.md#lifetime-and-cleanup)),
disarms every tap by the session-qualified id it was armed with, destroys
every local message by id, closes the control
sockets, leaves the list, restores the `use-new-chat-view` value it found, the
baseline chat, the stacking hint and the window state, then writes the
`ATTACK` row of every file that has none (`path` `onFinish`, not final) and,
when the totals stage did not run, the totals row without checks. The
teardown stage runs the same release while the session lives, and the settle
stage waits the window's repaint settle. A layer shown over the chat is hidden
at once and recorded, never confirmed. Nothing is sent: the messages are
client-id local, so their Web Storage never reaches the disk; the scroll
position is not restored.

## Verification

The pack runs `Test::RunWidgetOpsSelfCheck(WidgetOpsExchangeLeg::Apart)`
first (52 rows in Debug) and the exchange leg's rows last in the same stage
([test_widget_ops](test_widget_ops.md#verification)); it packs the
scanner's 2-leak, 1-note control into every campaign. In the same stage pure
controls of the row verdict, on literal final rows, must hold: a loaded,
clicked, watched and finished file with no leak, gap or violation is
complete and clean (the positive control); the same row refused by a
fixture check after its renderer was built, with its click and watch
skipped, is ran, keeps its reason in the refusal, lists `fixture-refused`
and `watch-missing`, and is neither complete nor clean; the clean row with
one renderer violation and no leak is complete and not clean; the clean
row whose click is `decided` is complete and clean, and the same row with
`not-observed` lists `click-not-observed` and is not complete. Then
`ClickOutcome()` on literal readings of a marked press: without the Debug
helper's tap an answered down, an answered down and up with no decision
and an empty reading give `not-observed` and a decision `decided`, but a
decision with no bound press or for another core's work `not-observed`;
with it a decision alone and a mirrored click alone give `not-observed`
and both `observed`. Last, each
reader's consumer (`ClickOutcome()`, then `Judge()` on the clean final row)
on real taps, one message with two generations marked on the newer, each
played press dispatched inside `DeliverMarkedPress()`: hosted generations
(stub hosts, never started) driven through the product's
`WidgetPressDispatched()`, `WidgetCoreDetached()` and
`WidgetPressObserved()`, each clicked up followed by its host's mirrored
trusted click while the host is not retired, read by the
`click-decision` and the `helper-mirror` readers. A retirement detaches
the core (`WidgetCoreDetached()`, as `RendererCore::detach()` calls it
before it closes the link), then retires the host (`WidgetHostRetired()`):
from the retirement on its host mirrors no click, while the route still
reports the queued up and the click its renderer decided, as the render
thread reports a reply it took before the detach reached it. In
each the older
generation's press, down before the mark, clicking after it; the newer's
played press down and up with no click; after another mark a press the
chat dispatched before it that clicks after it, beside a played press that
clicks none; after another a delivery that dispatched nothing, then a
genuine click of the same work (beside the generation's trusted click
where the build reads one: with the Debug tap); after another
a played down whose up never comes; after another a played press whose
generation a third selection superseded between its down and its click;
and on a later generation a press taken before another mark that clicks
after it (the same trusted click where the build reads one), then a played
down whose core retires before its up, which comes after the retirement,
and on a later generation still a played down whose core is detached
before its up, which comes, clicked and mirrored, between the
detach and the host's retirement, give `not-observed` and an incomplete row
(`click-not-observed`); for that last press the check also requires, read
both before and after the retirement, the down bound (`markedSerial` and
`markedPress`), one `downsConfirmed`, `markDetached`, the up and the
click counted in `pressStepsElsewhere` and no completion. The
played stable click, bound inside its delivery and clicked after it, gives
`decided` through the `click-decision` reader and, in Debug, `observed`
through the `helper-mirror` reader, each with a complete, clean row, and
so does the played click a later generation completed before its core
retired; a
Release build's tap reads no mirror, so there that reader gives
`not-observed`. The same reading with no played work gives `not-observed`
in every reader.

The verdict controls end with the death verdicts
(`CheckDeathVerdicts()`), on real taps of messages of no history, every
work played as `WidgetHostedPlay` plays it and each file read by
`ParseLog()`, the log stage's `ReadLogDeath()` and `Judge()` on the
reading after the removal: a spared work and its charged
replacement printing the same `Died: the widget engine process ended`
make the file dead whichever is sampled, measured only from the charged
work's own sample, while a spared sample beside a work withdrawn first is
alive; a display log whose stop reason forges a sandbox header and another
`Died: ` line neither replaces nor hides the summary's line; a file
sampled healthy is `hosts-pending` and alive while its final is left last,
then dead, measured, of a refusal on a route the stream spared, and clean
after a valid reply; a charged first selection evicted by 33 later ones
keeps its file dead, measured only when the log stage read its record
first; a stale summary holding the charged work's line and work keeps both
raw and the file dead and unmeasured; a file refused before any renderer
of it was selected is not run, alive and uncharged, its rows and the
totals naming the build's click source; and the totals' dead list
(`DeadFiles()`) of those 13 files names their 10 dead rows, each with that
row's reason, and no live one.

**Evidence.** Executed on macOS arm64 Debug against the Test account's
Twsteg megagroup in the evidence campaign of
`2026/10/24/audit-the-widget-attack-corpus-through-a-validated-ops-tap` (its
`work/test.md`), at widget-sandbox `v0.1.2`: the self-check and the three
verdict controls passed, and one launch of 289 s ran all 99 attack files.
Every file ran in the chat's own `HistoryInner`; 94 were live and each got
one click observed as a trusted click; 5 died by the runtime limits (the same
5 files the reference `--attacks` run reports), 3 of them before loading.
There were no leaks, notes, scan gaps, renderer violations or layers, and an
external loopback listener received the 8 controls of the launch and nothing
else. Windows and all other platforms have not run it. That campaign ran the
pack before its fixture moved into [test_widget_fixture](test_widget_fixture.md);
the move's own verification is listed there.

The hosted readings (`renderedHosts` as a load, the click outcome of the
hosted arrangement, `click.source`, the press readings and their totals)
and their verdict controls were added by
`2026/11/14/run-widget-validators-and-renderers-in-the-macos-helper`,
compiled in the macOS Debug build and syntax-checked with the Release
command line with and without `TDESKTOP_RELEASE_TEST_HARNESS`; they have
not run in the app yet. That task's evidence campaign plans the corpus in
the hosted arrangement against `in-process`, with a withheld up, a route
retired between the down and the up, a same-tap late up, a target its down
handler replaces, a played mouse-down the chat never dispatches followed
by a genuine click of the same work, and a stable target as controls, and
records them. Outside
the app the helper's glue, linked into a scratch program, reported its
renderer's click decision for a stable press and release (press 1, after
the up's reply) and none for a target replaced before the up, a secondary
press, a finger tap or a cancelled press; the glue before the report sent
none at all.

The same task's sixth review round bound the local click to the played
press too (the work registered at the selection, the down and the release
reported by the work, the `renderer` reader requiring the trusted click
matched to that press's own release) and added the withheld case and the
local and `helper-mirror` readers to the verdict controls; compiled in the
macOS Debug build and syntax-checked with the Release command line with and
without `TDESKTOP_RELEASE_TEST_HARNESS`, not run in the app yet. The 94
clicks of the first campaign above were read under the earlier rule, any
trusted click of the generation, which certifies nothing now. Outside the
app the tap's press methods and `ClickOutcome()`, taken unchanged from the
sources into a scratch harness, gave the expected outcome for every case
and reader, and the reader before the change certified the missed press
followed by a genuine click.

Its seventh round counted the local click only as its forwarding handed it
to the host and revoked the mark at the core's detach (`markDetached`), and
added the retired up and the click kept across a retirement to the verdict
controls of every reader; compiled in the macOS Debug build and
syntax-checked with the Release command line with and without
`TDESKTOP_RELEASE_TEST_HARNESS`, not run in the app yet. Outside the app
the tap's methods and seams, the verdict sequence and `ClickOutcome()`,
taken unchanged from the sources into a scratch harness with a model of
the host link, gave the expected outcome for every case and reader, and
the tap before the change certified the retired up beside an earlier
press's trusted click.

Its eighth round delivers the hosted retired case's queued up and click
after the retirement (the earlier press's mirrored click kept, no mirror
after it) and revokes the hosted mark at `WidgetHostRetired()`, so both
hosted readers give `click-not-observed` for it beside their completed
click before a retirement; compiled in the macOS Debug build, not run in
the app yet. Outside the app the tap's methods and the verdict sequence,
taken unchanged from the sources into a scratch harness, gave that outcome
in Debug and Release, and the tap before the change gave `observed` and
`decided`.

The death verdicts from final charges (`ParseLog()`'s paired sample,
`Judge()`'s `ReadHostedDeath()`, `deathMeasured`, `readingFinal`,
`DeadFiles()`), the barrier through every hosted final and the exchange
leg played apart came with
`2026/11/20/count-a-widget-helpers-death-only-against-widgets-with-work-in-flight`.
Its evidence campaign (its `work/test.md`, Attempt 1, which holds the
measurements) ran the pack in the macOS arm64 direct-download Debug client
and its embedded Debug helper, built with its disposable evidence overlay.
The first stage's rows passed, the 57 ops self-check rows and the 18
verdict rows among them. `layout-query-loop.html` and
`mem-uncaught-growth.html` died of their own limits on a live helper, each
measured from its own sampled work with its raw line kept, beside a live
`api-eval.html`, and `ATTACK_TOTALS.dead` named both with the same reasons.
On a run-local corpus of four text-free animated boxes, the overlay
holding the card's answer and stalling an unselected sibling around the
pack's own log and removal stages, every barrier closed with every
selected work final: a file sampled healthy, then charged
`refused/spared` for Telegram's refusal of a credited frame, read dead
with that refusal's reason, measured; the same file given the real painted
answer read alive and clean; a sample of a spared work whose replacement
was then charged with the same raw `Died: the widget engine process ended`
read dead, `charged, reason unmeasured`; and that replacement, sampled
itself, read dead with its line's reason, measured. `ATTACK_TOTALS.dead`
named those three files with their rows' reasons. No Release build with
the harness and no other platform ran it. Of those 18 verdict rows, the
one pinning the in-process renderer's death verdict was removed with that
verdict and its three control files by
`2026/11/21/remove-the-unreachable-local-widget-crash-count-and-death-readings`.

## Related guidance

[Scenario composition](../scenarios.md);
[scenario teardown](../teardown.md);
[chat-list resolution](../chat-lists.md);
[chat-list show before input](test_chat_show.md);
[input helpers](test_widgets.md#input-helpers);
[launch fuse](test_launch_fuse.md);
[validated-ops tap](test_widget_ops.md);
[shared widget fixture](test_widget_fixture.md);
[widget conformance runner](test_widget_conformance.md).
