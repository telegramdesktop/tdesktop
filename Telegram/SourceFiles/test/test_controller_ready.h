/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Usage, refusals and cleanup: docs/helpers/test_controller_ready.md.

#include "base/weak_ptr.h"
#include "test/test_runner.h"
#include "window/window_separate_id.h"

#include <QtCore/QString>

namespace Main {
class Session;
} // namespace Main

namespace Window {
class Controller;
class SessionController;
} // namespace Window

namespace Test {

// Exact account/session controller readings for -testagent overlays: which
// registered Window::Controller and Window::SessionController show one
// requested session (in the account's primary window, or in one exact
// window), and whether that controller is ready right now.
//
// The registry is Core::Application's window map, read through
// Core::Application::enumerateWindows. Windows enter it in
// ensureSeparateWindowFor (and at startup), are re-keyed by checkWindowId
// and leave it in closeWindow.
//
// Activation history is Core::Application::windowStack(), together with
// activeWindow() and activePrimaryWindow(). It changes only in
// setLastActiveWindow: at startup, in checkWindowId while no window is
// active, in closeWindow, and in windowActivated when the OS reports a
// window active. A background -testagent launch may never activate a
// just-created window, so here activation history is a diagnostic fact,
// never the registry. windowFor() falls back to the active primary window,
// so it is no exact lookup either.
//
// Every reading is pure: it activates, switches and creates nothing, runs
// no product action and sends no request. It never calls
// Window::Controller::account() (it asserts on the startup window),
// Main::Session::tryResolveWindow() (it may activate an account) or
// windowFor().
//
// Only Test-environment sessions are accepted: the session gate
// (test_session_gate.h) admitted no other, and a controller resolved here
// need not be the one the gate read.
//
// An accepted reading does not prove a painted, exposed or unobscured
// frame, OS activation or focus (KeepWindowExposed in
// test_window_exposure.h, ForceWindowActive in test_widgets.h).
//
// Main thread only.

// An identity handle, never dereferenced. Two handles name the same object
// only while that object is alive. A never-bound handle is null(); the
// handle of a destroyed object is empty() and matches nothing, so a new
// object created at a reused address never revives it.
using ControllerHandle = base::weak_ptr<const base::has_weak_ptr>;

// What a caller asks for: the account and session to find and, when
// |exactWindow| is set, the one registered window that must show them.
struct ControllerRequest {
	ControllerHandle account;
	ControllerHandle session;
	ControllerHandle window;
	bool exactWindow = false;
};

// The one-argument form asks for the account's Primary-type registered
// window. The two-argument form asks for exactly |window| and sets
// exactWindow, so a closed or never-bound window refuses as
// destroyed-window and never falls back to another window. Both take
// account = &session->account(), and leave account and session unbound
// when |session| is null. Safe to call from a poll.
[[nodiscard]] ControllerRequest ExactController(Main::Session *session);
[[nodiscard]] ControllerRequest ExactController(
	Main::Session *session,
	const base::weak_ptr<Window::Controller> &window);

// One registered window as the decision sees it, read from the registry or
// built by a caller-controlled fixture:
// - window: the Window::Controller; account: the account its id() names;
//   session: that account's maybeSession();
// - controller: its sessionController(); shownSession: that controller's
//   session();
// - target: the same controller, typed; filled only by the real reader;
// - type: id().type;
// - accountIndex: the Main::Domain storage index of account, or -1;
// - activation: the recency rank in windowStack(), 0 being the most recent
//   (activeWindow()), or -1 when the window is absent from it;
// - test: whether session is on the Test environment (isTestMode());
// - visible, minimized, sized: the widget's isVisible(), isMinimized() and
//   a non-empty size();
// - locked: Window::Controller::locked() (a passcode or terms lock);
// - activePrimary: whether this window is activePrimaryWindow().
// activation and activePrimary are facts; no decision reads them.
struct ControllerEntry {
	ControllerHandle window;
	ControllerHandle account;
	ControllerHandle session;
	ControllerHandle controller;
	ControllerHandle shownSession;
	base::weak_ptr<Window::SessionController> target;
	Window::SeparateType type = Window::SeparateType::Primary;
	int accountIndex = -1;
	int activation = -1;
	bool test = false;
	bool visible = false;
	bool minimized = false;
	bool sized = false;
	bool locked = false;
	bool activePrimary = false;
};

// Names (ControllerRefusalName), in decision order; the first that applies
// is the reading's refusal:
// 1. no-session: the requested session handle is empty (never bound, or
//    its session destroyed);
// 2. with an exact window requested:
//    - destroyed-window: its handle is empty (never bound, or closed);
//    - wrong-window: it is alive but not registered;
//    - wrong-account: it is registered for another account;
//    without one:
//    - no-registered-window: the account has no Primary-type registered
//      window. This is an absent controller, whatever activation history
//      says; a registered window missing from windowStack() is not refused
//      for that and reads activation=absent;
// 3. wrong-session: the window's account shows another session or none, or
//    its SessionController shows another session;
// 4. non-test: the session is not on the Test environment;
// 5. unready: no SessionController, or an empty widget size;
// 6. locked: Window::Controller::locked(), a passcode or terms lock;
// 7. hidden: the widget is not visible, or is minimized.
// Only the identity re-check of an accepted reading (ActOnController's,
// before its action) can give, after destroyed-window (its window closed):
// - replaced-session: its session was destroyed;
// - replaced-window: its SessionController was destroyed, or a fresh
//   decision accepts another window or controller.
// none means accepted; not-evaluated is the default and never accepts.
enum class ControllerRefusal {
	None,
	NotEvaluated,
	NoSession,
	DestroyedWindow,
	WrongWindow,
	WrongAccount,
	NoRegisteredWindow,
	WrongSession,
	NonTest,
	Unready,
	Locked,
	Hidden,
	ReplacedWindow,
	ReplacedSession,
};

[[nodiscard]] QString ControllerRefusalName(ControllerRefusal refusal);

// One decision. |entry| is the registered window the decision selected: a
// fresh decision leaves it empty for no-session, no-registered-window,
// destroyed-window and wrong-window, and a re-checked reading that refuses
// before deciding again keeps the retained entry. |registered| counts the
// windows read, |accountWindows| the registered windows naming the
// requested account, |activationSize| is the size of windowStack(), and
// |dispatched| is set only when the action ran.
// accepted() describes the moment of the reading. controller() is
// entry.target.get() only when accepted(), else null; it is null too once
// that controller is destroyed, so a retained reading never yields a stale
// or replacement controller.
struct ControllerReading {
	ControllerRequest request;
	ControllerEntry entry;
	ControllerRefusal refusal = ControllerRefusal::NotEvaluated;
	int registered = 0;
	int accountWindows = 0;
	int activationSize = 0;
	bool dispatched = false;

