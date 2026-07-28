/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace Test {

inline constexpr auto kDefaultStageTimeout = crl::time(10000);
inline constexpr auto kStartupStageTimeout = crl::time(30000);

// One scenario step. Runs |run| once, polls |until| on the event loop till it
// returns true (immediately ready when null), then runs |then| assertions.
// A stage past its |timeout| fails the scenario and finishes early — the
// scenario always ends in TEST_COMPLETE and quit, never a hang.
struct Stage {
	QString name;
	Fn<void()> run;
	Fn<bool()> until;
	Fn<void()> then;
	crl::time timeout = kDefaultStageTimeout;
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
