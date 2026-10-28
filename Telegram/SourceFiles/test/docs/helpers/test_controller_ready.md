# test_controller_ready

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API (includes a harness self-test).
Source contract: [test_controller_ready.h](../../test_controller_ready.h).

## Use when

An overlay acts through, or must find, one exact account's window, a
separate window it opened, or the window of a session a fixture resolved.
`Test::ResolveController` answers which registered `Window::Controller` and
`Window::SessionController` show that session and whether they are ready
right now; `Test::ActOnController` waits for that answer and acts once on
the accepted controller. The primary window the session gate admitted is
still read with `Core::App().activePrimaryWindow()` by the existing helpers
and needs no resolution here.

## Registry versus activation history

The registry is `Core::Application`'s window map, which owns every live
`Window::Controller`, one per `Window::SeparateId` key. It is read through
`Core::Application::enumerateWindows`, and `separateWindowFor(id)` is its
exact key lookup. Windows enter it at startup (the first primary window) and
in `ensureSeparateWindowFor`, are re-keyed by `checkWindowId` when the
primary window shows another account, and leave it in `closeWindow`. The
primary `Window::Controller` is reused across account switches, while its
`SessionController` is a new object for every session it shows.

Activation history is `Core::Application::windowStack()`, together with
`activeWindow()` and `activePrimaryWindow()`. Only `setLastActiveWindow`
writes it: at startup, in `checkWindowId` while no window is active, in
`closeWindow`, and in `windowActivated`, which runs when the OS reports a
window active. `ensureSeparateWindowFor` registers a new window at once but
only requests its activation (`Window::Controller::activate()`), and the
answer arrives asynchronously, if ever. A default background `-testagent`
launch on macOS does not activate the client, and the window server may
grant or refuse a later request ([launch behavior](../launch.md)), so a
just-created window can be registered, visible and ready while activation
history never lists it. Run 1 of
`2026/10/17/open-ones-own-qr-box-without-a-username-with-temporary-only-codes`
(its `work/test.md`) searched `windowStack()` right after creating the
importer's window and found no ready importer controller, although that
account's registered window existed.

Two nearby core calls are not readings. `windowFor(id)`, for an id with no
registered window, tries the account's primary key and then returns
`activePrimaryWindow()`, which may show another account.
`Main::Session::tryResolveWindow()` may call `domain().activate()`, an
account switch. This module reads the registry only. Each reading copies the
window's activation rank (`activation`), whether it is
`activePrimaryWindow()` (`activePrimary`) and the history's size
(`activationSize`) as facts, and no decision reads them.
`Main::Session::windows()` serves the self-test as an independent oracle
only.

## Preconditions and limits

- Main thread only: call it from stage callbacks.
- Test sessions only. A session not on the Test environment refuses
  `non-test`: the session gate admitted no other, and a controller resolved
  here need not be the one the gate read. Production legs are not
  supported, `Test::DeclareProductionLegs` included.
- It opens, activates and switches nothing. An account without a registered
  Primary-type window refuses `no-registered-window` until the caller opens
  one in an earlier stage, and that stage is bound by the session gate's
  rule that no stage switches the primary window or `domain().active()` to
  another account ([session gate](test_session_gate.md)). Another account's
  Primary-type window (`Window::SeparateId(account)`) becomes
  `activePrimaryWindow()` as soon as the OS activates it (`windowActivated`),
  and every helper reading the primary window then acts as that account.
- One answer per request. `Test::ExactController(session)` selects the
  account's Primary-type registered window, the one
  `separateWindowFor(Window::SeparateId(account))` finds; it never picks
  among separate windows. Name a separate window with
  `Test::ExactController(session, window)` and a
  `base::weak_ptr<Window::Controller>` the overlay kept: a closed or
  never-bound window refuses `destroyed-window` and never falls back to the
  account's primary window.
- Build the request inside the poll from weak handles (`session.get()`).
  `ExactController` reads only `Main::Session::account()`, never
  `Window::Controller::account()` (which asserts on the startup window), and
  a null session gives `no-session`. `ActOnController`'s `request` callback
  must only read.
