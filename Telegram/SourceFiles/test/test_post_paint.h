/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/weak_ptr.h"
#include "test/test_log.h"

#include <QtCore/QPointer>
#include <QtCore/QString>

#include <vector>

class QWidget;

namespace Test {

class Runner;

// One sample PostPaintSampler took. |seq| is 1-based and is the key a
// caller's images use: the action receives it, so an image is related to
// its row by identity, never by position. |paintAt| is the receipt of the
// first product paint the sample covers and |lastPaintAt| that of the last
// one; |covered| counts the product paints it covers and is at least 1.
// |lo| and |hi| are crl::now() right before and right after the action,
// taken by the sampler itself, so every row's brackets are taken the same
// way.
struct PostPaintSample {
	int seq = 0;
	crl::time paintAt = 0;
	crl::time lastPaintAt = 0;
	int covered = 0;
	crl::time lo = 0;
	crl::time hi = 0;
};

// The one row formatter:
// "kind=post seq=<n> paint=<ms> last=<ms> covered=<k> lo=<ms> hi=<ms>".
// The sampler's own NOTE rows print through it, and so should a caller's
// details, so one sample reads the same everywhere in a log. This form
// prints in the format the run asked for (HelperNumberFormat()): ordinary
// integers, or TelemetryNumbers after RequestTelemetryNumbers().
[[nodiscard]] QString PostPaintSampleText(const PostPaintSample &sample);

// The same row with its numbers in |format|, so a self-test reads both
// formats.
[[nodiscard]] QString PostPaintSampleText(
	const PostPaintSample &sample,
	NumberFormat format);

// What the stop row prints: the stop reason, the last sample's |seq| (0
// before any), whether a sample was pending at the stop, and the sampler's
// counters.
struct PostPaintStop {
	QString reason;
	int seq = 0;
	bool pending = false;
	int samples = 0;
	int paints = 0;
	int ignored = 0;
	int dropped = 0;
};

// The stop row after "NOTE: ", "<name>: kind=stop reason=<reason> seq=<n>
// pending=<0|1> samples=<n> paints=<n> ignored=<n> dropped=<n>" (one line),
// with its numbers in |format|; the sampler's stop passes
// HelperNumberFormat(). It takes |name| because the row has always
// substituted the name inside this same .arg chain, before the numbers, so
// a name holding a %<n> marker keeps expanding exactly as it did.
[[nodiscard]] QString PostPaintStopText(
	const QString &name,
	const PostPaintStop &stop,
	NumberFormat format);

// Samples one painted owner right after each of its product paints instead
// of on a timer.
//
// Why it exists. A timer-driven sampler of an animating surface loses the
// decisive frames in Debug when the owner's product paints run back to
// back. Attempt 1 Run 1 of
// 2026/10/05/play-gram-card-read-animations-one-after-another sampled a
// chat list with one-shot 16 ms base::Timer grabs. While two Gram cards
// animated, three product paints ran back to back (frames 44159, 44173 and
// 44193, about 16-20 ms each in Debug), the sampler had a 52 ms gap from
// grab 25 (hi 44159) to grab 26 (lo 44211), and the product's own precise
// timer reported "arrive due=44159 late=52" in the same stretch. The
// visible-start bound vstart(B2).hi - turn <= 50 spanned that gap and read
// 75, so the decisive check was undecided and the run was a TEST_FLAW. The
// recovery hand-wrote a post-paint sampler in the disposable overlay: in
// Run 2 the same check's bracket followed the previous sample by 20 ms (its
// G-sample gate, bound 34 ms), with 9 post-paint grabs in
// [turn - 100, turn + 300], and the bound read 46, decided. The overlay
// died with that task; this is its generic part, kept. Whether back-to-back
// product paints delay a 16 ms base::Timer is that run's reading, not a
// measured cause - AppendPostPaintSamplerSelfTest's timer-only control
// measures it on this host.
//
// The mechanism.
//
// The constructor installs an event filter on |owner|
// (base::install_event_filter, the two-argument form: the filter is a
// child of the owner and dies with it). An object event filter sees each
// QEvent::Paint before paintEvent runs - QApplicationPrivate::notify_helper
// calls the object filters before receiver->event().
//
// A product paint received while some sampling window is open and no
// grab-free window applies posts at most one sample: the first such paint
// sets the pending sample and posts one call through InvokeQueued on
// QCoreApplication::instance(). The paint is synchronous inside
// QWidgetPrivate::drawWidget and a posted call is dispatched only by the
// event loop, never in the posted-events pass that posted it, so the sample
// runs after that paint returned. A paint received while a sample is
// pending shares it: |covered| counts it and |lastPaintAt| is its receipt.
// For a paint the owner requests from its own paintEvent the next paint
// cannot come first - update() inside a paint posts a QUpdateLaterEvent
// after the sample, and posted events of one priority run in order - so
// such an owner yields covered == 1; a paint requested elsewhere (an
// animation tick calling update() outside paint) may be delivered before
// the posted sample, and then shares it (covered >= 2).
//
// The recursion guard. The sampler brackets the action itself: lo, then
// action(seq), then hi, with a flag set around the action. A grab renders
// the owner synchronously inside the action (Test::GrabRect ->
// QWidget::render -> drawWidget -> sendPaintEvent), so every paint the
// action causes arrives while the flag is set and is only counted as
// ignored: a sample never posts another sample. An update() the owner makes
// inside such a grab paint only posts a QUpdateLaterEvent, and the paint it
// produces later is a genuine product paint and is sampled - an owner that
// requests a repaint from every paint keeps being sampled because it keeps
// painting, never because the sampler feeds itself.
//
// The windows, in crl::now() milliseconds, each [from, till):
// - sampleDuring opens a sampling window. Outside every sampling window
//   the sampler is idle, so there is no start() or stop(). Window
//   membership is decided at receipt only: a paint received inside a window
//   is followed even when the window ends while its sample is queued.
// - grabFreeDuring declares a window in which no sample may run. A grab
//   started just before it could still be painting inside it, so a paint
//   counts as inside from max(40 ms, the longest action so far) before
//   |from|. A paint received there joins no pending sample and posts
//   nothing, and a sample posted earlier that would run there is dropped
//   without a row. Sampling resumes with the first product paint received
//   at or after |till|.
// - An empty or inverted window is ignored with a NOTE naming it.
// There is no timer fallback: nothing is ever sampled without a product
// paint. A frame of an idle surface is an ordinary grab taken outside the
// sampling windows.
//
// The owner's lifetime. The owner is held through a QPointer and never
// kept alive. ~QWidget emits destroyed before it deletes its children, so
// the sampler hears it through a connection whose context is the filter,
// stops, takes the pending sample (printed as pending=1) and logs the stop
// row; a queued sample that runs afterwards finds nothing pending and takes
// no grab. Destroying the sampler deletes the filter, whose callback
// captures the sampler, and logs nothing - it may run during teardown, so
// read the rows before releasing it. A queued sample that outlives the
// sampler finds its weak pointer null and does nothing.
//
// The rows. Each sample is one row and one log line,
//   NOTE: <name>: kind=post seq=<n> paint=<first receipt ms>
//     last=<last receipt ms> covered=<k> lo=<ms> hi=<ms>
// (one physical line; the payload is PostPaintSampleText), and the stop is
//   NOTE: <name>: kind=stop reason=owner-destroyed seq=<last seq>
//     pending=<0|1> samples=<n> paints=<n> ignored=<n> dropped=<n>
// (the payload is PostPaintStopText), where |paints| counts the product
// paints received, |ignored| the paints the actions caused and |dropped|
// the samples a grab-free window dropped.
// owner-destroyed is the only stop reason: a window ending is not a stop,
// and the sampler stays usable. Rows are read the way Test::Probe's are:
// take mark() immediately before the action under test and pass it to
// samplesSince(mark), which returns the rows with seq > mark. There is
// deliberately no accessor over the whole history. details() prints the
// owner, pending, stop reason, counters, window counts and the longest
// action, for a Check's details on either verdict.
//
// The numbers of the two rows are ordinary integers until
// Test::RequestTelemetryNumbers(); a row formatted after it prints every
// one of them as a TelemetryNumber (seq=4821p ... hi=61537p, pending=1p
// ...). details() and the empty-window NOTE keep ordinary integers either
// way.
//
// The instrument floor. A post-paint sample's lo can never precede the end
// of the paint it follows: in Attempt 2 of the same task the post-paint
// sampler could only grab after the Debug paint that started it
// (10340..10371, 31 ms), and D4's bound read 57 and was recorded
// undecided. Both brackets of a sample lie after the whole paint it
// follows, so a bound from that paint's start shorter than the paint plus
// one action cannot be decided by this helper.
//
// Unsupported, by contract:
// - any thread but the main one;
// - an action that destroys the owner or the sampler (the sampler touches
//   nothing of itself after such an action, but it is not supported);
// - any other grab of the owner or of an ancestor taken while a sampling
//   window is open: its paint looks like a product paint, so route it
//   through the action or take it outside the windows;
// - an owner that is not the widget whose own paintEvent paints the
//   measured surface: the filter sees the owner's paints only, never a
//   child's.
class PostPaintSampler final : public base::has_weak_ptr {
public:
	PostPaintSampler(
		QString name,
		not_null<QWidget*> owner,
		Fn<void(int seq)> sample);
	~PostPaintSampler();

