/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_runner.h"

#include "test/test_agent.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "base/call_delayed.h"
#include "core/application.h"
#include "data/data_session.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QPointer>
#include <QtCore/QTimer>

#include <limits>

namespace Test {
namespace {

constexpr auto kTickInterval = crl::time(50);
constexpr auto kDefaultWatchdogSeconds = 120;
constexpr auto kMaxWatchdogSeconds = 600;
constexpr auto kAbortAfterQuit = 10 * crl::time(1000);

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

static_assert(kMaxWatchdogSeconds
	<= std::numeric_limits<int>::max() / 1000);

[[nodiscard]] crl::time WatchdogTimeout() {
	const auto value = qEnvironmentVariable("TDESKTOP_TEST_WATCHDOG");
	auto selected = kDefaultWatchdogSeconds;
	auto source = u"default"_q;
	if (!value.isEmpty()) {
		auto ok = false;
		const auto seconds = value.toInt(&ok);
		if (ok && seconds > 0 && seconds <= kMaxWatchdogSeconds) {
			selected = seconds;
			source = u"environment"_q;
		} else {
			Note(u"TDESKTOP_TEST_WATCHDOG rejected: %1"_q.arg(value));
		}
	}
	Note(u"TDESKTOP_TEST_WATCHDOG=[%1] applied: %2s source=%3"_q.arg(
		value,
		QString::number(selected),
		source));
	return crl::time(1000) * selected;
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

// A main-thread timer that came due while the thread was blocked is
// delivered on the first event-loop pass after it is free again, so this
// fuse can arrive arbitrarily late and cannot tell a slow teardown from a
// run that wedged after recording its result. Once the completion marker
// exists it therefore reports the overrun instead of aborting, and leaves
// a post-marker wedge to the parent's grace kill and to
// Core::DeadlockDetector. A run with no marker still aborts.
void ResolveQuitFuse(crl::time deadline) {
	const auto completedAt = CompletedAt();
	if (!completedAt) {
		std::abort();
	}
	const auto now = crl::now();
	LogRaw(u"TEARDOWN_SLOW: the completion marker is already written, not "
		"aborting; fuseMs=%1 overdueMs=%2 sinceCompleteMs=%3"_q
			.arg(qint64(kAbortAfterQuit))
			.arg(qint64(std::max(crl::time(0), now - deadline)))
			.arg(qint64(now - completedAt)));
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

void Runner::actOnWidget(
		const QString &name,
		Fn<QWidget*()> resolve,
		Fn<void(QWidget*)> action,
		Fn<bool(QWidget*)> ready,
		crl::time timeout,
		Fn<QString(QWidget*)> readinessDetails) {
	struct State {
		QPointer<QWidget> widget;
		QString pendingReason;
	};
	const auto state = std::make_shared<State>();
	add({
		.name = u"act on widget: %1"_q.arg(name),
		.until = [=] {
			const auto widget = resolve();
			if (!widget) {
				state->widget = nullptr;
				state->pendingReason = u"target does not exist"_q;
				return false;
			} else if (ready && !ready(widget)) {
				state->widget = nullptr;
				state->pendingReason = readinessDetails
					? readinessDetails(widget)
					: u"task readiness predicate did not pass"_q;
				return false;
			}
			state->widget = widget;
			state->pendingReason = QString();
			return true;
		},
		.then = [=] {
			if (const auto widget = state->widget.data()) {
				action(widget);
			} else {
				Fail(
					u"act on widget: %1"_q.arg(name),
					u"accepted target was destroyed before the action"_q);
			}
		},
		.timeout = timeout,
		.timeoutDetails = [=] { return state->pendingReason; },
	});
}

void Runner::captureWidget(
		const QString &name,
		Fn<QWidget*()> resolve,
		Fn<bool(QWidget*)> ready,
		crl::time timeout,
		Fn<QString(QWidget*)> readinessDetails) {
	captureAndInspect(
		name,
		std::move(resolve),
		std::move(ready),
		{},
		timeout,
		std::move(readinessDetails));
}

void Runner::captureAndInspect(
		const QString &name,
		Fn<QWidget*()> resolve,
		Fn<bool(QWidget*)> ready,
		Fn<void(QWidget*, const QImage &)> inspect,
		crl::time timeout,
		Fn<QString(QWidget*)> readinessDetails) {
	const auto capture = std::make_shared<PreparedWidgetCapture>();
	add({
		.name = u"capture painted widget: %1"_q.arg(name),
		.until = [=] {
			const auto widget = resolve();
			if (!capture->prepare(widget)) {
				return false;
			} else if (ready && !ready(widget)) {
				capture->invalidate(
					readinessDetails
						? readinessDetails(widget)
						: u"task readiness predicate did not pass"_q);
				return false;
			}
			return true;
		},
		.then = [=] {
			if (!capture->save(name) || !inspect) {
				return;
			}
			if (const auto widget = capture->widget()) {
				inspect(widget, capture->image());
			} else {
				Fail(
					u"inspect capture %1"_q.arg(name),
					u"accepted target was destroyed before inspection"_q);
			}
		},
		.timeout = timeout,
		.timeoutDetails = [=] { return capture->pendingReason(); },
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
	if (_finished) {
		return;
	}
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
		const auto details = stage.timeoutDetails
			? stage.timeoutDetails()
			: QString();
		Fail(
			u"stage timed out: %1"_q.arg(stage.name),
			details.isEmpty()
				? u"waited %1 ms"_q.arg(stage.timeout)
				: u"waited %1 ms; last state: %2"_q.arg(
					stage.timeout).arg(details));
		finish();
	}
}

void Runner::beginStage() {
	while (true) {
		const auto &stage = _stages[_index];
		Step(stage.name);
		_stageStarted = crl::now();
		const auto reason = stage.skipReason
			? stage.skipReason()
			: QString();
		if (reason.isEmpty()) {
			if (stage.run) {
				stage.run();
			}
			return;
		}
		Skipped(stage.name, reason);
		if (++_index == int(_stages.size())) {
			finish();
			return;
		}
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
	const auto fuseDeadline = crl::now() + kAbortAfterQuit;
	QTimer::singleShot(int(kAbortAfterQuit), [=] {
		ResolveQuitFuse(fuseDeadline);
	});
	base::call_delayed(kFinishDrainDelay, [] {
		const auto failures = FailureCount();
		const auto skipped = SkippedCount();
		LogRaw(u"SCENARIO_RESULT: %1 (failures: %2%3)"_q.arg(
			failures ? u"FAIL"_q : u"PASS"_q,
			QString::number(failures),
			skipped
				? u", skipped: %1"_q.arg(skipped)
				: QString()));
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