- An accepted reading does not establish a painted, exposed or unobscured
  frame, OS activation, keyboard focus, membership in activation history, or
  which chat or section the controller shows. Keep a covered window painting
  with [window exposure](test_window_exposure.md); route focus-dependent
  input through the [input helpers](test_widgets.md#input-helpers). Which
  message list a chat's separate window builds is in
  [chat-list resolution](../chat-lists.md#a-chats-separate-window).
- The reading text prints storage indices, counts and flags only, never a
  user id, `uniqueId()`, name, peer or address.

## Minimal use

An overlay sketch against the real API; no translation unit compiles it.
`state->importer` stands for a Test session an earlier stage resolved, whose
account's primary window is registered (shown at launch, or opened by an
earlier stage under the session gate's rule); `state->window` for a window
an earlier stage opened (for example
`state->window = Core::App().ensureSeparateWindowFor(id);` in its `run`);
and `ShowsImportedChat` for the overlay's own outcome predicate.

```cpp
struct State {
	base::weak_ptr<Main::Session> importer;
	base::weak_ptr<Window::Controller> window;
	base::weak_ptr<Window::SessionController> acted;
	QString url;
	QString gate;
};
const auto state = std::make_shared<State>();

runner->addGated([=] { return state->gate; }, [&] {
	// The importer account's primary window: waits, then routes once.
	Test::ActOnController(
		runner,
		u"importer route"_q,
		[=] { return Test::ExactController(state->importer.get()); },
		[=](not_null<Window::SessionController*> controller) {
			UrlClickHandler::Open(
				state->url,
				QVariant::fromValue(ClickHandlerContext{
					.sessionWindow = base::make_weak(controller),
				}));
		});

	// Exactly the window the overlay opened, never a fallback.
	Test::ActOnController(
		runner,
		u"importer separate window"_q,
		[=] {
			return Test::ExactController(
				state->importer.get(),
				state->window);
		},
		[=](not_null<Window::SessionController*> controller) {
			state->acted = base::make_weak(controller);
		});

	// A stage's own check, with the reading as its details.
	runner->add({
		.name = u"importer shows the chat"_q,
		.then = [=] {
			const auto reading = Test::ResolveController(
				Test::ExactController(state->importer.get()));
			const auto controller = reading.controller();
			Test::Check(
				controller && ShowsImportedChat(controller),
				u"importer shows the imported chat"_q,
				Test::ControllerReadingText(reading));
		},
	});
});
```

`controller()` is null for every refusal, so a refused reading fails that
Check with the refusal in its details. The routing action is one product
call; check what it starts in a later stage.

## Readings and refusals

`ResolveController(request)` decides over one fresh registry read. The
first refusal that applies, in this order, is the reading's `refusal`:

| Order | Name | Refuses when |
| --- | --- | --- |
| 1 | `no-session` | The requested session handle is empty: never bound (a null session) or its session destroyed. |
| 2, exact window | `destroyed-window` | The requested window handle is empty: never bound, or the window closed. |
| 2, exact window | `wrong-window` | The window is alive but not registered. |
| 2, exact window | `wrong-account` | The window is registered for another account. |
| 2, account primary | `no-registered-window` | The account has no Primary-type registered window: an absent controller, whatever activation history says. |
| 3 | `wrong-session` | The window's account shows another session or none, or its SessionController shows another session. |
| 4 | `non-test` | The session is not on the Test environment. |
| 5 | `unready` | No SessionController, or an empty widget size. |
| 6 | `locked` | `Window::Controller::locked()`: a passcode lock, or the shown session's terms lock. |
| 7 | `hidden` | The widget is not visible, or is minimized. |

`none` means accepted. `not-evaluated` is the default and never accepts.
Only the identity re-check before an action gives `replaced-session` or
`replaced-window` (see below).

`ControllerReadingText(reading)` is one line (wrapped here):

```
controller verdict=<accepted|refused> refusal=<name>
  request=[window=<account-primary|exact-live|exact-gone>
  session=<live|none>] registered=<n> accountWindows=<n>
  activationSize=<n> entry=[none|index=<i|none>
  type=<Primary|Archive|Chat|Forum|Community|SavedSublist|SharedMedia>
  test=<0|1> session=<same|other|none> controller=<present|none>
  shown=<same|other|none> visible=<0|1> minimized=<0|1> sized=<0|1>
  locked=<0|1> activation=<rank|absent> activePrimary=<0|1>]
  dispatched=<0|1>
```

- `registered` counts the windows read, `accountWindows` the registered
  windows of any type naming the requested account, and `activationSize` is
  the size of `windowStack()`.
- `entry=[none]` means the decision selected no window: `no-session`,
  `no-registered-window`, `destroyed-window` and `wrong-window`. Otherwise
  it describes the selected window: `index` is the account's `Main::Domain`
  storage index (the session gate's index), `session` is the account's
  current session and `shown` the session its SessionController shows, each
  compared with the requested one, and `activation` is the rank in
  `windowStack()`, 0 being the most recent (`activeWindow()`).
- `window=`, `session=` and every handle-derived entry field read liveness
  when the text is made, so a retained reading's text shows what has died
  since.

An acceptance proves, at the moment of the reading: the window is registered
(the account's Primary-type one, or exactly the requested one) for the
requested account; that account's current session is the requested live
session, on the Test environment; the window's SessionController exists and
shows that session; and the widget is unlocked, visible, not minimized and
of non-empty size.

`activation=absent` on an accepted reading is a registered, ready window
that activation history does not list, the Run 1 importer window.
`no-registered-window` is an absent controller: with `accountWindows=0` the
account has no registered window at all, and with `accountWindows` above 0
it has only separate windows, which the exact-window form requests.

A refusal is never a PASS. `ActOnController` treats every refusal as "not
ready yet" until its timeout FAILs; a caller's own Check puts `accepted()`
or `controller()` into its condition.

## Retained readings and the action

Identity is liveness. Every handle (`Test::ControllerHandle`, a
`base::weak_ptr<const base::has_weak_ptr>`) shares its object's alive
tracker, and two handles name the same object only while it is alive. A new
object created at a freed address gets a new tracker, so a retained handle
never names a replacement. The self-test's fixture identities are plain
`base::has_weak_ptr` objects that go through the same decision.

A reading is a value. `accepted()` describes the moment it was taken and
never changes, and a refused reading never turns accepted: take a fresh
`ResolveController` reading. `controller()` is the typed controller only
while the reading is accepted and that controller is alive; it is null once
the controller is destroyed (its window closed, its session logged out, or
the primary window switched to another account), never a stale or
replacement controller. A live `controller()` of a retained reading is the
same controller, not a fresh readiness answer: it may since have been
hidden or locked.

`ActOnController(runner, name, request, action, timeout)` appends one stage,
`act on controller: <name>`. Its `until` resolves `request()` on every poll.
Its `then` runs in the same `Runner::tick()` as the accepting poll
(`Runner::completeStage()`), so its re-check is an identity guard, not a
second wait. Against a fresh registry read the accepted reading refuses as:

- `destroyed-window`: its window closed;
- `replaced-session`: its session was destroyed (tested before the
  controller, so a logout reads as a session replacement);
- `replaced-window`: its SessionController was destroyed, or a fresh
  decision of the same request accepts another window or controller;
- any fresh refusal, by its own name.

Only a reading that survives the re-check runs `action`, exactly once, with
that controller, and writes
`NOTE: CONTROLLER_READY: act=[<name>] <text>` with `dispatched=1`. Otherwise
the stage writes one
`TEST_RESULT: FAIL: act on controller: <name> - accepted controller changed before the action: <text>`
and does not call `action`. A timeout writes
`TEST_RESULT: FAIL: stage timed out: act on controller: <name> - waited <ms> ms; last state: <text>`
with the last poll's reading, and finishes the run as every stage timeout
does. Inside `Runner::addGated` the stage inherits the group's gate: a
refused gate writes `N/A`, and the stage neither polls nor acts.

## Lifetime and cleanup

Readings, requests and entries hold weak handles only and own nothing.
`ActOnController` keeps its last reading in the stage's own closures. There
is no subscription, timer or `onFinish` obligation for callers. A caller that
opens a window closes it itself, in a settling stage and in `Runner::onFinish`
for abort paths, as the self-test does.

The self-test opens an Archive window of the primary window's Test session
through `Core::Application::ensureSeparateWindowFor`, what
`SessionController::showInNewWindow` does for the product's "Archive -> New
window" (`Window::Filler::addNewWindow`). That requests OS activation, may
produce the product's own presence update (`account.updateStatus`, sent only
when the online state changes), loads the archive list read-only (creating
the local archive folder object if absent), hides a pending saved-window
restore offer if one is shown (`Window::SavedWindows::attachToWindow`), and
rewrites the local saved-window set. Its last stage closes the window
through `Core::Application::closeWindow`, which may re-activate the primary
window, records the window in the product's in-memory list of recently
closed windows and rewrites that set again. Its `Runner::onFinish` closes it
on abort paths (a stage timeout, the watchdog), since a window left open
would join the saved-window set at quit and reappear on the next launch. It
closes only a window it opened, sends no request of its own, switches no
account and writes no server fixture.

## Verification

`Test::AppendControllerReadySelfTest(runner)`, called from a disposable
overlay's `SetupScenario` after `runner->waitEvent(u"launch_finished"_q);`.
Add `Test::UseTestSession` and `Test::AppendTestSessionRestore` (the last
stage) only on a copy whose primary window opens on a non-Test account. It
needs a copy whose primary window shows an unlocked, authorized Test
session, and no Archive window restored at launch. Its stages are named
`controller ready self-test: <leg>`, plus the two `act on controller:` stages:

- fixture: two Test controllers, activation history holds one. Local
  identities for accounts A and B are both accepted with their own window
  and controller, A with `activation=0` and B with `activation=absent`, and
  both stay accepted with the same handles under an empty and a B-only
  history; account C, with no registered window, refuses
  `no-registered-window` with `accountWindows=0 registered=2`. Control: A,
  present in the history, accepted in the same registry.
- fixture: refusals. `wrong-account`, `wrong-session` (a request and an
  entry form), `wrong-window`, `destroyed-window`, `no-session`, `non-test`,
  `unready` (no controller; empty size), `locked` and `hidden` (not visible;
  minimized), each beside an accepted control that differs in that one
  field.
- fixture: retained generations. A retained reading re-checked over an
  unchanged registry stays accepted (control); after its controller,
  session or window is destroyed and reallocated it refuses
  `replaced-window`, `replaced-session` (its request then `no-session`) and
  `destroyed-window`, while a fresh request accepts each replacement; a
  refused reading stays `no-registered-window` after its account gains a
  window. A `reusedAddress` NOTE says whether the allocator reused an
  address (diagnostic only).
- live: Test session prerequisite. The fixture gate: the primary window
  shows an unlocked Test session, as `ReadSessionGate()` agrees.
- live: open a second window of the Test session. The Archive window opens;
  the selection snapshot sees the registry change, the sensitivity control
  of the purity check.
- live: two controllers by registry. Both windows are accepted with
  distinct windows and controllers; the primary reading names the primary
  window, its account key's window, its controller and the session gate's
  index; the Archive reading names the opened window, its key's window and
  its controller; both controllers are in `session->windows()`;
  `ResolveController` gives the same answers in that turn; both stay
  accepted under a primary-only and an empty history, the Archive window
  reading `activation=absent` under the primary-only one (the Run 1 shape).
  A NOTE records the real activation ranks.
- live: polls observe only. In one turn, 60 polls (the two windows and an
  absent account, 20 each) leave the registry, activation history, active
  primary window and active account unchanged, give 40 acceptances and 20
  `no-registered-window`, and run no action.
- act on controller: self-test primary window, then self-test second
  window. The first stage's request names an absent account for its first
  three polls, a caller-controlled delay; each stage acts once and writes
  its `CONTROLLER_READY` NOTE.
- live: one action per accepted target. Each action ran once on its own
  window's controller, the primary one after its refused polls and with no
  poll after it, and the active primary window and account are unchanged.
- live: refused targets receive none. `wrong-account`, `wrong-session`,
  `wrong-window` and absent-account requests keep their refusals through
  the shared dispatch with no action; control: the same dispatch of a fresh
  accepted primary reading acts once.
- live: a closed window refuses. After the self-test closes its Archive
  window, its retained reading, its dispatch, its retained request and a
  request built from the closed window's weak handle all refuse
  `destroyed-window` with no action and no fallback to the primary window;
  the primary reading stays accepted for the same controller; the Archive
  key is unregistered and the active primary window and account are
  unchanged. A NOTE prints the selection before and after; the whole
  registry is not compared across turns, because a deferred saved-window
  restore may add windows once the OS first grants activation.

Expected on such a copy: `SCENARIO_RESULT: PASS (failures: 0)` with no FAIL
and no N/A row. On an account-free copy the prerequisite writes one
`TEST_RESULT: FAIL: fixture gate: the primary window shows an unlocked Test session - ...`
and the eight later stages read
`TEST_RESULT: N/A: <stage> - fixture gate: no unlocked Test session in the primary window`,
so `SCENARIO_RESULT: FAIL (failures: 1, skipped: 8)`: the fixture legs ran,
the live part is unproven, and that is a blocker, not a pass. An Archive
window that already existed makes the close leg N/A ("the Archive window
preexisted; the self-test closes only windows it opened"); rerun on a copy
without one before claiming the live destroyed-window check.

The controls above run inside every launch of the self-test. Session
replacement, passcode and terms locks, and hidden or minimized windows are
covered by fixture legs only: producing them live would need a login, a
local passcode or a product UI change. Proposed, not part of the self-test,
and executed only when a reviewer doubts that the fixture B and live Archive
checks discriminate: one disposable mutation build whose decision accepts
only entries with `activation >= 0` (Run 1's search), expected to FAIL those
rows. Record executed results in the change's evidence, and say when the
mutation was only proposed.

## Related guidance

[Runner contract](test_runner.md); [session gate](test_session_gate.md);
[launch behavior](../launch.md); [failure diagnosis](../diagnostics.md).
