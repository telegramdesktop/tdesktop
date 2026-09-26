/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class QImage;
class QWidget;

namespace Test {

inline constexpr auto kDefaultStageTimeout = crl::time(10000);
inline constexpr auto kStartupStageTimeout = crl::time(30000);

// One scenario step. Runs |run| once after prerequisites from earlier stages,
// polls the pure readiness observer |until| on the event loop till it returns
// true (immediately ready when null), then runs |then| assertions/actions.
// Expected product results belong in |then|, not |until|. A stage past its
// |timeout| fails the scenario and finishes early — the scenario always ends
// in TEST_COMPLETE and quit, never a hang. A stage whose |skipReason| returns
// a non-empty string does not apply: the runner writes TEST_RESULT: N/A with
// that reason, skips |run|, |until| and |then|, and moves to the next stage
// in the same turn, so a false gate never waits and never times out.
struct Stage {
	QString name;
	Fn<QString()> skipReason;
	Fn<void()> run;
	Fn<bool()> until;
	Fn<void()> then;
	crl::time timeout = kDefaultStageTimeout;
	Fn<QString()> timeoutDetails;
};

class Runner final {
public:
	void add(Stage stage);

	// Sugar stages over the common waits.
	void waitEvent(
		const QString &event,
		crl::time timeout = kStartupStageTimeout);
	void waitForSessionReady(crl::time timeout = kStartupStageTimeout);
	void waitForChatsLoaded(crl::time timeout = kStartupStageTimeout);
	void waitForChatsLoadedStrict(crl::time timeout = kStartupStageTimeout);

	// Polls an exact target and an optional pure readiness predicate, then
	// runs the action once against the same lifetime-guarded widget. This
	// avoids eager stage actions and resolve-again races.
	void actOnWidget(
		const QString &name,
		Fn<QWidget*()> resolve,
		Fn<void(QWidget*)> action,
		Fn<bool(QWidget*)> ready = {},
		crl::time timeout = kDefaultStageTimeout,
		Fn<QString(QWidget*)> readinessDetails = {});

	// Resolves an exact widget on every tick, waits until the harness can
	// prepare a valid painted frame and the optional task predicate agrees,
	// then saves that same frame. Use this for full boxes, layer owners, and
	// animated surfaces instead of capture-from-then timing guesses.
	void captureWidget(
		const QString &name,
		Fn<QWidget*()> resolve,
		Fn<bool(QWidget*)> ready = {},
		crl::time timeout = kDefaultStageTimeout,
		Fn<QString(QWidget*)> readinessDetails = {});

	// Saves the same accepted frame as captureWidget, then gives that exact
	// widget and image to assertions. Keep readiness limited to identity and
	// paint availability; geometry/raster expectations belong in |inspect|
	// so a mismatch is a FAIL with actual values, never a timeout.
	void captureAndInspect(
		const QString &name,
		Fn<QWidget*()> resolve,
		Fn<bool(QWidget*)> ready,
		Fn<void(QWidget*, const QImage &)> inspect,
		crl::time timeout = kDefaultStageTimeout,
		Fn<QString(QWidget*)> readinessDetails = {});

	// Release point for per-scenario timers, rpl::lifetimes, watchers, and
	// raw cross-stage pointers. finish() runs every registered callback
	// exactly once on every path that reaches it — stage timeout, watchdog,
	// skip-to-end, and normal completion — after _finished is set and the
	// ticker/watchdog are cancelled, before the post-quit fuse is armed and
	// before kFinishDrainDelay is scheduled. The drain still runs so a fused
	// file-launch can observe its fuse; these callbacks neither skip that
	// drain nor wait for it, and they run before Complete() and Core::Quit().
	// They also run when a teardown stage already ran, so they must be safe
	// to call after teardown. A registration made after finish() has already
	// run executes immediately and is never silently dropped.
	void onFinish(Fn<void()> callback);

	[[nodiscard]] bool empty() const;

	void start();

private:
	void tick();
	void beginStage();
	void completeStage();
	void finish();

	std::vector<Stage> _stages;
	int _index = 0;
	bool _started = false;
	bool _finished = false;
	crl::time _stageStarted = 0;
	base::Timer _ticker;
	base::Timer _watchdog;
	std::vector<Fn<void()>> _onFinish;

};

// Defined by test/test_scenario.cpp. The per-task test overlay replaces that
// file with a scenario built from the task's test design; the repository
// copy registers nothing.
void SetupScenario(not_null<Runner*> runner);

// The finish-release hook measuring itself. One Runner calls finish() once,
// so the overlay selects the path. Timeout and Watchdog emit a harness FAIL
// by construction. registerRelease=false is the timeout control that leaves
// the 5 ms timer ticking; do not pack that control into a scenario that must
// exit cleanly.
enum class FinishReleasePath {
	Timeout,
	Watchdog,
	Complete,
	CompleteWithTeardown,
	SkipAll,
	LateRegister,
};

void AppendFinishReleaseSelfTest(
	not_null<Runner*> runner,
	FinishReleasePath path,
	bool registerRelease = true);

} // namespace Test