	void sampleDuring(crl::time from, crl::time till);
	void grabFreeDuring(crl::time from, crl::time till);

	// The end of the row history right now; take it immediately before the
	// action under test.
	[[nodiscard]] int mark() const;
	[[nodiscard]] std::vector<PostPaintSample> samplesSince(int mark) const;

	// True while a posted sample has not run yet.
	[[nodiscard]] bool pending() const;

	// Empty while live, "owner-destroyed" once the owner is gone.
	[[nodiscard]] QString stopReason() const;
	[[nodiscard]] QString details() const;

private:
	struct Window {
		crl::time from = 0;
		crl::time till = 0;
	};

	void paintReceived();
	void post();
	void runPending();
	void finish(const QString &reason);

	[[nodiscard]] bool sampling(crl::time now) const;
	[[nodiscard]] bool grabFree(crl::time now) const;

	const QString _name;
	const QPointer<QWidget> _owner;
	const Fn<void(int)> _sample;
	QPointer<QObject> _filter;
	std::vector<Window> _windows;
	std::vector<Window> _grabFree;
	std::vector<PostPaintSample> _samples;
	QString _stopReason;
	crl::time _pendingFirstAt = 0;
	crl::time _pendingLastAt = 0;
	crl::time _longest = 0;
	int _pendingCovered = 0;
	int _paints = 0;
	int _ignored = 0;
	int _dropped = 0;
	bool _pending = false;
	bool _sampling = false;

};

// PostPaintSampler measuring itself on a harness-owned synthetic widget. It
// needs only a shown, non-minimized primary window: no session, no chats
// list and no account fixture. Its fixture stage stacks that window above
// other applications' windows with Test::KeepWindowExposed, which activates
// nothing, so a window another application covers - the usual state after a
// default workspace.py test-run launch, which does not activate the client -
// exposes and paints; the hint is cleared at its last stage, on a failed
// gate, or in Runner::onFinish. The widget is an opaque Ui::RpWidget
// parented to that window at (0, 0) and raised, so its product paints go
// through the same backing-store sync as the surfaces the helper is for.
// Each paint busy-waits 18 ms (the source's 16-20 ms Debug paints) and
// appends one record stamped by the paint itself, with a grab flag the
// test's own action sets around its GrabRect - so every oracle below reads
// the widget's own records and the sampler's rows, never the helper's
// counters alone. Nine stages, ten Test::Check rows:
//
// 1. Fixture and warm-up. Builds the widget and the sampler, with no
//    sampling window open, keeps the primary window exposed with
//    Test::KeepWindowExposed, and repaints continuously (each product paint
//    requests the next from inside paintEvent). Check: the fixture gate -
//    at least 5 product paints in a shown, exposed, non-minimized primary
//    window, with the helper's reading in the details (its exposed= is the
//    value from before the call, where a window covered since it was shown
//    can read exposed=1). A minimized or hidden window produces no
//    paints and the helper refuses it; the gate then fails once with what
//    it read and the refusal, clears the hint, and every later stage is N/A
//    by that name.
// 2. Timer-only control at a 16 ms cadence. One-shot base::Timer grabs of
//    the same widget for 1500 ms, still with no sampling window open, so
//    neither sampler's grabs fall into the other's readings. No check: a
//    NOTE "control{...}" prints its largest gap, missed paints, lateness,
//    the paints started while a tick was overdue and the lateness of ticks
//    with no paint inside, and stage 3 repeats it in its details.
// 3. Stressed. A 1500 ms sampling window over the continuous widget.
//    Checks: the widget repainted back to back (at least 20 product paints
//    in the window); every product paint is covered by a sample taken
//    after it returned, within one product paint plus one grab read from
//    the same rows, with the helper's own gap and misses beside the
//    control's.
// 4. Recursion guard: settle. Repainting stops; waits until no paint ended
//    for 100 ms. No check.
// 5. Recursion guard: one product paint, then an idle window. A 1200 ms
//    window and one update(). Checks: exactly one product paint, exactly
//    one sample covering it and taken after it returned; no further sample
//    through the window's drain over an idle window of at least 500 ms,
//    with at least one grab paint recorded - the control that the guard
//    had a paint to ignore. A second product paint is named as a stray
//    paint in the details instead of being blamed on the helper.
// 6. Grab-free window. Continuous repainting in a 1200 ms sampling window
//    containing the grab-free window [from + 400, from + 700). Checks: at
//    least 3 product paints inside it, no sample overlapping it and at
//    least one sample before it (sampling was live); the first sample
//    taken after it (lo at or after its end) follows a product paint
//    received at or after its end, matched within 1 ms, with no
//    later-received paint sampled first, taken after that paint returned
//    and within one product paint plus one grab.
// 7. Owner destroyed: settle, as stage 4.
// 8. Owner destroyed with a sample pending. A 1000 ms window, the stage's
//    own paint filter installed after the sampler's, so Qt calls it first,
//    and one update(). On the first product paint that filter posts the
//    destroy call D before the sampler's filter posts its sample S; posted
//    calls of one priority run in order, so D reads the premise and
//    destroys the widget while S is still queued - deterministic for any
//    paint path, without a synchronous repaint(), which the main window's
//    texture child can turn into a deferred one. Checks: the premise - D
//    was posted, one product paint, a sample pending and no row yet; then
//    no action invocation (counted before the action's own null check) and
//    no row after the destruction, stop reason owner-destroyed, nothing
//    pending. The helper's NOTE "kind=stop reason=owner-destroyed ...
//    pending=1" is the log row of that stop.
// 9. The stage after the owner was destroyed runs. Releases the stopped
//    sampler, whose filter already died with the owner, and clears the
//    hint stage 1 set. Check: it ran, the stop reason is owner-destroyed,
//    and the sampler and the widget are released.
//
// Runner::onFinish clears the hint, cancels the control timer, then
// releases the sampler before the widget, on every path that reaches it;
// after stage 9 it finds all of them already released or cleared.
//
// It emits no deliberate FAIL: every row is expected to PASS with the
// primary window shown and not minimized, which stage 1 keeps exposed. Its
// negative legs are two disposable mutations of the helper, never a stage
// that fails on purpose: with the recursion guard in paintReceived deleted
// each grab paint posts the next sample, and with the post-paint trigger
// replaced by a 16 ms timer that runs the sample, samples are taken without
// a product paint - either way both stage 5 checks fail.
void AppendPostPaintSamplerSelfTest(not_null<Runner*> runner);

} // namespace Test
