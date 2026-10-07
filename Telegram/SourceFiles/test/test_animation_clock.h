/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "ui/click_handler.h"

#include <crl/crl_time.h>

#include <QtCore/QString>

#include <vector>

namespace Test {

class Runner;

// Captures a short Ui::Animations-driven product animation at chosen
// elapsed times after one synchronous action, instead of at whatever runner
// tick comes next.
//
// Why it exists. The overlay of
// 2026/10/06/round-rich-message-edge-media-like-ordinary-media measured 200
// ms slide transitions and a spoiler reveal, and sampling them by wall clock
// failed in three runs (that task's work/test.md):
// - Run 2: 12 premise rows failed because every transition, and the reveal,
//   was sampled at the next 50 ms runner tick and had already finished: the
//   capture showed only the destination slide or the revealed photo. The
//   test window was minimized, so there were no compositor paints to wait
//   for either.
// - Run 3 clocked them: every clocked transition row was decided, but the
//   three reveal premises read "media centre #9f116f" (still hidden) at all
//   three ticks, 41, 100 and 160 ms after the click. The click went through
//   ActivateClickHandler() (lib_ui/ui/click_handler.cpp), which does not
//   call onClick() but posts it with crl::on_main, and Test::Settle drains
//   only Ui::PostponeCall, so the handler ran only after the synchronous
//   stage returned to the event loop - after every frame.
// - Run 4 invoked the handler directly (link->onClick(context) inside
//   Test::Settle) and decided the in-progress frames, but each frame ran a
//   ~140 ms pair capture between ticks, so its ticks landed at 40, 181 and
//   324 ms instead of 40, 100 and 160, and the last one was past the reveal.
//   Its tick past the end was reported as a FAIL where it was a gate.
// That code died with the overlay; this is its generic part, kept, plus a
// proof that each tick ran and an N/A gate past the declared end.
//
// The mechanism, in RunClockedFrames' order.
// 1. The request is validated: a non-empty |name|, exactly one of |action|
//    and |handler|, a |render|, and |elapsed| non-empty, non-negative and
//    strictly increasing. A refused request is one row,
//      TEST_RESULT: FAIL: fixture gate: <name>: clocked frames refused
//        - <why>
//    and nothing runs.
// 2. A probe Ui::Animations::Basic local to the call is started through
//    Basic::start(). Its callback counts its calls, keeps the |now| it
//    receives and asks to continue. Manager::update() returns without a tick
//    while its active list is empty, while _updating, or while _scheduled;
//    the probe keeps the list non-empty, and update() evaluates every
//    active animation at one |now|, so a probe call proves a tick and its
//    |now| is the time every animation was evaluated at. The probe's
//    destructor stops it on every return path.
// 3. The action runs inside Test::Settle, with crl::now() stamped right
//    before the action and right after its postponed calls were drained
//    (|actionStarted|, |actionFinished|). A |handler| is invoked directly,
//    handler->onClick(context), never through ActivateClickHandler(): inside
//    a synchronous stage the call that one posts runs only after the stage
//    returns, after every frame.
// 4. For each requested elapsed time, in order:
//    - a requested time at or after a declared |animationEnd| (> 0) is a
//      gate: one N/A row, no wait, no tick, no render;
//    - the main thread sleeps in 1 ms steps until actionStarted + requested;
//    - Test::SettlePostponedCalls(): Manager::schedule() sets _scheduled and
//      posts its callback with Ui::PostponeCall, and the drain runs it,
//      clearing _scheduled. Read back: once that callback called
//      updateQueued(), _timerId is -1 and schedule() returns at once, so
//      later ticks in the same synchronous stage need no drain; when it
//      started a timer instead, the next update() sets _scheduled again. The
//      drain before every tick covers both;
//    - Core::App().animationManager().update(), once - the call the
//      manager's own timer makes, and the only Manager call the helper
//      makes;
//    - a tick that did not call the probe is refused, manager-did-not-tick:
//      a schedule callback still pending (under
//      Manager::SetScheduleWithInvokeQueued(true) it is an InvokeQueued call
//      the drain does not run) or an update in progress (_updating);
//    - the reached bounds (below); a frame whose upper bound is at or after
//      the declared end is the same N/A gate, not rendered;
//    - |render| runs with the probe counted around it; a render that called
//      the probe advanced the animations past the bounds and is refused,
//      render-advanced-the-manager.
//    A refused frame is not taken and the loop goes on with the next time.
//
// The reached bounds. A Basic started inside the action, or in a postponed
// call it posted that the drain ran, joins the manager's active list at
// once, its start time crl::now() taken there (Manager::start(); one started
// during an update in progress joins at the end of that update instead,
// which a stage cannot be inside). So at a tick whose |now| is T the
// subject's elapsed time lies in [T - actionFinished, T - actionStarted],
// all of one crl::now() clock; evaluate the transition at both bounds. The helper never reads the
// subject: whether its animation started, and what it painted, is the
// caller's pixel oracle. The probe proves only the tick.
//
// The render. Synchronous only: Test::GrabWidget / GrabRect, which are
// QWidget::render. Never repaint(), whose QEvent::UpdateRequest can tick the
// manager through crl::on_main_update_requests(), and never processEvents().
// Keep it cheap - Run 4's per-frame capture moved its ticks - and save
// images after the run. Because the render is QWidget::render and no paint
// is awaited, it works the same in a minimized window or on a hidden
// widget.
//
// For the caller:
// - anim::Disabled() makes the first tick jump to the end: gate it;
// - anim::SlowMultiplier() scales Ui::Animations::Simple durations: scale
//   the elapsed times and the end with it;
// - a streamed video player is not driven by the manager:
//   Media::Streaming::Player paces frames with its own base::Timer, and an
//   inline one also waits for a paint of its view to mark a frame shown, so
//   a clocked stage does not advance it;
// - unsupported: any thread but the main one, a nested call, an action or a
//   render that destroys what |render| reads.

// One requested time. |index| is 0-based; every row prints it as the
// 1-based ordinal, index + 1. |tick|, |reachedLo| and |reachedHi| are -1
// until a tick was proven. |waited| is the time slept before the tick;
// |probeTicks| counts the probe calls the tick made (1 when proven) and
// |renderTicks| those the render made (0 unless refused). |gate| is the N/A
// reason and |refusal| the named refusal; at most one is set.
struct ClockedFrame {
	int index = 0;
	crl::time requested = 0;
	crl::time tick = -1;
	crl::time reachedLo = -1;
	crl::time reachedHi = -1;
	crl::time waited = 0;
	int probeTicks = 0;
	int renderTicks = 0;
	QString gate;
	QString refusal;

