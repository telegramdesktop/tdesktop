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
// in TEST_COMPLETE and quit, never a hang.
struct Stage {
	QString name;
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

};

// Defined by test/test_scenario.cpp. The per-task test overlay replaces that
// file with a scenario built from the task's test design; the repository
// copy registers nothing.
void SetupScenario(not_null<Runner*> runner);

} // namespace Test