	[[nodiscard]] bool accepted() const;
	[[nodiscard]] Window::SessionController *controller() const;
};

// Decides |request| over one fresh read of the registry: for every
// registered window its id(), sessionController(), locked() and widget
// visibility, minimized state and size, its account's maybeSession() and
// storage index and that session's isTestMode(), with windowStack() and
// activePrimaryWindow() copied in as facts. Pure; safe in a Stage's
// |until|.
[[nodiscard]] ControllerReading ResolveController(
	const ControllerRequest &request);

// The reading as one line, in this format (wrapped here):
//   controller verdict=<accepted|refused> refusal=<name>
//     request=[window=<account-primary|exact-live|exact-gone>
//     session=<live|none>] registered=<n> accountWindows=<n>
//     activationSize=<n> entry=[none|index=<i|none>
//     type=<Primary|Archive|Chat|Forum|Community|SavedSublist|SharedMedia>
//     test=<0|1> session=<same|other|none> controller=<present|none>
//     shown=<same|other|none> visible=<0|1> minimized=<0|1> sized=<0|1>
//     locked=<0|1> activation=<rank|absent> activePrimary=<0|1>]
//     dispatched=<0|1>
// session= and shown= compare with the requested session; window=,
// session= and every handle-derived entry field read liveness when the
// text is made. It prints storage indices and counts only, never a user
// id, uniqueId(), name, peer or address.
[[nodiscard]] QString ControllerReadingText(const ControllerReading &reading);

// Appends one stage, "act on controller: <name>".
// - Its |until| builds |request|() and calls ResolveController on every
//   poll. Every refusal is "not ready yet" (as Runner::actOnWidget treats
//   an unresolved widget); the stage's timeoutDetails is the last reading's
//   text, so a timeout names the refusal. |request| must only read.
// - Its |then| runs in the same runner tick as the accepting poll
//   (Runner::completeStage), so the re-check there is an identity guard,
//   not a second wait: against a fresh registry read it requires the same
//   live window, controller and session, then calls |action| exactly once
//   with that controller and writes
//     NOTE: CONTROLLER_READY: act=[<name>] <text>
//   Otherwise it writes one
//     TEST_RESULT: FAIL: act on controller: <name> - accepted controller
//       changed before the action: <text>
//   and does not call |action|.
// - Inside Runner::addGated the stage inherits the group's gate.
// - It opens, activates and switches nothing: a caller opens or activates
//   a window in an earlier stage, under the session gate's rule that no
//   stage switches the primary window or domain().active() to another
//   account.
void ActOnController(
	not_null<Runner*> runner,
	const QString &name,
	Fn<ControllerRequest()> request,
	Fn<void(not_null<Window::SessionController*>)> action,
	crl::time timeout = kDefaultStageTimeout);

// This helper measuring itself.
// - Fixture legs, needing no session, feed caller-controlled registries of
//   local identity objects through the same decision the real reader
//   feeds: two Test controllers of which activation history holds only one
//   are both accepted, an account without a registered window refuses as
//   no-registered-window, each refusal is checked beside its accepted
//   counterpart, and a retained reading never becomes valid for a
//   replacement controller, session or window.
// - Live legs open an Archive window of the primary window's Test session
//   (the product's "Archive -> New window" route), resolve both windows by
//   registry, check that polls change neither the registry nor activation
//   history, act once per accepted target through ActOnController, refuse
//   wrong-account, wrong-session, wrong-window and absent targets without
//   acting, then close the Archive window and check that its retained
//   reading, and a request for it, refuse as destroyed-window.
// - Prerequisites: an unlocked Test session in the primary window, and
//   launch_finished awaited by the overlay. Without that session it writes
//   one fixture-gate FAIL and the live stages read N/A; an Archive window
//   that already existed makes the close leg N/A.
// - Side effects: it opens and closes that Archive window, with the OS
//   activation request, presence update and local saved-window bookkeeping
//   the product does for it; it sends no request of its own.
// Expected verdict on a copy with that session: no FAIL and no N/A.
void AppendControllerReadySelfTest(not_null<Runner*> runner);

} // namespace Test