	// Ticked, rendered, neither gated nor refused.
	[[nodiscard]] bool taken() const;
};

// |action| or |handler| (with |context|), exactly one. |elapsed| in ms after
// the action. |animationEnd| <= 0 declares no end. |render| receives the
// frame with its tick and reached bounds filled in.
struct ClockedRequest {
	QString name;
	Fn<void()> action;
	ClickHandlerPtr handler;
	ClickContext context;
	std::vector<crl::time> elapsed;
	crl::time animationEnd = 0;
	Fn<void(const ClockedFrame &frame)> render;
};

// The action window (-1 when the request was refused), the declared end,
// one frame per requested time in request order, and the request refusal.
struct ClockedRun {
	crl::time actionStarted = -1;
	crl::time actionFinished = -1;
	crl::time animationEnd = 0;
	std::vector<ClockedFrame> frames;
	QString refusal;

	// The number of taken frames.
	[[nodiscard]] int taken() const;
};

// Runs the request on the calling stage and writes its rows: each taken
// frame is one row,
//   NOTE: <name>: kind=frame index=<k> requested=<ms> reached=[<lo>,<hi>]
//     tick=<ms> waited=<ms> probeTicks=<n> renderTicks=<n>
// (one physical line, ClockedFrameText); a gated frame is
//   TEST_RESULT: N/A: <name>: frame <k> at <ms> ms - <reason>
// and a refused one
//   TEST_RESULT: FAIL: fixture gate: <name>: frame <k> at <ms> ms
//     - <refusal>
// whose refusal starts with manager-did-not-tick or
// render-advanced-the-manager.
[[nodiscard]] ClockedRun RunClockedFrames(const ClockedRequest &request);

// The one frame formatter: "kind=frame index=<k> requested=<ms>
// reached=[<lo>,<hi>] tick=<ms> waited=<ms> probeTicks=<n> renderTicks=<n>",
// "none" for a bound or tick that was not read, then " gate=<reason>" or
// " refused=<refusal>" when set. The helper's NOTE rows print through it,
// and so should a caller's details.
[[nodiscard]] QString ClockedFrameText(const ClockedFrame &frame);

// RunClockedFrames measuring itself on a harness-owned synthetic widget. It
// needs only the primary window, as the parent of a bar widget that is never
// shown: no session, chats list or account fixture. A real
// Ui::Animations::Simple drives a 200 ms anim::linear value (scaled by
// anim::SlowMultiplier()) from 0 to 1, which the widget paints as a #2060d0
// bar of that share of its width over a #ffffff track; a LambdaClickHandler
// starts it. Two stages, eight Test::Check rows and one N/A row of the
// helper:
//
// 1. Fixture. Builds the bar, an opaque Ui::RpWidget child of the primary
//    window created after that window was shown and never show()n: it
//    stays hidden, QWidget::update() is a no-op on it, and no backing-store
//    or compositor paint can reach it, while Test::GrabWidget still renders
//    it. The primary window itself is read, never changed. Check: the
//    fixture gate - a primary window, the bar hidden and not visible, and
//    animations enabled. A failed gate makes stage 2 N/A by that name.
// 2. Clocked frames from a click handler. One synchronous run, so no
//    event-loop turn lies between the click and the readings: the handler
//    is clocked through RunClockedFrames at 25, 50, 75 and 125 % of the
//    duration with the duration as the declared end, each render a
//    Test::GrabWidget of the bar. Checks: the handler ran once inside the
//    action window; for each frame at 25, 50 and 75 % the value read from
//    the rendered pixels (bar pixels of the middle device row over its
//    width) lies within the linear transition at the frame's reached bounds,
//    within 1.5 device pixels; the three values strictly increase; the
//    fourth time is the helper's N/A row, with no frame image; the bar was
//    hidden and not visible before and after, nothing painted it outside
//    the renders, and the renders painted it once per taken frame, three.
//    The window reading - the bar's hidden and visible, the window's
//    minimized, hidden, exposed, screenLocked and identity - is printed
//    beside the frames. The three frames are saved as
//    animation_clock_frames, then the bar is released.
//
// Without a runner finish callback (the dev Runner has none): the Simple and
// its counters live in the bar's own lifetime(), so they die with the bar -
// at the end of stage 2, or with the primary window in Core::Application's
// destructor, before the animation manager; the Runner, a function static,
// destroys the stages later still. The stages keep only the bar's owning pointer, the
// handler (whose lambda checks a QPointer to the bar first), plain values
// and strings.
//
// It emits no deliberate FAIL. Its negative legs are two disposable
// mutations of the helper: with the click routed through
// ActivateClickHandler() the handler has not run when stage 2 reads it, and
// the handler and frame rows fail; with the manager tick deleted every
// frame is refused manager-did-not-tick, and the frame rows and the paint
// row fail.
void AppendAnimationClockSelfTest(not_null<Runner*> runner);

} // namespace Test
