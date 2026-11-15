# test_widget_attacks

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API (a permanent stage pack).
Source contract: [test_widget_attacks.h](../../test_widget_attacks.h).

## Use when

A scenario runs widget-sandbox's `corpus/attacks` through the app's own widget
view and needs, per file, whether it ran, loaded, died, what its validated ops
held under the `--attacks` leak definition, and how one real click through the
bubble went, plus in-process loopback controls for an external network
listener. `Test::AppendWidgetAttackAudit` appends the whole linear audit: each
HTML file becomes a local outgoing `messageMediaWidget` message in the legacy
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
- **One file at a time.** A file's message is destroyed and its tap must
  finish (every selected factory released, every built wrapper destroyed)
  within 10 s before the next file starts. A file that does not finish stops
  the audit: the remaining files are not run, by name.
- **Time.** Per file about 4-8 s in Debug for a healthy widget, and about
  70 s for one that reaches every bound (8 s layout, two 10 s shows, 25 s
  load, the 1.5 s watch, 3 s log, 10 s barrier). The runner's watchdog (`TDESKTOP_TEST_WATCHDOG`, at most 600 s)
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
`RunWidgetOpsSelfCheck()` row as a Check, then the three verdict controls
of Verification), `report the corpus`, `capture the
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
| `ATTACK_LOG` | `file`, `source` (`requestLog`, `logText` when the retired runtime keeps no callback, `logText-at-bound`, `unavailable`), `pending`, `waitedMs`, `chars`, `truncated`, `text` (its first 32768 characters). |
| `ATTACK` | One per selected file, see below. |
| `ATTACK_TOTALS` | `path`, `files`, `ran`, `loaded`, `complete`, `clean`, `leaks`, `notes`, `gaps`, `batches`, `violations`, `liveAtClick`, `clicksAttempted`, `clicksObserved`, `layers`, named lists (`count`, `files`) `dead`, `notLoaded`, `notRun`, `incomplete` (`<file>: <reasons>`, with a fixture refusal's reason in parentheses), `leaking`, `violating`, then `unknownRequested`, `selfChecked`, `selfCheckControl`. |

`ATTACK` fields: `file`, `index`, `bytes`, `sha256`, `item`, `path` (`final`
from the remove stage, `totals` for a file never started, `onFinish` after an
abort), `startMs` / `endMs` (ms since the epoch, injection to final reading),
`ran`, `notRun`, `loaded`, `notLoaded`, `died`, `deathReason`, `complete`,
`incomplete`, `fixtureRefusal`, `clean`, `fixture` (local shape, widget
media, payload),
`loadShow` and `clickShow` (`finished`, `shown`, `kind`, `refusal`, `reason`,
`local`, `card`, `wholeCard`, `rect`, `viewport`, `hover`, `hovered`, `paints`,
`elapsedMs`, `readiness`), `loadOutcome` (`loaded`, `died`, `stopped`,
`failed`, `no-runtime`, `bound`), `loadWaitMs`, `stateAfterLoad`,
`liveAtClick`, `click` (`outcome`, `details`, `attempted`, `pressMs`,
`trustedClicks`, `trustedEvents`, `eventTypes` since the mark taken right
before the press), `watchMs`, `stateAfterWatch`, `log` (`source`, `chars`,
`sandboxHeader`, `diedLine`), `layers`, `barrierClosed`, `barrierMs`,
`itemGone`, `examplesTruncated`, and `tap`: the reading's counts with its
`leakExamples`, `noteExamples`, `gapExamples` and `violationDetails`.

The statuses are independent, so a death with a leak is in both lists:

- `ran`: at least one wrapper was built (`wrappersBuilt >= 1`); otherwise
  `notRun` names why (gate, abort, injection, a fixture check, no renderer
  built). A fixture check that refuses the file after a renderer was built
  leaves it ran, with the batches seen so far, its reason in
  `fixtureRefusal` and `fixture-refused` among `incomplete`: its load wait,
  click, watch and log stages were skipped;
- `loaded`: `ackQueries > 0 && ackResults > 0`; `notLoaded` is ran and not
  loaded;
- `died`: `fatalFrozen > 0`, or a `Died: ` line right after the log's first
  sandbox header that is not `terminated`;
- `click.outcome`: `observed` (`trustedClicks >= 1`), `not-observed`,
  `show-refused`, `revalidation-failed`, `died-before-press` (not live at the
  press and frozen, stopped or failed), or `not-live` (not live after the load
  wait, so no click was owed);
- `incomplete`: `fixture-refused` (see `ran`), `aborted`, `hosts-pending`,
  `watch-missing` (the 1500 ms watch did not run to its end), `scan-gaps`,
  or `click-<outcome>` for a card live after its load wait that was neither
  clicked and observed nor dead before the press;
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
fixture refusal, every watch run, every live card's click observed, every
barrier closed, no scan gap), `every file's factories and wrappers finished
before its final reading`, `no
attack file opened a layer`, `CheckNoBlockedLaunches(no widget handed
anything to the operating system)`, and the teardown and settle rows. Deaths,
not-loaded files and renderer violations are Noted, not failed, and never
counted clean.

A pass proves, for the files of that launch: each ran in the product's own
`HtmlWidget` view of a local message of the fixture session, no fixture
check refused it and its watch ran, every batch every wrapper sent was
scanned at `Renderer::send` with zero leaks and no gap, every live card took
one real press and release with a trusted `click` observed, every host
finished before its final reading, no layer opened, no OS hand-off, and the
controls were written. It does not prove that no request was made (that needs
the listener and the source audit), that a control was received, which
element the click hit, or anything about files of other launches.

## Lifetime and cleanup

`Runner::onFinish` is registered before any stage: on every exit path it
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

The pack runs `Test::RunWidgetOpsSelfCheck()` first and packs the scanner's
2-leak, 1-note control into every campaign. In the same stage three pure
controls of the row verdict, on literal final rows, must hold: a loaded,
clicked, watched and finished file with no leak, gap or violation is
complete and clean (the positive control); the same row refused by a
fixture check after its renderer was built, with its click and watch
skipped, is ran, keeps its reason in the refusal, lists `fixture-refused`
and `watch-missing`, and is neither complete nor clean; the clean row with
one renderer violation and no leak is complete and not clean.

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
