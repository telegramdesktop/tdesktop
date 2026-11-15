# test_widget_conformance

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API (a permanent stage pack), with internal modules.
Source contract: [test_widget_conformance.h](../../test_widget_conformance.h).
Internal to it, documented below:
[test_widget_conformance_state.h](../../test_widget_conformance_state.h),
[test_widget_conformance_actions.h](../../test_widget_conformance_actions.h),
[test_widget_conformance_input.h](../../test_widget_conformance_input.h)
([timed pointer sequences](#timed-pointer-sequences)),
[test_widget_conformance_bridge.h](../../test_widget_conformance_bridge.h)
([bridge calls and uncaught errors](#bridge-calls-and-uncaught-errors)),
[test_widget_conformance_context.h](../../test_widget_conformance_context.h)
([message contexts](#message-contexts) and
[played bot answers](#played-bot-answers)),
[test_widget_conformance_hosts.h](../../test_widget_conformance_hosts.h)
([the content safe area](#readings-and-refusals) and the host reading
control of [verification](#verification)),
[test_widget_conformance_paint_policy.h](../../test_widget_conformance_paint_policy.h)
(the paint-policy fixture's read for [verification](#verification)),
[test_widget_conformance_pictures.h](../../test_widget_conformance_pictures.h)
([pictures](#pictures)) and
[test_widget_conformance_stop.h](../../test_widget_conformance_stop.h)
([desktop stops](#preconditions-and-limits)).

## Use when

A scenario measures the desktop client with widget-sandbox's
`packages/conformance` set and needs the run file that the package's
`tools/report.mjs` turns into the report. `Test::AppendWidgetConformance`
appends the whole linear runner. Each selected manifest entry becomes, per
theme, a local outgoing `messageMediaWidget` message with width anchor 390 in
the legacy `HistoryInner` of a Test chat, shown in the product's own
`HtmlWidget` view through the [shared widget fixture](test_widget_fixture.md);
its result is the last `data-conformance` value of its host's validated ops,
read through [test_widget_ops](test_widget_ops.md); an auto entry's scripted
actions are real Qt input sent to that `HistoryInner`, so the bubble's own
press handling and recognizers take them; its stages are cropped at 2x from
the list's own paint; a visual entry the caller asks a picture of gets its
whole card captured at 2x in bands ([pictures](#pictures)); and the runs are
written as one run file of the
`desktop` / `twidget-core` renderer. It follows the package's `CLIENTS.md`
("The runner, step by step", "Reading the ops"). twidget's
`tools/conformance` runner is a comparative reference only: its off-screen
Direct2D renderer and bare bubble are not the app's view, input or paint.

Caller: the evidence campaign of
`2026/10/24/run-the-widget-conformance-sets-through-the-chat-list-and-report`,
and reruns of it at a widget-sandbox or twidget pin move, such as
`2026/10/26/report-the-desktop-conformance-at-widget-sandbox-v0-2-2` at
`v0.2.3` and
`2026/10/29/report-the-desktop-conformance-and-run-the-stress-kit-at-widget-sandbox-v0-3-0`
at `v0.5.0` and
`2026/11/02/report-the-desktop-conformance-at-widget-sandbox-v0-6-0` at
`v0.6.0`, on a Release build without LTO.

Use something else when:

- the question is whether hostile widgets leak or reach the network: that is
  [test_widget_attacks](test_widget_attacks.md); the two packs can share a
  launch, each with its own fixture and its own control token;
- one widget's result or ops are enough: arm a tap and read
  `ReadWidgetOpsLatest()` ([test_widget_ops](test_widget_ops.md));
- the report is wanted: `report.mjs`, the schema validation and any assembly
  of launches run on the files afterwards; the pack writes no report.

## Preconditions and limits

- **Session and chat.** Append it after the overlay's `waitEvent`,
  `waitForSessionReady` and `waitForChatsLoaded`, with `Test::UseTestSession`
  and `Test::AppendTestSessionRestore` around it when the copy may open on
  another account ([test_session_gate](test_session_gate.md)). `chat` is
  loaded in the Test session the primary window shows, with its folder known
  and no unread messages; the fixture gates it, shows the window normally,
  keeps it exposed and opens the chat in its legacy `HistoryInner`
  ([test_widget_fixture](test_widget_fixture.md)). A failed gate skips every
  run by name. The pack hardcodes no chat, path or port. The chat's kind
  decides which runs a launch hosts ([message contexts](#message-contexts)):
  a private context a user's chat, a group one a basic group or a megagroup,
  a copy any chat; the others are omitted from that launch, for a launch on
  a fitting chat.
- **Window.** Right after the fixture's opening stages, before the first
  run's focus and injection, the fixture's `AppendWidgetFixtureOneColumn()`
  sizes the primary window for the app's one-column layout on the window's
  own screen, whatever layout the launch inherited
  ([one-column window](test_widget_fixture.md#one-column-window)): the
  chat column is then as wide as `MainWidget`. Without it a two-column
  window just wide enough for two columns leaves the chat column narrower
  than the card: in Run 5 of
  `2026/10/31/stop-a-widget-on-its-last-picture-when-the-paint-bounds-refuse-its-frames`
  a 715 logical px window on `pc` measured each core at 311 CSS px and
  refused every run `width-mismatch`. A wider two-column window may hold
  the anchor; the gate requires one column all the same. Its two gates are
  fixture gates: a missing window or screen, a refused target, or any
  layout but only the chat in one column skips every run by that gate's
  name. The screen is never changed, so the device pixel ratio the 2x
  captures need stays the launch's. A maximized or full-screen window
  leaves that state for the sizing, without `Qt::WindowActive`, and stays
  normal: the fixture's release never maximizes it again, which on Windows
  would activate it; a normal window gets only its new geometry. The
  sizing requests no activation and sets no stay-on-top hint, clears no
  focus and moves no cursor, so the focus oracle below still reads the
  window's own focus child.
- **The compose field.** The chat's field must be ordinary and empty: no
  draft text, no message being edited, no edit draft, no rich cloud draft, no
  voice recording, no forward draft. Each run reads it before its injection
  and each key action again before its key, and refuses
  (`compose-not-ordinary`) rather than clearing someone's draft.
- **The focus oracle.** Before each injection the runner calls
  `HistoryWidget::setInnerFocus()` of the exact legacy `HistoryWidget` that
  owns the fixture's list, and the window's `focusWidget()` must then be that
  field's editor (`focus-not-compose` otherwise). `QWidget::setFocus()` sets
  an inactive window's focus child, so nothing activates the window, which
  would mark the chat read. The oracle is that window focus child alone:
  `QApplication::focusWidget()` and the window's activation are recorded
  (`applicationFocus`, `windowActive`), never required, and no OS focus is
  claimed. `chatFocused` (`CLIENTS.md`'s D4) is whether the window's focus
  child is still that editor when the first result wait ends, before any
  input; `focusHolder` names the holder when it is not. A run whose field was
  never focused leaves `chatFocused` out.
- **Package.** `package` is the absolute `packages/conformance` directory of
  the pinned widget-sandbox. While the stages are appended the pack reads
  `manifest.json`, `probe-fixture.json` and each selected entry's HTML, and
  nothing else; `refs/`, `catalogue.json`, `run.schema.json` and `tools/`
  are the report's. A missing or invalid manifest or probe fixture refuses
  every run by name before anything is injected. The first stage reads one
  more file of the same pin, outside the package: its sibling
  `../core/tests/fixtures/paint-policy.json`, for the paint-policy rows of
  [verification](#verification); a missing or unreadable one fails one
  Check of that stage and refuses no run. A selected entry that is
  malformed (no id, set, file or light/dark themes, `widths` without 390,
  actions that are not objects, a host check that is not an object with an
  id, a stage without a name, themes or a rect in whole CSS px, a `message`
  that is not a non-empty array whose first item is an object, a first spec
  whose kind the runner does not implement or whose `chat_data` or
  `public_data` is not a string, a `bot` table that is not an object of
  objects with string `result`s, a `stops` that is not a non-empty string,
  a `safeArea` that is not an object) or whose HTML is unreadable or empty
  is not run, by name.
  `framework` and `engine` go into the run file as given: the campaign names
  the tag and the twidget pin, and an empty one fails a Check.
- **The matrix.** The task's matrix is every auto entry in both themes,
  whatever its manifest `themes`, at 390 CSS px, and every visual entry only
  in its manifest themes. At `v0.6.0` that is 115 entries, 68 auto and 47
  visual (21 visual and 26 client SDK): 136 requested auto runs. Three runs
  are omitted in every launch (`elsewhere` false). One is
  `auto/20-client-behaviour` in dark, while its manifest entry still gives
  the reason - its id, `themes: ["light"]` and a first action switching the
  chat to dark. Its scenario starts in light and switches dark and back
  itself, so a dark start's first action changes nothing and its theme-switch
  check could only fail by the runner's doing; the maintainers' web runner
  and twidget's reference keep it to light. A pin at which any of those
  premises changes runs the dark start. The other two are
  `auto/26-context-preview` in both themes, whose message is a preview the
  desktop does not build ([message contexts](#message-contexts)). The run
  file's `omittedRuns` and `notes` state each omission, its reason and that
  a file with every other auto run has 133 auto runs, not the 136 of the
  requested 68 x 2: a departure from that criterion, never to be reported as
  meeting it without this reconciliation; `matrix.permanentAutoOmissions`
  and `matrix.fullAutoRuns` count the same from the manifest. The 133 are
  67 light and 66 dark runs, and 64 of the 66 dark ones are of light-only
  entries; they go beyond `CLIENTS.md`'s loop over each entry's own themes
  (70 auto runs at `v0.6.0`), `report.mjs` counts their checks and skips
  their stages by each stage's own themes, and `matrix.extraDark` and a
  note say so. The visual set gives 47 light
  and 4 dark runs, 51 visual runs. A run the launch's chat cannot host is
  omitted from that launch only, `elsewhere` true, and subtracts nothing
  from the matrix; a note states the launch's share for its selection
  alone, and only every launch's runs together give the full matrix.
- **Visual entries are load checks.** No actions, no crops, no wait after
  actions: a visual run waits for `done` or `awaiting` (the SDK's mini harness
  and some visual pages wait for a person's input) and records its result,
  load, death, errors, bridge calls, height and focus like any run.
  `report.mjs` takes no verdict evidence from them; its summary shows each
  visual id's light run first, so read dark records in the run file. A
  requested [picture](#pictures) adds captures of the card, never a verdict.
- **Production configuration.** The pack configures no host: every widget
  host gets `DefaultHostOptions()` of `tgw_runtime.cpp`, policy `strict` and
  features `webAnimations`, which the run file states as `policy` and
  `features` (a source fact, not a per-run reading). Every run's message
  gets a [host watch](test_widget_host.md) with an empty
  `WidgetHostOverride`, which replaces no option, so its `applied` options
  are the product's; the runner reads only their `contentSafeArea`. The
  watch is not free: with its `onChange` set, `WidgetHost::changed()` copies
  the host's whole diagnostics on the host thread for every change. That is
  host-thread work beside the widget's, not JS or main-thread work, on every
  run, the timing-sensitive `auto/19-limits` and the stop entries included;
  a note in the run file states it. The measured core width
  must be 390 within 0.01 CSS px, or the run carries `width-mismatch` (the
  anchor was not honoured, or the chat column is too narrow); the light runs
  go in the app's day palette and the dark ones in its night palette, and a
  renderer scheme other than the run's theme is `scheme-mismatch`. Both are
  measured after the result wait, never waited for. The bubble's palette,
  button and text tokens are the product's own; each run records the raw
  `theme` message its renderer got. The default themes must tell day from
  night, or the palette switch refuses `sample-unchanged` and that theme's
  runs do not run.
- **Waits.** From the load show: an auto entry without actions waits up to
  20 s for `done`, one with actions for `done` or `awaiting`, and a visual
  entry for either. A death, a failure, the message gone or the bound ends the
  wait with that outcome, recorded and not refused: the report judges what the
  widget published, and a dead or unloaded run fails every check. After the
  actions the runner waits up to 10 s for `done`. An entry with `stops`
  (twelve at `v0.6.0`: `auto/31`-`auto/35` and `auto/38`-`auto/44`) waits
  for its widget's death instead, as `CLIENTS.md`'s step 4 asks: no status
  ends its wait, so a widget its host never stops publishes `done` and the
  wait runs to the 20 s bound, and only a fatal `frozen`, a stopped or
  failed runtime or the message gone end it sooner.
  Its `dead` keeps its usual sources: the `Died: ` reason of the host's
  fresh or final summary (`WidgetDiagnostics::dead` verbatim, cut at 240
  characters), and only when the summary gives no reason but `terminated`,
  the desktop's own stop ([desktop stops](#preconditions-and-limits));
  never the localized stopped label. Whether it starts with the entry's
  `stops` is `report.mjs`'s judgement alone.
- **Desktop stops.** A stop the desktop decided itself, such as a paint
  bound's refusal of the widget's frames, reaches no host death: the
  summary has no `Died: ` line or, once the retired host is gone,
  `Died: terminated` ([test_widget_host](test_widget_host.md)). In every
  run whose summary gives no other `Died: ` reason, `RecordLog()` reads it
  once, in the log stage, with `ReadDesktopStop()` of
  `test_widget_conformance_stop.{h,cpp}`: the fixture's
  `WidgetDesktopStop()` for the core of the run's host watch, and only
  when that watch has exactly one attachment
  ([test_widget_fixture](test_widget_fixture.md)). It reads it
  whatever the result waits saw, so a stop after the last wait counts too.
  A reason sets the run's death, and `dead` is `RendererCore`'s stop reason
  cut as `Clip()` cuts the summary's (240 characters, its "..." included).
  The runner keeps no field for it beyond `dead`; a host's own reason
  always comes first.
- **2x.** The stage crops and the picture bands need the device pixel ratio
  times the interface scale to be 200 % (a Retina display at 100 %, say), and
  the accepted image of each snapshot and band must be exactly twice its CSS
  size. Anything else refuses every stage of the run, or the band, `not-2x`;
  no frame is upscaled.
- **One host per run.** A run whose tap selected more than one host factory
  (a restart after a crash, a re-creation) still reports its result: the
  newest value, or the older host's last one when the newer cleared it
  (`result.retained`). Its bridge and error reading is refused
  (`summary-multiple-hosts`), and a host replaced between its first
  `visible` action and a later one is `host-replaced`.
- **Time.** Every wait ends in its stage with a named reading: 6 s for
  each of the window's two settles before the first run (at least 1 s
  each), 8 s layout, 10 s per show, 8 s per floating-date wait before a
  capture (accepted after 3.35 s of an unchanged scroll, so each snapshot
  and band costs at least that: about 80 s for a launch of 16 snapshots
  and 8 bands), 20 s result,
  350 ms gaps, the planned time of each timed
  sequence, 4 s per palette settle, 4 s theme and visibility waits, 10 s done,
  3 s log, 10 s host barrier, each with `kDefaultStageTimeout` more as the
  stage timeout. The runner's watchdog (`TDESKTOP_TEST_WATCHDOG`, at most
  600 s) is a real process-lifetime limit ([scenario design](../scenarios.md)).
  When one launch cannot hold the selection, split it with `themes`, `sets`
  or `ids`, each launch with its own fresh output directory. A run file holds
  only its launch's runs and its notes say the selection was narrowed;
  `report.mjs` writes one `<platform>-<renderer>.json` per renderer, so
  partitions are combined into one renderer file before the report. The pack
  has no merge, resume or load-existing code.
- **Message contexts.** Each run's message is injected with the flags of its
  entry's first `message` spec's kind and that spec's state, an entry
  without `message` with the private chat's context and no state; the
  client derives the chat type and the viewer itself
  ([message contexts](#message-contexts)). Its text is the entry's id, the
  caption below the widget (`WidgetFixtureMessage::caption`), but for an
  entry with `safeArea` (`auto/36-safe-area` and `visual/17-time-corner` at
  `v0.6.0`), whose message has no text: the caption puts the message's time
  in its own line below the widget, so the desktop itself gives the host
  `CLIENTS.md`'s all-0 content safe area, while an uncaptioned message's
  host gets the band and corner the desktop passes for a widget with no
  caption below it, with the time drawn over that corner. The `safeArea`
  extra records what each host got
  ([Readings and refusals](#readings-and-refusals)).
- **Played bot.** Every injected run's message is armed with its manifest
  entry's `bot` table (empty without one) through
  [test_widget_bot](test_widget_bot.md), so its widget's requests are
  answered by the runner at `Tgw::Runtime::botRequested()`, never by
  `Tgw::Bridge`, the server or a real bot
  ([played bot answers](#played-bot-answers)).
- **Not measured.** Widths 360 and 430 (the width is fixed at 390), other
  platforms, whole-process network traffic, pixels of the visual set (a
  requested picture is captured, never judged), and
  whether `framework` names the checked-out tag. The real cursor is not
  moved: the lists resolve `QCursor::pos()` on enter, resize and scroll
  ([chat-list show](test_chat_show.md)), which the action readings make
  visible but cannot prevent.

## Minimal use

In the disposable overlay's `SetupScenario`:

```cpp
void SetupScenario(not_null<Runner*> runner) {
	const auto session = Test::UseTestSession(runner);
	runner->waitEvent(u"launch_finished"_q);
	runner->waitForSessionReady();
	runner->waitForChatsLoaded();
	Test::AppendWidgetConformance(runner, {
		.package = u"<absolute packages/conformance directory>"_q,
		.chat = <the Test chat's PeerId>,
		.output = u"<absolute directory, absent or empty>"_q,
		.framework = u"<the widget-sandbox tag>"_q,
		.engine = u"<the twidget pin, by its subject>"_q,
		.themes = qEnvironmentVariable("<THEMES>").split(
			QChar(','),
			Qt::SkipEmptyParts),
		.pictures = { u"<visual entry id>"_q },
		.token = qEnvironmentVariable("<TOKEN>"),
		.ports = { <port>, <port> },
	});
	Test::AppendTestSessionRestore(runner, session);
}
```

The pack Checks its own outcome; read its `CONFORMANCE_TOTALS` row and the
run file, then run `node <package>/tools/report.mjs --out <dir>
<output>/run.json` on the final renderer file.

## Stages

Every stage name, Check and Note of the pack starts `widget conformance: `,
after `palette switch: ` or `palette restore: ` in the palette helpers'
stages; the show helper's stages are `show in chat list: widget conformance
<key> <what>`; the first stage takes the rows of the tap's, the played
bot's, the probe fixture's and the paint-policy fixture's checks under their
own names, such as `widget ops self-check: ` and `paint policy fixture: `
([verification](#verification)). A run's key is `<id>|<theme>|390`. In
order:

- `tap, result reader, summary reader and run file self-check`, `report the
  package, the selection and the output`;
- the fixture's `capture the baseline and show the window normally`, `keep
  the window exposed`, `open the chat in the legacy HistoryWidget`, `move
  the window for the one-column layout` and `size the window for the
  one-column layout` ([window](#preconditions-and-limits)), then `watch the
  session for outgoing messages` and `network control before the first
  widget`;
- per theme, light first: `palette restore: widget conformance: before the
  dark runs` (from the second phase on), `palette switch: widget conformance:
  the <theme> runs` and `judge the palette of the <theme> runs`, then every
  run of that theme in manifest order:
  - `<key>: inject the fillers above the widget`, only for a run with a
    `visible` action;
  - `<key>: focus the chat's input field`, `<key>: arm the tap and inject the
    widget message` (with its entry's message context and caption and an
    empty-override host watch; it also arms the played bot, in the same
    call, and refuses a group run in a broadcast channel before anything is
    added),
    `show in chat list: widget conformance <key> load` (the
    card's top 48 CSS px, so the list counts it visible and its host starts;
    no hover), `<key>: wait for the result, a death or a failure`;
  - with actions: `<key>: start the actions`, each action's stages
    ([Actions](#actions)), `<key>: close the last action` and `<key>: wait
    for done after the actions`;
  - with stages in its theme: `show in chat list: widget conformance <key>
    snapshot`, `<key>: wait out the floating date before the snapshot` and
    `<key>: snapshot the stages at 2x and crop them`;
  - with a requested picture, per band 0 and 1: `show in chat list: widget
    conformance <key> picture band <band>`, `<key>: wait out the floating
    date before picture band <band>` and `<key>: capture picture band
    <band> at 2x` ([pictures](#pictures));
  - `<key>: read the runtime log and the card height` and `<key>: remove the
    message and wait for its hosts`;
- `network control after the last widget`, `palette restore: widget
  conformance: after the last run`, `judge the palette after the last run`,
  `stop watching for outgoing messages`, `totals and the run file`, and the
  fixture's `teardown` and `settle after the teardown`.

A press is sent in the run of the stage right after its show, which
revalidates the accepted list, view, core, card geometry and targets first.

## Actions

Performed only for an auto run whose first wait ended `awaiting` and whose
widget had not died; in any other run every action not skipped as
unperformable keeps the refusal `not-awaiting`. Each action but `wait` starts
with `action #<i> <kind>: keep the gap`: at least 350 ms since the previous
action's last input (`CLIENTS.md`'s "about 350 ms"; none before the first; a
manifest `wait` is no input and keeps only its own interval). At the gap's
end the runner hides any layer shown over the chat (an `openLink`
confirmation) at once, confirming nothing, closes the previous action's
event window, requires the widget to be live (`not-live`), marks the tap's
trusted events and resolves a pointer action's targets from the newest value.

A target is the centre of `targets[name]`, or a CSS point `[x, y]`; a drag's
`to` with `by` adds its delta to the start, otherwise it names a target. CSS
px map to the card at the interface scale, and the action's show brings the
card's full width over its targets' band (8 CSS px more each way) into view,
ready and hovered at the start point. At the press the targets are resolved
again: one that moved more than 0.5 CSS px is `target-moved`.

| Action | Route through the app | Reading |
| --- | --- | --- |
| `tap` | `Test::Click` on the list at the target: press, release, leave | `point`, `listAlive` |
| `press`, `drag`, `dblclick`, `rightclick` | a [timed pointer sequence](#timed-pointer-sequences) on the list; `press` without `ms` holds 400 ms, `drag` without `steps` takes 10 | `sequence`, `menuBuilt` |
| `hover` | the action show's own enter and move at the target; no leave, the pointer stays until the next action | `point`, `show` |
| `wheel` | `Test::Wheel` at the target with angle delta `(0, -round(dy * 1.2))` (Chromium's 100 CSS px per 120 units), then a leave | `wheel`: the `WheelDelivery` |
| `key` | `Enter` as `Test::PressKey(Qt::Key_Return)`, no text; `Space` and one printable character as `Test::TypeText`; sent to the window's focus child | `receiver`, `focusMoved`, `typed`, `cleared`, `composeBefore`, `composeAfter` |
| `theme` | restore of the palette chain's last switch, then `AppendPaletteSwitch` to the day (`light`) or night (`dark`) palette, then up to 4 s for the newest host's scheme to reach the target | `palette`, `widgetTheme` |
| `visible: false` | one `Ui::ElasticScroll::scrollToY` of the list's own scroll putting the view's top 8 px below the viewport, then up to 4 s for the runtime to count it hidden | `before`, `scrollTo`, `after`, `runtimeVisible`, hosts |
| `visible: true` | `AppendShowInChatList` of the card's top band, then up to 4 s for the runtime to count it visible | `show`, `after`, `runtimeVisible`, hosts |
| `wait` | a stage of `wait` ms | `waitedMs`, `afterLastInputMs` |

Every pointer action but `hover` ends with a leave, a refused one included. A
pointer action's reading also has its `target`, its show and the card it
accepted. Every action's has its manifest entry and its times (`gapMs`,
`beganMs`, `firstEventMs`, `lastEventMs`, `sinceLastInputMs`), and once its
gap ended on a live widget `trusted`: the tap's trusted renderer events from
the mark at its gap's end to the next action's, so its show's hover and the
widget's late reaction are in it (types, moves, the moves between the first
`pointerdown` and its `pointerup` or `pointercancel`).

- **Keys** go where the OS sends a key, the window's focus child, only into
  an ordinary empty field: a focus child that is not the editor is recorded
  (`focusMoved`), never redirected. Whatever the key typed is cleared in the
  same stage, before the event loop runs the field's queued typing update,
  so no typing action goes out; an Enter meets an empty field.
- **Themes** keep one palette obligation outstanding at a time: each phase and
  each theme action restores the chain's last switch, then switches, so a
  later phase and each theme action make exactly one real palette change,
  whichever palette the launch began in (the first phase's switch changes
  nothing when the launch already shows that palette). These palette stages
  run whenever the launch runs, even for a run or an action that is refused,
  so the chain never loses its obligation. Checks: each restore (when its
  switch succeeded), each switch, exactly one transition to the target per
  theme action, and after the final restore the launch's night mode, follow
  flag and `windowBg` sample.
- **Visibility.** A run with a `visible` action (`auto/20` in light at
  `v0.1.2`) gets four 40-line local outgoing text messages right above its
  widget, registered at setup and destroyed with it, so the list can scroll
  the card wholly off screen within one viewport, inside the distance at which
  `HistoryInner` keeps heavy parts loaded. The runtime's own answer is its
  private `_visible`, read through a test-only accessor. The same runtime,
  tap generation and factory count must hold across both actions; a different
  host is `host-replaced`, since a restarted host would not measure the pause.

Only an action this build cannot perform at all is skipped, into
`skippedActions` with its reason in `skippedReasons`: a key other than
`Enter`, `Space` or one printable character, a theme other than light or
dark, a `visible` that is not boolean, a `wait` that is not a non-negative
number, or none of `CLIENTS.md`'s action keys. At `v0.1.2` none is.

Every other unperformed action keeps a named refusal in `actions[].refusal`
and fails the totals: `not-performed` (never reached), `not-awaiting`,
`not-live`, `target-unresolved`, `show-refused`, `revalidation-failed`,
`target-moved`, `input-not-delivered`, `receiver-gone`,
`compose-not-ordinary`, `no-focus-child`, `palette-refused`,
`palette-transitions`, `no-list`, `no-room`, `still-in-view`. Input that was
delivered and that the product then rejected is performed and recorded,
never skipped or refused: a right click for which no menu was built, a
widget whose scheme did not follow the palette (`widgetTheme.reached`
false), a runtime still counting itself visible off screen. The widget's own
checks then carry it into the report as a discrepancy. The action stages add
three run refusals: `too-few-moves`, when `auto/23-gestures-b`'s `mid` drag
reached the widget as fewer than the 3 trusted `pointermove` events with the
button down its check needs (the runner's timing, never the widget's
verdict), `host-replaced` and `fillers-refused`.

## Stage snapshot and crops

After the actions and the done wait, an auto run with stages in its theme
gets one snapshot of the union of those stages' rects, in CSS px from the
widget's top-left, mapped onto the current card. Its show needs
`WidgetCardReady()` (live, its interactive core current, attached and
accepting input, a frame cut for the card size) and hovers nothing.

The next stage waits out the floating date. When a scroll moves the list
up, `HistoryInner::scrollDateCheck()` shows the date of the top message
over the viewport's top and hides it `kScrollDateHideTimeout` (1000 ms)
later; when a scroll down moves the top message into another day (the
chat's older messages above the fixture's card, say),
`scrollDateCheckDownward()` shows it and keeps it for
`kScrollDateHideOnDayCrossingTimeout` (3000 ms). Either hides with a fade
of `st::historyDateFadeDuration` (200 ms), and a capture before that paints
the badge over the card's top rows. So the wait, `AppendDateWait()` in the
pictures module, samples the show's list's owning `Ui::ElasticScroll` on
every runner tick from its run and accepts once it read the show's
accepted `scrollTop` unchanged for 3350 ms (the longer timeout, the fade
and 150 ms for the coarse timer and the fade's last frame), whichever way
the list scrolled; any change restarts the count, and an earlier scroll is
older than the run, so the count from the run is conservative. It is a
condition, never a fixed sleep, and it reads only: no input, hover or
scroll. A list or scroll gone, or 8 s without that, ends it unaccepted.
Each snapshot and band therefore costs at least 3.35 s: launch 1 of
`2026/10/26/report-the-desktop-conformance-at-widget-sandbox-v0-2-2`
(16 snapshots and 8 bands) about 80 s more, still under the 600 s
watchdog. The reading is the snapshot's
`dateWait`: `{finished: false}` until it ended, then `finished`, `hidden`,
`shownTop`, `top` (the last reading), `changes`, `stillMs`, `neededMs`,
`waitedMs` and `detail`.

The stage after it revalidates the view, core and geometry, refuses an
unaccepted date wait, then captures the shown rect of the list with
`PreparedWidgetCapture` ([test_capture](test_capture.md)): the list's own
paint, accepted only when visible, non-empty, not blank and a valid paint
root. The image must be exactly `2w x 2h` of the union's CSS
size. It is saved as `cards/<id>.<theme>.png`; in these file names every
character of the id and the stage name but ASCII letters, digits, `-` and
`_` becomes `_`.

Each stage is then cut at its manifest rect, never at the rect the widget
published for it (logged beside as `widgetRect`): the pixel rect
`((x, y) - snapshotOrigin) * 2` of size `2w x 2h`, where `snapshotOrigin` is
the union's CSS top-left. It is converted to `QImage::Format_RGB32`, saved
as `crops/<id>.<theme>.<stage>.png` and its size read back from the file.
The run's stage entry has `crop`, `rect` and `capture` (manifest rect,
widget rect, origin, pixel rect, logical rect, size). A stage without a crop
carries only its refusal as `detail`, which `report.mjs` judges as having no
crop: `not-captured`, `not-2x`, `not-live` (died or did not load),
`show-refused`, `capture-refused`, `date-not-hidden` (the date wait was not
accepted; its `detail` names why), `out-of-bounds` (the rect outside the
painted card or the snapshot), `write-failed`. Any of them fails the totals;
a skipped crop is never a completed measurement.

## Pictures

`test_widget_conformance_pictures.{h,cpp}`. `pictures` names visual
entries whose cards the caller wants as the list paints them, beside the
package's `refs/visual/<id>.png`, as the corrected pictures of
`2026/10/26/report-the-desktop-conformance-at-widget-sandbox-v0-2-2`. The
list's viewport (500 logical px in the campaign) is shorter than the tallest
of them (544 and 534 CSS px at 100 %), and the show helper refuses a rect
taller than the visible area, so one capture cannot hold a card.

- **Selection.** `SelectPictures()`, right after `SelectRuns()`: every run
  of a named visual entry gets a requested picture of two bands, in each
  theme it runs in. An id that is no visual entry of the manifest (an auto
  id, or none) goes into `selection.unknownPictures` and fails the
  selection Check; a visual entry this launch runs in no theme (another
  `ids`, `sets` or `themes`, or left to another chat) gets a note and
  nothing else; no `pictures` adds no stage, row, note, extra or Check.
  `selection.pictures` keeps the argument as given.
- **Bands.** Band 0 is the card's CSS rows from 0, band 1 those from 300,
  each `min(300, height - y)` tall at the card's whole width: two bands hold
  a card of up to 600 CSS px. A band the card ends above is not needed, its
  show skipped, and no missing capture.
- **Show and capture.** After the result wait and before the log and the
  removal, per band: a show of the band's rows on the current card
  (`WidgetCardLocal()`), with no hover, needing `WidgetCardReady()`; the
  snapshot's floating-date wait
  ([stage snapshot and crops](#stage-snapshot-and-crops)); then, in the
  stage after it, the snapshot's revalidation of the view, core, card and
  geometry the show accepted, the date wait's acceptance and one
  `PreparedWidgetCapture` of the shown rect of the list, its own paint. The
  image must be exactly `2w x 2h` of the band's CSS size, from the device
  pixel ratio and the interface scale, and is saved unconverted as
  `pictures/<id>.<theme>.<band>.png`, its size read back from the file.
- **One host, one size.** Band 0 is the first capture: it keeps the card's
  CSS size, its interactive core, the tap's host generation and the
  `stages` the widget published then. Band 1 is placed on that card size
  and joins only from the same core and generation, at the same card size,
  right below band 0's saved rows; anything else is refused rather than
  stitched from two hosts or geometries. The two bands are two moments of
  one live card: an animation may differ between them.
- **Refusals** (`refusal` and `detail` of a band without a file):
  `not-captured` (never reached, or band 1 without a saved band 0 to be
  placed on), `not-2x`, `not-live` (died or did not load), `show-refused`
  (the show's refusal, `cannot-fit` in an environment whose viewport cannot
  hold a band among them), `capture-refused` (the revalidation, a card that
  is no whole number of CSS px, another core, generation or card size than
  band 0's, a shown rect other than the band's, or the capture itself),
  `date-not-hidden` (the band's date wait was not accepted), `write-failed`.

Each run with a requested picture, completed or not, carries the extra
`picture`; the runner judges no pixel of it, and `report.mjs` reads none:

| Field | Meaning |
| --- | --- |
| `bandCss` | 300. |
| `card` | The card's `[w, h]` CSS px at band 0, `null` without it. |
| `core`, `generation` | Band 0's interactive core (an address, never dereferenced) and host generation. |
| `stages` | The `stages` the widget published at band 0, `null` when none. |
| `devicePixelRatio`, `interfaceScale` | At the last band. |
| `complete`, `missing` | Whether every needed band is saved at its rows, the card holds no rows below band 1, and what is missing otherwise. |
| `bands` | Per band: `band`, `needed` (`null` while band 0's card is unknown), `css`, `list` (the shown rect, the list's logical px), `card` (the accepted card in its view), `core`, `generation`, `image`, `file`, `refusal`, `detail`, `show`, `dateWait` (as the snapshot's); a band not needed is `{band, needed: false}` alone. |

The totals stage, when any picture was asked for, Checks that every selected
run with a requested picture is `complete`. A run file written from
`onFinish` keeps the bands taken so far without that Check. The task-local
comparison stitches the bands of `complete` pictures and sets them beside
the package's pictures.

## Readings and refusals

**The run file** is `<output>/run.json` (`EvidenceDir() + "conformance"`
when `output` is empty), written with `QSaveFile` by the totals stage, or by
`Runner::onFinish` when that stage did not run; its `writtenBy` says which.
It has the schema's `platform` `desktop`, `renderer` `twidget-core` (the
report's `desktop-twidget-core.json`), `engine` (the campaign's text naming
the twidget pin, the app version with its build, "Debug" under `_DEBUG` and
otherwise "Release without LTO", which `TDESKTOP_RELEASE_TEST_HARNESS`
requires, OS, CPU, Qt, "TwidgetCore painted by tgw's QtBackend (QPainter)",
device pixel ratio and interface scale), `framework`,
`policy`, `features`, `date` (UTC, at setup) and `runs`; and the extras
`locale`, `environment`, `notes`, `omittedRuns`, `incompleteRuns`, `matrix`,
`selection`, `selfCheck`, `palette`, `paletteRestore` and
`unexpectedOutgoing`. The schema forbids no extra field. Each
`omittedRuns` entry has `id`, `theme`, `width`, `reason` and `elsewhere`:
true only for a run left to a launch on another chat, which a combination
of launches may drop once another launch completed it; false for the
manifest's own omissions, which stay. `matrix` has the manifest's
`autoTests`, `visualTests`, `requestedAutoRuns`, `permanentAutoOmissions`,
`fullAutoRuns`, `clientsAutoRuns` and `visualManifestRuns`, and this
launch's `autoLight`, `autoDark`, `autoRuns`, `extraDark`, `visualLight`,
`visualDark`, `elsewhereAuto` and `elsewhereVisual`.

`runs` holds only completed runs: measured through their log, with a final
tap reading, and not refused by the package or the fixture (a malformed
entry, unreadable HTML, a refused injection, no widget card of the list's
own). Every other selected run is in `incompleteRuns` with the state it
reached and why, never written as a completed run. A completed run can
still carry runner refusals; they fail the totals. Its fields:

- `id`, `theme`, `width` (the measured core width rounded, or 390 with
  `widthMeasured` false), `loaded` (the host's post-load ack answered),
  `dead` (null, a fatal `frozen`, a stopped runtime, the summary's `Died:`
  reason other than `terminated`, a stop the desktop decided itself
  ([desktop stops](#preconditions-and-limits)), or "died, reason not
  observed"), `status`
  and `checks` verbatim from the reported value;
- `stages` as above; `observed`: `bridge` (`{method, args}`, `args` a JSON
  array), `height` (the card's height in CSS px before the removal),
  `chatFocused`, `focusHolder`, and `syntheticInput` false: the input goes
  through the list's own handlers as real Qt events, mouse and keys and
  never a touch, and TwidgetCore marks every such event it forwards trusted
  (only a real finger's `pointerdown` and `mousedown` would not be), so
  `report.mjs` judges the gesture-gated host checks; `errors` (the console's
  uncaught errors); `skippedActions`;
- `host`, only while the bridge calls are unknown (below);
- extras: `key`, `set`, `skippedReasons`, `actions`, `runnerRefusals`
  (`{name, detail}`), `result` (`present`, `error`, generation, serial,
  values, html id, `retained`), `tap` (the final counts and leak and gap
  examples), `focus`, `view` (list, load-show and final view, core, card,
  core width, scheme, scheme changes, generation, theme message), `load`
  (show, outcome, waits), `log` (source, summary provenance, the summary
  reading's counts), `contentHeight`, `layers`, `message`
  ([message contexts](#message-contexts)), `bot`
  ([played bot answers](#played-bot-answers)), `safeArea` once the run's
  host watch was read (below), with stages `snapshot`,
  and with a requested picture `picture` ([pictures](#pictures)). An
  incomplete run carries `message`, its `picture` when one was requested
  and, once injected, `bot` too.

An observation that was not made is left out, never filled in: `bridge` and
`errors` appear only when the [summary](#bridge-calls-and-uncaught-errors)
established them, `chatFocused` only when the field was focused, `height`
only when the card was measured.

**The content safe area.** `RecordLog()` reads the run's host watch once,
in the log stage before the removal, with `ReadSafeAreas()` of
`test_widget_conformance_hosts.{h,cpp}`, which also holds
`SafeAreaObject()` and `SafeAreaRefusal()`, and `safeArea` is
`{caption, applied}`: `caption` the message's text, the entry's id, or null
for an entry with `safeArea`; `applied` one
`{top, right, bottom, left, cornerLeft, cornerRight}` in CSS px per armed
construction of the run's host, in order: `WidgetHostApplied`'s
`contentSafeArea`, what the `Job` built the host with
([test_widget_host](test_widget_host.md)). The key is never the schema's
`host`, which `report.mjs` would read as judged host checks. A safe area
`Runtime::constructed()` reconciles before `start()`, or a later
`setContentSafeArea()`, is not in `applied`. A message captioned from its
creation is never the bubble's bottom, so `HtmlWidget::contentSafeArea()`
gives it 0 from its first layout on (a static reading of
`history_view_html_widget.cpp`). The manifest's `safeArea` numbers are its
reference picture's, never expectations: a `safeArea` entry's band is
whatever the desktop passes, so it is recorded and never refused, and a
zero band there is the desktop's finding. Its cross-checks are the
widget's own readings, `auto/36`'s `contentSafeAreaInset` check details and
`visual/17`'s footer in its picture.

**Unknown bridge calls in the report.** `report.mjs` counts a missing
`observed.bridge` as no calls (`observed.bridge ?? []` in its `hostChecks()`)
and reads no `runnerRefusals`, so leaving `bridge` out alone would turn an
unknown history into zero-call passes (`input.user-activation`,
`input.prevent-default`, `input.key-gesture`) and zero-call failures
(`tw.open-link`, `html.el.a-external`, `limit.haptic-rate`). A run whose
bridge calls are unknown (its reading refused, or never read) therefore also
carries the schema's `host`, which `report.mjs` takes in place of judging
`observed`:
`ConformanceHostFieldsOf()` in `test_widget_conformance_bridge.cpp` judges
every host check of the run's manifest entry as `hostChecks()` does, with its
order and texts, except that a check counting bridge calls is `skip`,
`bridge calls not observed (runner refusal: <names>)` or `(the run's runtime
summary was not read)`, the way `hostChecks()` skips an unobserved height.
Its catalogue IDs then get no verdict from that run, neither pass nor fail.
The height and focus checks keep their judgements (`behaviour.auto-height`,
`js.focus`), and the runner refusal still fails the totals. A run with a
bridge reading, of no call or of calls, carries no `host`: `report.mjs` judges
its `observed` itself. A run without `observed.bridge` always has `host`.

Run refusals (`runnerRefusals`), each failing the totals:
`compose-unavailable`, `compose-not-ordinary`, `focus-not-compose`,
`load-show-refused`, `width-mismatch`, `width-unmeasured`, `scheme-mismatch`,
`scheme-unmeasured`, `height-unmeasured`, `card-write-failed`,
`too-few-moves`, `host-replaced`, `fillers-refused`,
`safe-area-not-zero` (a run of an entry without `safeArea`, so with a
caption, whose host got a nonzero field in any construction: the runner did
not make `CLIENTS.md`'s all-0 state), `safe-area-unmeasured` (a loaded run
with no watch reading or no `applied` entry), `stop-reason-unmeasured` (a
run of an entry with `stops` that died without a `Died: ` reason other
than `terminated` in its host's fresh or final summary and without a stop
the desktop decided itself, so its `dead` is the runner's placeholder),
and the summary reader's. None of the three
drops a run from `runs`. A result value that is not a
JSON object, not published for this run's id, or whose `checks` is not an
array is not used; `result.error` names why.

**Rows**, each `NOTE: <marker>: <compact JSON>`:

| Row | Fields |
| --- | --- |
| `CONFORMANCE_PACKAGE` | `dir`, `error`, `framework`, `engine`, `tests`, `probeFixtureBytes`, `selection`, `invalid`, `refused`, `runs` (every key), `omittedRuns`, `matrix`, `output`, `outputError`, `network`. |
| `CONFORMANCE_ENVIRONMENT`, `CONFORMANCE_NET_CONTROL` | The fixture's rows. |
| `CONFORMANCE_FILLERS` | `run`, `injected`, `fillers`. |
| `CONFORMANCE_ACTION` | `run`, `index`, `kind`, `performed`, `refusal`, `skipReason`, `reading`. |
| `CONFORMANCE_ACTIONS` | `run`, `actions`, `performed`, `notPerformed`, `elapsedMs`. |
| `CONFORMANCE_SNAPSHOT` | The run's `snapshot` and every stage's crop, refusal and capture. |
| `CONFORMANCE_PICTURE` | After each band's capture stage: the band's entry of the `picture` extra, plus `run`, `pictureCard`, `pictureCore` and `pictureGeneration`. |
| `CONFORMANCE_LOG` | `run`, `source`, `summaryState`, `summary`, `pending`, `waitedMs`, `chars`, `truncated`, `text` (its first 32768 characters). |
| `CONFORMANCE_RUN` | The run's entry, plus `item`, `completed`, `progress`, `notRun`, `barrierMs`, `itemGone`, `fixture`. |
| `CONFORMANCE_OUTGOING` | An outgoing message the runner did not inject: `run`, `id`, `wallMs`, `inChat`, `local`, `regular`, `service`. |
| `CONFORMANCE_TOTALS` | `path`, counts and named lists of completed, incomplete, loaded, dead, focus, refusals, actions, crops, pictures (`requested`, `complete`, `missing`), leaks, gaps, violations, unfinished hosts, accepted `openTelegramLink` calls and layers; `absences` (`leaks`, `gaps`, `telegramLinks`: `selected`, `covered`, `found`, `certified`, `uncovered`); `abort`, `palette`, the self-check, and the run file's `written`, `file`, `bytes` and `error`. |

The show helper Notes each show's reading and the palette helpers their
`PALETTE_SWITCH:` and `PALETTE_RESTORE:` rows.

**Checks.** The self-check rows; the package was read; every requested
theme, set and id is the manifest's, every requested picture a visual
entry's, and `framework` and `engine` are named;
every selected entry is well formed and its HTML read; the selection holds a
run; the output directory is fresh and ready; the fixture's gates and
controls; the palette judgements; no outgoing message the runner did not
inject; the run file is written. The totals stage, never a run file written
from `onFinish`, then Checks that every selected run completed, every
completed run was `chatFocused`, none carries a runner refusal, every
scripted action was performed or skipped as unperformable, every requested
crop was saved, every requested picture is complete (only when one was
asked for), the copied `--attacks` scan discriminates
(`DiscriminatingScan`, the self-check's control leaks as its control) and no
widget leaked a URL into its validated ops, no scan gap, every run's hosts
finished before its final reading, no host accepted an `openTelegramLink`
call (no scripted action asks for one, and it would leave the bubble through
the app's t.me route), and `CheckNoBlockedLaunches`.

The three absences (no leak, no scan gap, no accepted `openTelegramLink`)
pass only over readings that cover every selected run
(`JudgeConformanceAbsences()`): the scan's control leaks prove it can match,
not that it saw the widgets, and an empty count of runs never read is no
absence. A run covers the leak absence with a final reading of a tap that
wrapped a host, no scan gap and at least one scanned validated batch; the gap
absence with a final reading of a tap that wrapped a host; the
`openTelegramLink` absence with its bridge calls. Otherwise it is named in
the Check's `uncovered=[<key>: <why>]` (no tap was armed, the tap wrapped no
host, the reading is not final, scan gaps, no batch scanned, the bridge
reading refused or never read) and the absence fails, its counts kept; an
empty selection certifies nothing either. Deaths, failed loads,
missed statuses, console limits, renderer violations and the layers hidden
over the runs are recorded, not failed: they are the widget's or the
product's, judged by the report. A totals pass is the runner's
completeness, never the client's conformance.

**Network.** Each run's tap is armed before its message exists and read
final after its hosts finished, so every validated batch of every host is
scanned. The fixture sends loopback TCP and UDP controls before the first
widget and after the last; only an external listener's record of them is
the positive control, and the listener sees only those ports. The pack never
confirms a layer and never follows a link.

## Lifetime and cleanup

`Runner::onFinish` is registered before any stage. On every exit path it
stops the outgoing-message watcher, aborts every pointer sequence still
running (its held buttons released and the list left), keeps every
incomplete run's last tap reading, disarms every played bot by its run's kept
key (the records stay with the runs), runs the fixture's idempotent release
(every tap disarmed by its kept key, every widget and filler message
destroyed by id, the controls closed, the list left, the option, chat and
a minimized window's state restored, never a maximized state the sizing
left), then writes the run file and the totals row without the totals
stage's Checks when that stage did not run. A run whose hosts did
not finish within 10 s of its removal stops every later injection, by name.

The palette chain's final restore is a stage before the fixture's teardown;
[test_palette](test_palette.md)'s own finish backstop restores an
outstanding switch if a launch is cut. The palette helpers persist settings
as their reference documents: `KeepApplied()` writes the settings when the
mode changed and, on a first switch, the other mode's theme slot, and a
restore saves the settings. Run on a disposable portable copy.

The messages are local client-id messages, never sent; their Web Storage
never reaches the disk. A key's typed text is cleared in its own stage; the
window's focus child is left on the compose field; the scroll position is
not restored. The window keeps the one-column geometry and position the
sizing gave it on its own screen (Telegram saves its geometry), and a
maximized or full-screen state the sizing left is not given back: the
window stays normal, the state the fixture's "settle after the teardown"
Checks ([one-column window](test_widget_fixture.md#one-column-window)). The
output directory keeps `run.json`, `cards/`, `crops/` and, with requested
pictures, `pictures/`.

## Message contexts

`test_widget_conformance_context.{h,cpp}` reads each manifest entry's
`message` at setup (`ParseManifestMessage()`) as `CLIENTS.md` ("The message
context") and the maintainers' web runner read it: only the first spec
counts, by its `kind`; later specs are not run (`message.specs` counts
them).

| First spec's kind | isCopy / canQuery / backgroundQuery | Chat it needs |
| --- | --- | --- |
| no `message` | F / T / T, no state | a user's private chat |
| `private` | F / T / T | a user's private chat |
| `group` | F / T / F | a basic group or a megagroup |
| `no-queries` | F / F / F | a user's private chat |
| `copy` | T / F / F | any chat |
| `live-copy` | T / T / F | any chat |
| `preview` | run as `private`: F / T / T | a user's private chat |

The flags are `WIDGET_MESSAGE_KINDS` of widget-sandbox's `sdk-policy.ts`,
taken from the kind whatever the spec's `allow_queries` says. The spec's
`chat_data` and `public_data` go into the media as given, as UTF-8: a
missing one stays unset, an empty one is present and empty; a copy keeps
the state it was sent with, and the framework drops a copy's `chatData`
itself. No widget id is set, so nothing polls the message. A `message` that
is not a non-empty array whose first item is an object, any other kind
(`via-bot` included, which no `v0.2.3` entry asks for) and a state that is
not a string make the entry malformed, by name. The runner sets neither the
chat type nor the viewer: `WidgetMedia::context()` derives the chat type
from the message's chat and flags, `HostContext()` in `tgw_runtime.cpp`
gives the account's own user as viewer where `canQuery`, and the widget's
own checks observe both, verbatim.

**Chats.** `SelectRuns()` knows only the chat's `PeerId` and reads no
account state: a run whose context needs a user's chat is selected only when
the id is a user's, a group run only when it is a basic group's or a
channel's, a copy always. Any other run is omitted from that launch with
`elsewhere` true and its reason in `omittedRuns` and the notes; a launch on
a fitting chat runs it, and only such omissions may be dropped when launches
are combined. Right before a group run's injection `InjectRun()` reads the
fixture's loaded peer: a broadcast channel is a wrong fixture, so the run is
refused `not injected: ...` before anything is added and stays in
`incompleteRuns`, failing the totals by name. At `v0.2.3` a launch in a
user's private chat runs everything but `auto/26-context-group` and
`visual/sdk/25-group-chat-state`, which a launch in a megagroup or basic
group runs.

**Previews.** The client builds no host with `context.preview` (inline
results are listed as article rows) and the owner deferred preview hosts
pending preview design mockups (2026-10-26), so the runner builds none
either. An auto entry whose message is a preview (`auto/26-context-preview`)
is omitted in every launch, in both themes, with that reason naming
`tw.preview` and `behaviour.preview`. A visual one (`visual/sdk/23-preview`)
loads as a private message with its state, as a load check, in a user's
chat; its `message` extra records the requested kind `preview`, the kind
`private` it ran as and the untested `previewLeg`, and a note says no
preview context is claimed from it.

**After the injection** `CheckInjected()` reads `WidgetMedia::context()` of
the checked item into `ConformanceRun::context`: the flags and the chat
type the client derived. `InsertContextExtras()` writes the run's `message`
extra, for completed and incomplete runs of an entry whose kind was read,
beside its `bot` extra; an observation not made stays out:

| Field | Meaning |
| --- | --- |
| `requestedKind` | The first spec's kind, `private` without `message`. |
| `kind` | The kind it ran as: `private` for a preview. |
| `implicit` | No `message`: the private chat's context with no state. |
| `specs` | The entry's specs; only the first is run. |
| `flags` | `isCopy`, `canQuery`, `backgroundQuery` as injected. |
| `state` | `chatData`, `publicData` as injected, `null` when unset. |
| `widgetId` | `null`: never set. |
| `previewLeg` | Only for a preview: why its preview leg is untested. |
| `derived` | Only once read: the flags and `chatType` (`private`, `group`, `supergroup`, `channel`, or `null` for none) `WidgetMedia::context()` gave. |

The notes state the first-spec and default rule, the launch's chat by its
id and the chat types derived for its injected runs, each loaded preview's
requested and effective context, and what the played bot answers and
leaves unmeasured.

## Played bot answers

`test_widget_conformance_context.{h,cpp}` reads each manifest entry's `bot`
table at setup (`ParseManifestBot()`): each entry's exact data and its
`result` string, or none, for the played bot; every other key of an entry
(`edit`, `editShared`, `repeat`, only in `visual/sdk` tables at `v0.2.3`) is
named as unplayed, since the played bot edits no message and polls nothing.

`InjectRun()` arms [test_widget_bot](test_widget_bot.md) for every run whose
message was injected, an empty table included, in the same call right after
`InjectWidgetMessage()` returned, under the message's kept session-qualified
key. Nothing between them turns the event loop: the injection's layout
notification is synchronous, the host is built only after a paint, an
asynchronous `Build()` and `Runtime::constructed()`, and requests reach the
main thread through `crl::on_main`, so no request of the message can come
before the arming. The removal stage disarms by that key before
`RemoveFixtureMessage()`, independently of the tap's `armed` flag, and
`onFinish` disarms every held run before the fixture's release. The record
stays with the run until the run file is written.

A request is answered from the table as the reference bot answers it: the
listed `result` with `{n}` (requests with that data for the message so far),
`{from}` `Conformance` and `{time}` `12:00`; a listed entry without result and
data not in the table without result. A request its message's context
forbids (no `canQuery`, or no tap and no `backgroundQuery`) is refused by
the framework's gate before the client and reaches no record.

Each completed run, and each incomplete run that was injected, carries the
extra `bot`, written by the same `InsertContextExtras()`; a run never
injected has none and is never labelled played:

| Field | Meaning |
| --- | --- |
| `armed` | true: armed from the injection to the removal. |
| `played` | Whether any request was answered by the played bot. |
| `scope` | `message-lifetime`: the requests of every host of the message. |
| `hosts` | The run's tap reading's selected host factories. |
| `table` | The entry's table: `{<data>: {"result": <text>}}`, `{}` without result. |
| `unplayed` | `{data, operation}` of every operation not played. |
| `requests` | `{data, listed, n, result, sinceArmedMs}` in arrival order, `result` null when none. |

The record belongs to the message: a host restarted after a death or
re-created adds to the same requests and its `n` continues, so with `hosts`
above 1 the requests cannot say which host asked or published the run's
result. Such a run keeps `result.generation` and `result.retained` and its
summary's `summary-multiple-hosts` refusal; nothing retries it. The answers
are played: `messages.getBotWidgetQueryAnswer`, its errors and deadline, the
server and the bot are not measured by any run.

## Timed pointer sequences

`test_widget_conformance_input.{h,cpp}`, `Test::ConformancePointer`: the
manifest's `press`, `drag`, `dblclick` and `rightclick` as mouse events sent
to the chat's own `HistoryInner`, spaced in real time by a `base::Timer`.
Only the action stages use it. A held button needs the widget core's clock to
run while it is down, and TwidgetCore keeps only a pointer's latest move until
its next frame (twidget's `docs/TWIDGET_CORE.md`,
`core/src/renderer.cpp`), so
`Test::Drag`'s synchronous burst reaches a widget as one move. Taps, the
wheel and keys stay with [test_widgets](test_widgets.md#input-helpers).

- **Arguments.** `list` must be the legacy `HistoryInner`; `from`, and a
  drag's `to`, are list-local logical px inside its rect (the runner passes
  the action show's hover point). `hold` is a press's ms, `steps` a drag's
  moves (10 when not positive). `item` answers the message a right click's
  menu must belong to; `observe` is called right before and after every
  event and must only read (the runner reads the core's `pressed()` and
  `hovered()`).
- **Plans.** `press`: press, then the release once `hold` ms passed since the
  press was handled. `drag`: press, `steps` moves from `from` towards `to`
  with the left button held, each at least 24 ms after the event before it,
  then the release at `to` 24 ms after the last move. `dblclick`: press,
  release 40 ms later, `MouseButtonDblClick` 60 ms after that and its release
  40 ms later, the order `QWidgetWindow` gives a widget. `rightclick`: the
  right press and, 60 ms later, its release, with
  `Test::OpenChatListContextMenu` ([test_chat_menu](test_chat_menu.md)) for
  `item` at `from` in the turn of the press on a press trigger and of the
  release on a release trigger, as `QWidgetWindow` sends the context-menu
  event. The trigger is read as `tgw_touch.cpp`'s `MenuOnRelease()` reads it:
  `QStyleHints::contextMenuTrigger()` from Qt 6.8, `Platform::IsWindows()`
  before.
- **Delivery.** Each event is a `QMouseEvent` with its local and global
  position, Qt's button and buttons (a press and a double click include their
  button, a release excludes it) and no modifiers, sent with
  `QApplication::sendEvent` inside `Test::Settle`. A drag on a widget card
  presses the card's own link, which has no drag text, so
  `HistoryInner::prepareDrag()` makes no mime data and no platform drag
  starts.
- **Timing.** Every gap runs from the end of the previous event's handling,
  `Settle`'s drain included, and an event goes out only once the whole-ms
  clock is past its due time; a timer that fires early is re-armed. An event
  with a gap always gets an event-loop turn of its own, so an overdue move is
  sent late, never in one burst with the next. A late timer may lengthen a
  hold or a drag and never shortens one. `ConformancePointerPlannedMs()` is
  the sum of the planned gaps; the runner's stage timeout is that plus
  `kDefaultStageTimeout`.
- **Menus.** A menu the reader built and attributed is hidden at once
  (`Ui::PopupMenu::hideMenu(true)`); a refused one is left to the reader,
  which hides only what it built. The reader's enter resolves the real cursor
  first (`HistoryInner`'s `enterEventHook`), and its leave ends the list's
  hover even while the right button is still down. Every completed sequence
  ends with a leave: its own, or on a release trigger the reader's.
- **Reading.** `ConformancePointerReading`: every event as sent (its
  observations, local and global points, `gap`, `due`, `at` and `handled` in
  ms from the first send, its event-loop `turn`, button, buttons, the buttons
  held after it, `delivered`, `accepted`, `aborting`); `elapsed`, from the
  first press's handling to the last release's send, the least time a button
  was down; `minGap`, `maxLate`, `moves`; for a right click `trigger`, the
  menu reading, `menuHidden` and `menuAfter`; `left`, `complete`, `aborted`,
  `heldAtEnd`. `delivered()` is finished, complete and unrefused.
  `ConformancePointerJson()` is the whole reading, the action's `sequence`.
- **Refusals.** Before anything is sent: `no-list`, `not-history-inner`,
  `point-outside`. Later: `receiver-gone` (the list was destroyed before an
  event or by its own handler; the buttons it held are named) and `aborted`.
  The runner refuses an undelivered sequence `input-not-delivered` (a menu
  event the reader refused before delivery among them); a delivered right
  click for which the product built no menu is product behaviour.
- **Abort.** `abort()` cancels the timer, sends a release for every button
  still held and a leave, and ends the sequence aborted; with a button held
  and the list already gone it ends `receiver-gone`. Destroying a sequence
  cancels its timer and sends nothing. The runner aborts from `onFinish`
  before the fixture's release removes the messages.

## Bridge calls and uncaught errors

`test_widget_conformance_bridge.{h,cpp}`: `ReadConformanceSummary(log,
hosts)` reads `observed.bridge` and `errors` from the raw text `Summarize()`
in `tgw_runtime.cpp` leaves in `Tgw::Runtime::_summary`, which the fixture
keeps as `WidgetRuntimeLog::summary` with its provenance
([test_widget_fixture](test_widget_fixture.md)). Bridge calls never cross the
renderer seam the tap wraps, and `Runtime::Host` is defined only in
`tgw_runtime.cpp`, so this summary is the one route to the host's
`diagnostics()` from the harness. The outer `Runtime::logText()` is never
parsed: it appends `Notes:` after the summary, and a lookalike section there
would pass for the genuine one.

- **Provenance.** Only a `Fresh` summary (read in the `requestLog()`
  callback, right after the host's sample was stored) or a `Final` one (the
  last host's final sample) of a run whose tap selected exactly one host
  factory is read. Otherwise nothing is: `summary-unavailable` (absent, or no
  factory selected), `summary-stale`, `summary-multiple-hosts`.
- **The bridge section** runs from the last line that is exactly `Bridge
  calls: N.` to the end, since `Summarize()` appends it last: the formatter's
  `... K earlier entries not shown.` line, then one `<method>: <args JSON>`
  line per accepted call, which no newline can split. The method must be one
  `sdk::checkBridgeCall()` accepts (`ready`, `openLink`, `openTelegramLink`,
  `haptic`) and the arguments must parse as a JSON array. Refusals:
  `bridge-section-missing`, `bridge-section-ambiguous` (a line that is no
  call line), `bridge-unknown-method`, `bridge-args-clipped` (the formatter
  cut them at 240 characters), `bridge-args-not-array`,
  `bridge-count-mismatch` (N is not the lines plus K), `bridge-tail-omitted`
  (K above 0), `bridge-host-saturated` (N at `WidgetHost`'s 1000-call record
  cap). Any of them leaves `observed.bridge` out: calls are never presumed
  from a count, kept from a refused section or written as strings.
- **The console section** is the one line of the form `Console: N
  entries[, the host dropped all later ones].` before the bridge header, up
  to it. An entry starts at `<level>: ` (`log`, `info`, `warn`, `error`,
  `debug`); any other line continues the entry before it, as a stack line
  does. `errors` are the `error` entries whose text starts `Uncaught `, read
  whole; a logged `console.error` is none. `console-ambiguous` refuses the
  section when not exactly one line has the header's form, a continuation
  comes before any entry, or the entries are not the N - K shown, and
  `errors` is then left out: a continued line that reads as an entry cannot
  be told from one, and no error is invented. The bridge is read
  independently.
- **Limits.** Each summary field is cut at 240 characters (`kMaxLogLine`),
  the console tail at 24 KB and the bridge tail at 16 KB (`kConsoleBytes`,
  `kBridgeBytes`), and the host records its first 1000 calls
  (`kMaxRecordedCalls` in widget-sandbox's `widget_host.cpp`). Console
  entries not shown, a host that dropped later entries and clipped error texts
  are readings (`consoleOmitted`, `consoleDropped`, `errorsClipped`, the
  totals' `consoleLimits`), not refusals; the bridge's limits are refusals.
  None is expected at `v0.1.2`, whose accepted calls are short `ready`, link
  and `haptic` arrays; a refusal is incomplete evidence, never zero calls,
  in the run file's `host` as in the totals' absences
  ([Readings and refusals](#readings-and-refusals)).

## Verification

The pack's first stage runs, as Checks: `RunWidgetOpsSelfCheck()` (its
scanner control is the leak scan's control in the totals);
`RunWidgetBotSelfCheck()` ([test_widget_bot](test_widget_bot.md#verification));
`RunWidgetConformanceProbeCheck()` on the package's `probe-fixture.json`
([test_widget_ops](test_widget_ops.md)); the paint-policy fixture's rows
verbatim (`RunPaintPolicyFixture()` of
`test_widget_conformance_paint_policy.cpp`: `RunWidgetPaintPolicyCheck()`
([test_widget_paint_policy](test_widget_paint_policy.md)) on the bytes of
`package`'s sibling `../core/tests/fixtures/paint-policy.json`, read by its
cleaned path, the same pin's `packages/core/tests/fixtures/paint-policy.json`;
at `v0.6.0` 93 rows, the 92 entry rows in fixture order - constants 2,
shadows 13, boxes 31, hidesSubtree 4, animations 14, messages 2, reports 6,
cases 20 - and the coverage row; a `package` that is not an absolute path,
or a fixture that cannot be opened or read, gives one failed row instead,
`paint policy fixture: the fixture file was read`, with the path and the
error; empty or unparsable bytes give the checker's coverage row alone,
failed); two result-reader controls on
literal readings (this run's value taken with its checks verbatim once per
generation and serial; another run's id, text that is no JSON object and
checks that are no array refused, and a newer host's clear leaving no
value); `RunConformanceSummaryCheck()` on literal `Summarize()`-format texts
(calls in order with array arguments and one multi-line uncaught error while
a logged error is none; clipped and non-array arguments refused; a missing
section, a count mismatch, omitted calls, the host cap and an unknown method
refused; console lookalikes of a bridge section, a console header and an
error line before a genuine `Bridge calls: 0.` giving zero calls and no
invented error; a lookalike section in the outer log's notes never read;
stale, absent and multi-host summaries refused);
`RunConformanceObservationCheck()` (no observed bridge and every host check
judged, the bridge-counting ones skipped naming why with no count, for
refused, stale, multi-host, clipped, omitted, sectionless and unread
readings, against the host checks of the pinned manifest's four entries that
have them; height and focus judged as `hostChecks()` does, in and out of
bounds, unmeasured, kept, moved with and without a holder, expected failures
and synthetic input; measured empty and exact calls as `observed.bridge` with
no `host`; with a good scan control, runs without a tap, hostless,
zero-batch, non-final and gapped left uncovered, runs read in full certified,
no control or no run certifying nothing; refused and unread bridge readings
uncovered, one of several or all); three run-file controls (a completed run
with the schema's fields, verbatim checks, array arguments, a refused stage
without a crop or judgement and only a true skip in `skippedActions`; an
incomplete run only in `incompleteRuns` and an omission in `omittedRuns` and
the notes, `elsewhere` true only for one left to another chat; a refused
bridge reading through `RunFileObject()` with its
manifest entry's checks in `host`, and a reading of no call without);
`RunContextCheck()` on literal entries (the pinned kinds' flags whatever
`allow_queries` says, with their state, a second spec not run and a preview
run as private; no `message` the private context with no state; a
copy-shaped spec keeping its state; an empty `chat_data` present beside a
missing `public_data` unset; a `message` of the wrong shape, `via-bot`, a
missing kind and a non-string state malformed by name; every kind's chat
requirement over a user's chat, a basic group, a channel by its id, a
megagroup and a broadcast, and `ChatPartition()` by id, though
`ChatRefusal()`'s reading of a real loaded peer is not in it; a `bot`
table's results, an entry
without result and its `edit`, `editShared` and `repeat` read as unplayed,
no `bot` an empty table, a `bot`, an entry and a `result` of the wrong type
malformed by name; the extras of a run whose record played a listed and an
unlisted request under a peer-0 id - its `bot` with its table, unplayed
operation, host count and scope, its `message` with the derived context -
beside a run never injected that has its requested context only);
a selection control (`SelectRuns()` of literal entries in a user's chat by
its id: the auto preview omitted in both themes naming both catalogue IDs,
`elsewhere` false; the visual preview run as private with its state and
`previewLeg`; group entries of both sets omitted with `elsewhere` true in
the state and in `omittedRuns`; `matrix` and the notes counting only the
permanent omissions against the matrix); a host reading control
(`CheckHostReadings()` of `test_widget_conformance_hosts.cpp`, with the
result wait's `ResultWait` and `WantedStatus()` declared in `_state.h`
for it, the only place the three new refusals' positive
cases and the stop wait run, as a live campaign shows only their
negatives: `SafeAreaRefusal()` gives none for an entry without `safeArea`
with all 0, `safe-area-not-zero` for `bottom` 26 and for `cornerRight` 118
alone, `safe-area-unmeasured` for a loaded run with no `applied` and none
for one not loaded, and none for a `safeArea` entry with 26/118 or with
all 0; `StopRefusal()` gives none for a stop run that died with a `Died: `
line, `stop-reason-unmeasured` for one that died without, and none for a
stop run that did not die or a run without `stops` that died without one;
`WantedStatus()` at `done` is false for a `stops` entry and true for a plain
auto entry, in a literal `ConformanceState`); a desktop stop control
(`RunDesktopStopCheck()` of `test_widget_conformance_stop.cpp`:
`StopRefusal()` of a stop run that died with only `Died: terminated` gives
none with a stop the desktop decided itself and `stop-reason-unmeasured`
without one); `RunPictureCheck()` (requested
visual, auto, unknown, unselected and no picture ids through
`SelectPictures()` and the notes; the bands of a 544 CSS px card and the
completeness of 336, 280 and 650 CSS px pictures, with band 1 refused,
shifted, not needed or nothing captured; band 1 joining band 0 only from
its core, host generation and card size after band 0 was saved; the date
wait over literal scroll readings and times: the show's scroll top held
for 3350 ms accepted and not a ms sooner, a scroll away and back
restarting the count, another top refused `date-not-hidden` at the 8 s
bound and not a ms sooner, a lost scroll and a show that showed nothing
decided at once, no wait and an unended one refused by name); and a
totals control
(`CountTallies()` over runs no tap was armed for, one without a bridge
reading, beside a good scan control).

Executed so far, by
`2026/10/24/run-the-widget-conformance-sets-through-the-chat-list-and-report`
(its evidence records the commands and output): the macOS arm64 Debug
`Telegram` build after each phase, and four checks outside the app, with
stand-in harness headers and static Qt 6.11.2:

- the selection and run-file model, before the per-run stages existed: the
  full selection gave 120 runs (40 light and 39 dark auto, `auto/20` dark
  omitted with its note, 37 light and 4 dark visual); that run file, still
  without completed runs, validated against `run.schema.json` with a
  task-local interpreter of its keywords, and `report.mjs` exited 0; four of
  five mutations of the reader and the model failed their rows, and the fifth,
  dropping the omission's first-action premise, changes nothing at `v0.1.2`;
- the summary reader: its six rows, and summaries made by a verbatim copy of
  `Summarize()` (a multi-line uncaught error, 5000 console entries, 40
  clipped calls, 1000 haptics, a console lookalike, a kind detail holding a
  console header, a dead host's `openTelegramLink`); four mutations (the
  outer text parsed, the first header taken, no entry count, calls kept
  despite refusals) failed their rows;
- the pointer sequences: 18 rows on a real `QApplication` with a stand-in
  list and menu reader, clean in six runs, and seven mutations caught (moves
  in one burst, no re-arm, gaps from the send, the trigger ignored, the menu
  not hidden, an abort without its release, moves without the button). The
  real `HistoryInner`, `HtmlWidget`, widget core and menu reader were not in
  it;
- the observation and absence functions, after the review that found
  refused bridge readings reaching `report.mjs` as zero calls: the bridge
  module's eleven rows, and its serialized run entries (six refusals, an
  unread reading, measured empty and exact calls, four observation variants)
  through the pinned `report.mjs`, imported and as a command: every
  bridge-counting check `skip` with no count, its catalogue IDs `skip`, every
  other check equal to `hostChecks()`'s own judgement of the same `observed`,
  the measured cases judged by `report.mjs` itself, all valid against
  `run.schema.json`; the same runs without `host` gave the review's false
  zero-call passes and failures. Seven mutations (no `host`, zero batches or
  non-final readings or unknown bridges counted as covered, no control
  needed, another holder fallback, the console refusal kept) failed their
  rows, and the first and the sixth the round trip too. `RunObject()`,
  `CountTallies()` and the totals' Checks were not in it.

Executed for the `v0.5.0` adaptation (the stop wait, the caption, the
empty-override watch with its `safeArea` extra and refusals, the build name
and the host reading control), by
`2026/10/29/report-the-desktop-conformance-and-run-the-stress-kit-at-widget-sandbox-v0-3-0`:
the macOS arm64 Debug and Release (no LTO, with the harness) `Telegram`
builds of the change, and a Release smoke of `auto/31-stop-text`,
`auto/04-values-units`, `auto/36-safe-area` and `visual/17-time-corner`
recorded in its `work/smoke.md`: `CheckHostReadings()`, then in
`test_widget_conformance.cpp` itself, passed in the app;
`auto/31-stop-text` died with `dead` "Text limit exceeded: a text value of
40000 characters (max 32768)" from its final summary and no refusal; the
two captioned runs got all 0 and the two `safeArea` entries `bottom` 26
with `cornerRight` 118, which `auto/36-safe-area` read back.

That task's full `v0.5.0` campaign ran afterwards on its Release build
without LTO, with the harness: four counted launches, merged into one run
file of 168 runs (117 auto and 51 visual) with their crops and pictures,
which `report.mjs` turned into the report. Its `work/test.md` records the
commands and readings and its `work/discrepancies-v0.5.0.md` the per-ID
report; the smoke above was pre-review validation, not campaign evidence.

Executed for the `v0.6.0` adaptation (the paint-policy read of
`test_widget_conformance_paint_policy.{h,cpp}` and its first-stage call),
by `2026/11/02/report-the-desktop-conformance-at-widget-sandbox-v0-6-0`:
the macOS arm64 Debug `Telegram` build compiled and linked them, and its
`v0.6.0` campaign on the Release build without LTO, with the harness, ran
them in the first stage of every launch: all 93 rows (92 entries and the
coverage row) of the pin's `paint-policy.json` passed in each of its
eight launches (the four merged into the report and the four they
replaced), and one launch on a package copy without the
sibling `core/` failed only the read row, "the fixture file was read"
(`No such file or directory`), which `selfCheck.failed` named. Its merged
run file holds 184 runs (133 auto and 51 visual); its `work/test.md`
records the commands and readings and its `work/discrepancies-v0.6.0.md`
the per-ID report.

The [desktop stop](#preconditions-and-limits) reading of
`test_widget_conformance_stop.{h,cpp}`, its `RecordLog()` call, the
`StopRefusal()` argument and the two literal cases of
`RunDesktopStopCheck()` compiled in the Windows Win32 Debug build of
`2026/10/31/stop-a-widget-on-its-last-picture-when-the-paint-bounds-refuse-its-frames`.
`RunDesktopStopCheck()` runs in the pack's first stage, so only a launch of
the pack runs it; that task's evidence loop runs it and a synthetic smoke
of a paint stop, and neither has run yet. Before that build this page
recorded no Windows build or run of the pack; the code is portable C++ and
Qt.

The [window sizing](#preconditions-and-limits) and the move of the host
readings into `test_widget_conformance_hosts.{h,cpp}` have not run in the
app yet. The evidence campaign of
`2026/10/31/put-the-conformance-runners-window-in-the-one-column-layout`
runs a two-entry smoke from a normal window narrowed to the first
two-column width and again from an inactive maximized window, and records
what ran in that task's evidence; until then this page claims no measured
width, focus or window-state result for the sizing.

## Related guidance

[Shared widget fixture](test_widget_fixture.md);
[validated-ops tap](test_widget_ops.md);
[widget attack audit](test_widget_attacks.md);
[chat-list show before input](test_chat_show.md);
[chat-list context menus](test_chat_menu.md);
[palette switch and restore](test_palette.md);
[capture selection](test_capture.md);
[input helpers](test_widgets.md#input-helpers);
[window exposure](test_window_exposure.md);
[launch fuse](test_launch_fuse.md);
[session gate](test_session_gate.md);
[scenario design](../scenarios.md);
[scenario teardown](../teardown.md);
[chat-list resolution](../chat-lists.md).
