/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_runner.h"

#include "test/test_agent.h"
#include "test/test_log.h"
#include "core/application.h"
#include "data/data_session.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QTimer>

namespace Test {
namespace {

constexpr auto kTickInterval = crl::time(50);
constexpr auto kDefaultWatchdogSeconds = 120;
constexpr auto kAbortAfterQuitSeconds = 10;

[[nodiscard]] crl::time WatchdogTimeout() {
	const auto value = qEnvironmentVariable("TDESKTOP_TEST_WATCHDOG");
	auto ok = false;
	const auto seconds = value.toInt(&ok);
	return crl::time(1000) * ((ok && seconds > 0)
		? seconds
		: kDefaultWatchdogSeconds);
}

[[nodiscard]] bool SessionReady() {
	const auto &domain = Core::App().domain();
	return domain.started() && domain.active().sessionExists();
}

} // namespace

void Runner::add(Stage stage) {
	Expects(!_started);

	_stages.push_back(std::move(stage));
}

void Runner::waitEvent(const QString &event, crl::time timeout) {
	add({
		.name = u"wait for event: %1"_q.arg(event),
		.until = [=] { return HasFired(event); },
		.timeout = timeout,
	});
}

void Runner::waitForSessionReady(crl::time timeout) {
	add({
		.name = u"wait for session ready"_q,
		.until = SessionReady,
		.timeout = timeout,
	});
}

void Runner::waitForChatsLoaded(crl::time timeout) {
	add({
		.name = u"wait for chats loaded"_q,
		.until = [] {
			return SessionReady()
				&& Core::App().domain().active().session().data(
					).chatsListLoaded();
		},
		.timeout = timeout,
	});
}

bool Runner::empty() const {
	return _stages.empty();
}

void Runner::start() {
	Expects(!_started && !_stages.empty());

	_started = true;
	LogRaw(u"SCENARIO_START: %1 stage(s)"_q.arg(_stages.size()));
	_watchdog.setCallback([=] {
		Fail(u"scenario watchdog"_q, u"hard wall-clock cap reached"_q);
		finish();
	});
	_watchdog.callOnce(WatchdogTimeout());
	beginStage();
	_ticker.setCallback([=] { tick(); });
	_ticker.callEach(kTickInterval);
}

void Runner::tick() {
	if (_finished) {
		return;
	}
	const auto &stage = _stages[_index];
	if (!stage.until || stage.until()) {
		completeStage();
	} else if (crl::now() - _stageStarted > stage.timeout) {
		Fail(
			u"stage timed out: %1"_q.arg(stage.name),
			u"waited %1 ms"_q.arg(stage.timeout));
		finish();
	}
}

void Runner::beginStage() {
	const auto &stage = _stages[_index];
	Step(stage.name);
	_stageStarted = crl::now();
	if (stage.run) {
		stage.run();
	}
}

void Runner::completeStage() {
	const auto &stage = _stages[_index];
	if (stage.then) {
		stage.then();
	}
	if (++_index == int(_stages.size())) {
		finish();
	} else {
		beginStage();
	}
}

void Runner::finish() {
	if (_finished) {
		return;
	}
	_finished = true;
	_ticker.cancel();
	_watchdog.cancel();
	const auto failures = FailureCount();
	LogRaw(u"SCENARIO_RESULT: %1 (failures: %2)"_q.arg(
		failures ? u"FAIL"_q : u"PASS"_q,
		QString::number(failures)));
	Complete();
	QTimer::singleShot(kAbortAfterQuitSeconds * 1000, [] {
		std::abort();
	});
	Core::Quit();
}

void Start() {
	if (!Active()) {
		return;
	}
	static auto Started = false;
	if (Started) {
		return;
	}
	Started = true;
	static auto runner = Runner();
	SetupScenario(&runner);
	if (runner.empty()) {
		Note(u"no scenario registered"_q);
		return;
	}
	const auto marker = cWorkingDir() + u"testing"_q;
	if (!QFile::exists(marker)) {
		LogRaw(u"SCENARIO_REFUSED: missing disposable-copy marker %1"_q.arg(
			marker));
		return;
	}
	runner.start();
}

} // namespace Test
