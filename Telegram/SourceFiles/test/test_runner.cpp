/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_runner.h"

#include "test/test_agent.h"
#include "test/test_log.h"
#include "base/call_delayed.h"
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

// The fuse records a blocked launch inside the fused
// Platform::File::Unsafe* wrapper, which a click reaches only across at
// least two queued main-thread hops: Ui::ActivateClickHandler's
// crl::on_main, then Core::File::Launch's crl::on_main or
// Core::File::OpenWith's InvokeQueued. So finish() must not read
// FailureCount() in the turn that completes the last stage, or a run that
// did reach the launcher reports PASS. The window bounds the hand-offs
// already queued by then; it stands in for no observable condition,
// main-queue quiescence having none.
constexpr auto kFinishDrainDelay = crl::time(500);

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

[[nodiscard]] bool ChatsLoaded() {
	return SessionReady()
		&& Core::App().domain().active().session().data().chatsListLoaded();
}

enum class ChatsLoadedWaitOutcome {
	Pending,
	Loaded,
	TimedOut,
};

struct ChatsLoadedWaitState {
	ChatsLoadedWaitOutcome outcome = ChatsLoadedWaitOutcome::Pending;
	crl::time deadline = 0;
	base::Timer deadlineTimer;
	rpl::lifetime loadedLifetime;
};

void ResolveChatsLoadedWait(
		const std::shared_ptr<ChatsLoadedWaitState> &state,
		ChatsLoadedWaitOutcome outcome) {
	if (state->outcome != ChatsLoadedWaitOutcome::Pending) {
		return;
	}
	state->outcome = outcome;
	state->deadlineTimer.cancel();
	state->loadedLifetime.destroy();
}

void ResolveChatsLoadedWaitAt(
		const std::shared_ptr<ChatsLoadedWaitState> &state,
		crl::time observedAt) {
	ResolveChatsLoadedWait(
		state,
		(observedAt < state->deadline)
			? ChatsLoadedWaitOutcome::Loaded
			: ChatsLoadedWaitOutcome::TimedOut);
}

void ArmChatsLoadedDeadline(
		const std::shared_ptr<ChatsLoadedWaitState> &state) {
	const auto now = crl::now();
	if (now >= state->deadline) {
		ResolveChatsLoadedWait(
			state,
			ChatsLoadedWaitOutcome::TimedOut);
	} else {
		state->deadlineTimer.callOnce(state->deadline - now);
	}
}

void StartChatsLoadedWait(
		const std::shared_ptr<ChatsLoadedWaitState> &state,
		crl::time stageStarted,
		crl::time timeout) {
	state->deadline = stageStarted + timeout;
	const auto weak = std::weak_ptr<ChatsLoadedWaitState>(state);
	state->deadlineTimer.setCallback([weak] {
		if (const auto state = weak.lock()) {
			ArmChatsLoadedDeadline(state);
		}
	});
	if (ChatsLoaded()) {
		ResolveChatsLoadedWaitAt(state, crl::now());
		return;
	}
	Core::App().domain().activeSessionValue(
	) | rpl::map([](Main::Session *session) {
		if (!session) {
			return rpl::never<Data::Folder*>();
		}
		return session->data().chatsListLoaded()
			? rpl::single<Data::Folder*>(nullptr)
			: session->data().chatsListLoadedEvents();
	}) | rpl::flatten_latest(
	) | rpl::filter([](Data::Folder *folder) {
		return !folder;
	}) | rpl::on_next([weak] {
		if (const auto state = weak.lock()) {
			ResolveChatsLoadedWaitAt(state, crl::now());
		}
	}, state->loadedLifetime);
	ArmChatsLoadedDeadline(state);
}

void ObserveChatsLoadedDeadline(
		const std::shared_ptr<ChatsLoadedWaitState> &state) {
	if (state->outcome == ChatsLoadedWaitOutcome::Pending
		&& crl::now() >= state->deadline) {
		ResolveChatsLoadedWait(
			state,
			ChatsLoadedWaitOutcome::TimedOut);
	}
}

[[nodiscard]] bool ChatsLoadedWaitFinished(
		const std::shared_ptr<ChatsLoadedWaitState> &state) {
	ObserveChatsLoadedDeadline(state);
	return state->outcome != ChatsLoadedWaitOutcome::Pending;
}

[[nodiscard]] bool ChatsLoadedWaitSucceeded(
		const std::shared_ptr<ChatsLoadedWaitState> &state) {
	ObserveChatsLoadedDeadline(state);
	return state->outcome == ChatsLoadedWaitOutcome::Loaded;
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
	const auto state = std::make_shared<ChatsLoadedWaitState>();
	add({
		.name = u"wait for chats loaded"_q,
		.run = [=] {
			StartChatsLoadedWait(state, _stageStarted, timeout);
		},
		.until = [=] { return ChatsLoadedWaitFinished(state); },
		.then = [=] {
			Note(u"chats loaded wait: loaded=%1 elapsedMs=%2"_q.arg(
				(state->outcome == ChatsLoadedWaitOutcome::Loaded)
					? u"true"_q
					: u"false"_q,
				QString::number(crl::now() - _stageStarted)));
		},
		.timeout = timeout,
	});
}

void Runner::waitForChatsLoadedStrict(crl::time timeout) {
	const auto state = std::make_shared<ChatsLoadedWaitState>();
	add({
		.name = u"wait for chats loaded (strict)"_q,
		.run = [=] {
			StartChatsLoadedWait(state, _stageStarted, timeout);
		},
		.until = [=] { return ChatsLoadedWaitSucceeded(state); },
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
	QTimer::singleShot(kAbortAfterQuitSeconds * 1000, [] {
		std::abort();
	});
	base::call_delayed(kFinishDrainDelay, [] {
		const auto failures = FailureCount();
		LogRaw(u"SCENARIO_RESULT: %1 (failures: %2)"_q.arg(
			failures ? u"FAIL"_q : u"PASS"_q,
			QString::number(failures)));
		Complete();
		Core::Quit();
	});
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

#else // _DEBUG

#include "test/test_agent.h"

namespace Test {

void Start() {
}

} // namespace Test

#endif // _DEBUG
