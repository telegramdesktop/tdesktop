/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_gated_stage.h"

#include "base/timer.h"
#include "base/unique_qptr.h"
#include "core/application.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "window/window_controller.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtGui/QPainter>

#include "styles/palette.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

const auto kFirstRefusal = u"fixture gate: stage-group self-test "
	"prerequisite refused by construction"_q;
const auto kLaterRefusal = u"later incidental refusal: target does not "
	"exist"_q;
const auto kInnerRefusal = u"inner group refusal: never read under a "
	"refused outer group"_q;
const auto kOwnRefusal = u"own stage refusal: never read under a refused "
	"group"_q;
const auto kNotOwed = u"applies=0: no restoration owed by construction"_q;
const auto kEndRefusal = u"fixture gate: stage-group self-test ending "
	"group refused by construction"_q;
const auto kSkippedPrefix = u"TEST_RESULT: N/A: "_q;

// Capture names become screenshot file names, so the tokens are file-safe.
const auto kRefusedLabel = u"group-refused"_q;
const auto kOpenLabel = u"group-open"_q;
const auto kUngatedLabel = u"group-ungated"_q;
const auto kEndingLabel = u"group-ending"_q;

constexpr auto kRefusedTimeout = crl::time(1000);
constexpr auto kTimeoutPathTimeout = crl::time(500);
constexpr auto kDependentStages = 7;
constexpr auto kRefusedRows = 10;
constexpr auto kSkippedRows = 11;
constexpr auto kFixtureWidthFactor = 4;
constexpr auto kFixtureHeightFactor = 3;

struct GroupCounters {
	int runs = 0;
	int untilPolls = 0;
	int thens = 0;
	int ownSkipReads = 0;
	int resolves = 0;
	int readies = 0;
	int actions = 0;
	int captureResolves = 0;
	int captureReadies = 0;
	int inspectResolves = 0;
	int inspectReadies = 0;
	int inspects = 0;
	int helperRuns = 0;
	int helperResolves = 0;
	int helperActions = 0;
};

struct State {
	base::unique_qptr<Ui::RpWidget> owner;
	QString gate;
	GroupCounters refused;
	GroupCounters open;
	GroupCounters ungated;
	GroupCounters ending;
	base::Timer held;
	qint64 logOffset = 0;
	int skippedBefore = 0;
	int failuresBefore = 0;
	int recoveryLater = 0;
	int recoveryCleared = 0;
	int independentRuns = 0;
	int notOwedRuns = 0;
	int afterLater = 0;
	int afterCleared = 0;
	int innerGateReads = 0;
	int openGateReads = 0;
	int releases = 0;
	bool built = false;
};

struct GroupRows {
	int firstRefusal = 0;
	int notOwed = 0;
	int later = 0;
	int inner = 0;
	int own = 0;
	int timeouts = 0;
	QStringList refusedNames;
};

[[nodiscard]] QString GroupCountersText(const GroupCounters &counters) {
	const auto fields = {
		std::pair(u"runs"_q, counters.runs),
		std::pair(u"untilPolls"_q, counters.untilPolls),
		std::pair(u"thens"_q, counters.thens),
		std::pair(u"ownSkipReads"_q, counters.ownSkipReads),
		std::pair(u"resolves"_q, counters.resolves),
		std::pair(u"readies"_q, counters.readies),
		std::pair(u"actions"_q, counters.actions),
		std::pair(u"captureResolves"_q, counters.captureResolves),
		std::pair(u"captureReadies"_q, counters.captureReadies),
		std::pair(u"inspectResolves"_q, counters.inspectResolves),
		std::pair(u"inspectReadies"_q, counters.inspectReadies),
		std::pair(u"inspects"_q, counters.inspects),
		std::pair(u"helperRuns"_q, counters.helperRuns),
		std::pair(u"helperResolves"_q, counters.helperResolves),
		std::pair(u"helperActions"_q, counters.helperActions),
	};
	auto result = QStringList();
	for (const auto &[name, value] : fields) {
		result.push_back(u"%1=%2"_q.arg(name).arg(value));
	}
	return result.join(QChar(' '));
}

[[nodiscard]] bool Untouched(const GroupCounters &counters) {
	return GroupCountersText(counters) == GroupCountersText(GroupCounters());
}

[[nodiscard]] bool HealthyCounters(const GroupCounters &counters) {
	return (counters.runs == 1)
		&& (counters.thens == 2)
		&& (counters.untilPolls >= 1)
		&& (counters.ownSkipReads == 2)
		&& (counters.actions == 1)
		&& (counters.resolves >= 1)
		&& (counters.captureResolves >= 1)
		&& (counters.inspects == 1)
		&& (counters.helperRuns == 1)
		&& (counters.helperActions == 1);
}

[[nodiscard]] QString PathName(StageGroupGatePath path) {
	switch (path) {
	case StageGroupGatePath::Complete: return u"complete"_q;
	case StageGroupGatePath::SkippedToEnd: return u"skipped-to-end"_q;
	case StageGroupGatePath::Timeout: return u"timeout"_q;
	case StageGroupGatePath::Watchdog: return u"watchdog"_q;
	}
	Unexpected("Unknown StageGroupGatePath");
}

[[nodiscard]] QString LogPath() {
	return EvidenceDir() + u"test_log.txt"_q;
}

// A Windows row ends in CRLF; one CR is chopped so rows compare the same.
[[nodiscard]] QStringList LogLinesSince(qint64 from) {
	auto file = QFile(LogPath());
	if (!file.open(QIODevice::ReadOnly) || !file.seek(from)) {
		return QStringList();
	}
	auto result = QString::fromUtf8(file.readAll()).split(QChar('\n'));
	for (auto &line : result) {
		if (line.endsWith(QChar('\r'))) {
			line.chop(1);
		}
	}
	if (!result.isEmpty() && result.back().isEmpty()) {
		result.pop_back();
	}
	return result;
}

[[nodiscard]] GroupRows ReadGroupRows(qint64 from) {
	const auto endsWith = [](const QString &row, const QString &reason) {
		return row.endsWith(u" - "_q + reason);
	};
	auto result = GroupRows();
	for (const auto &row : LogLinesSince(from)) {
		if (row.contains(u"stage timed out"_q)) {
			++result.timeouts;
		}
		if (!row.startsWith(kSkippedPrefix)) {
			continue;
		}
		if (row.contains(kRefusedLabel)) {
			const auto rest = row.mid(kSkippedPrefix.size());
			result.refusedNames.push_back(
				rest.left(rest.lastIndexOf(u" - "_q)));
			if (endsWith(row, kFirstRefusal)) {
				++result.firstRefusal;
			}
		}
		if (endsWith(row, kNotOwed)) {
			++result.notOwed;
		} else if (endsWith(row, kLaterRefusal)) {
			++result.later;
		} else if (endsWith(row, kInnerRefusal)) {
			++result.inner;
		} else if (endsWith(row, kOwnRefusal)) {
			++result.own;
		}
	}
	return result;
}

[[nodiscard]] bool BuildFixture(State &state) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return false;
	}
	const auto row = st::defaultActiveButton.height;
	state.owner = base::make_unique_q<Ui::RpWidget>(window->widget().get());
	const auto owner = state.owner.get();
	owner->setAttribute(Qt::WA_OpaquePaintEvent);
	owner->paintOn([=](QPainter &p) {
		p.fillRect(owner->rect(), st::windowBg);
		p.fillRect(0, row, owner->width(), row, st::attentionButtonFg);
	});
	owner->setGeometry(
		0,
		0,
		row * kFixtureWidthFactor,
		row * kFixtureHeightFactor);
	owner->show();
	Ui::SendPendingMoveResizeEvents(owner);
	return true;
}

void Arm(State &state) {
	state.built = BuildFixture(state);
	const auto owner = state.owner.get();
	Check(
		state.built,
		u"fixture gate: stage-group self-test fixture built"_q,
		owner
			? u"size=%1x%2 visible=%3"_q
				.arg(owner->width())
				.arg(owner->height())
				.arg(owner->isVisible() ? 1 : 0)
			: u"no primary window"_q);
	state.logOffset = QFileInfo(LogPath()).size();
	state.skippedBefore = SkippedCount();
	state.failuresBefore = FailureCount();
}

// The stand-in for an external Append* helper that knows nothing about
// gates: it appends through the runner like every real helper does.
void AppendCountingHelper(
		not_null<Runner*> runner,
		const QString &label,
		not_null<GroupCounters*> counters,
		Fn<QWidget*()> resolve,
		crl::time timeout) {
	runner->add({
		.name = u"stage-group gate self-test %1 helper stage"_q.arg(label),
		.run = [=] { ++counters->helperRuns; },
		.until = [] { return true; },
		.timeout = timeout,
	});
	runner->actOnWidget(
		u"%1 helper act"_q.arg(label),
		[=] {
			++counters->helperResolves;
			return resolve();
		},
		[=](QWidget*) { ++counters->helperActions; },
		{},
		timeout);
}

void AppendDependentStages(
		not_null<Runner*> runner,
		const QString &label,
		not_null<GroupCounters*> counters,
		Fn<QWidget*()> resolve,
		bool ready,
		const QString &ownReason,
		crl::time timeout) {
	runner->add({
		.name = u"stage-group gate self-test %1 wait"_q.arg(label),
		.run = [=] { ++counters->runs; },
		.until = [=] {
			++counters->untilPolls;
			return ready;
		},
		.then = [=] { ++counters->thens; },
		.timeout = timeout,
	});
	runner->add({
		.name = u"stage-group gate self-test %1 own policy"_q.arg(label),
		.skipReason = [=] {
			++counters->ownSkipReads;
			return ownReason;
		},
		.then = [=] { ++counters->thens; },
		.timeout = timeout,
	});
	runner->actOnWidget(
		u"%1 act"_q.arg(label),
		[=] {
			++counters->resolves;
			return resolve();
		},
		[=](QWidget*) { ++counters->actions; },
		[=](QWidget*) {
			++counters->readies;
			return ready;
		},
		timeout);
	runner->captureWidget(
		u"%1-capture"_q.arg(label),
		[=] {
			++counters->captureResolves;
			return resolve();
		},
		[=](QWidget*) {
			++counters->captureReadies;
			return ready;
		},
		timeout);
	runner->captureAndInspect(
		u"%1-inspect"_q.arg(label),
		[=] {
			++counters->inspectResolves;
			return resolve();
		},
		[=](QWidget*) {
			++counters->inspectReadies;
			return ready;
		},
		[=](QWidget*, const QImage &image) {
			++counters->inspects;
			Check(
				!image.isNull(),
				u"stage-group gate self-test: %1 inspected a painted "
				"frame"_q.arg(label),
				u"size=%1x%2"_q.arg(image.width()).arg(image.height()));
		},
		timeout,
		{},
		[=] {
			++counters->ownSkipReads;
			return ownReason;
		});
	AppendCountingHelper(runner, label, counters, resolve, timeout);
}

void AppendRefusedGroup(
		not_null<Runner*> runner,
		std::shared_ptr<State> state) {
	runner->add({
		.name = u"stage-group gate self-test prerequisite"_q,
		.then = [=] {
			state->gate = kFirstRefusal;
			Note(u"stage-group gate self-test: the prerequisite refused: "
				"%1"_q.arg(kFirstRefusal));
		},
	});
	const auto gate = [=] { return state->gate; };
	runner->addGated(gate, [&] {
		// A resource a helper acquires while appending, released at finish.
		state->held.setCallback([] {});
		state->held.callEach(crl::time(5));

		// The null resolver stands for the widget only a healthy fixture
		// would show: a skip that did not take times out on it.
		AppendDependentStages(
			runner,
			kRefusedLabel,
			&state->refused,
			[]() -> QWidget* { return nullptr; },
			false,
			kOwnRefusal,
			kRefusedTimeout);
		runner->addRecovery({
			.name = u"stage-group gate self-test recovery sets a later "
				"reason"_q,
			.run = [=] {
				++state->recoveryLater;
				state->gate = kLaterRefusal;
			},
		});
		runner->add({
			.name = u"stage-group gate self-test %1 after a later "
				"reason"_q.arg(kRefusedLabel),
			.run = [=] { ++state->afterLater; },
			.then = [=] { ++state->afterLater; },
			.timeout = kRefusedTimeout,
		});
		runner->addRecovery({
			.name = u"stage-group gate self-test recovery clears the "
				"gate"_q,
			.run = [=] {
				++state->recoveryCleared;
				state->gate = QString();
			},
		});
		runner->add({
			.name = u"stage-group gate self-test %1 after a cleared "
				"gate"_q.arg(kRefusedLabel),
			.run = [=] { ++state->afterCleared; },
			.then = [=] { ++state->afterCleared; },
			.timeout = kRefusedTimeout,
		});
		const auto inner = [=] {
			++state->innerGateReads;
			return kInnerRefusal;
		};
		runner->addGated(inner, [&] {
			runner->actOnWidget(
				u"%1 nested act"_q.arg(kRefusedLabel),
				[=]() -> QWidget* {
					++state->refused.resolves;
					return nullptr;
				},
				[=](QWidget*) { ++state->refused.actions; },
				[=](QWidget*) {
					++state->refused.readies;
					return false;
				},
				kRefusedTimeout);
		});
	});
}

void AppendIndependentRestoration(
		not_null<Runner*> runner,
		std::shared_ptr<State> state) {
	runner->add({
		.name = u"stage-group gate self-test restoration owed"_q,
		.skipReason = [] { return QString(); },
		.run = [=] { ++state->independentRuns; },
	});
	runner->add({
		.name = u"stage-group gate self-test restoration not owed"_q,
		.skipReason = [] { return kNotOwed; },
		.run = [=] { ++state->notOwedRuns; },
	});
}

void AppendHealthyControls(
		not_null<Runner*> runner,
		std::shared_ptr<State> state) {
	const auto fixtureResolver = [=]() -> QWidget* {
		return state->owner.get();
	};
	const auto open = [=] {
		++state->openGateReads;
		return QString();
	};
	runner->addGated(open, [&] {
		AppendDependentStages(
			runner,
			kOpenLabel,
			&state->open,
			fixtureResolver,
			true,
			QString(),
			kDefaultStageTimeout);
	});
	AppendDependentStages(
		runner,
		kUngatedLabel,
		&state->ungated,
		fixtureResolver,
		true,
		QString(),
		kDefaultStageTimeout);
}

void CheckRefusedGroup(const State &state, int skipped, int failures) {
	Check(
		Untouched(state.refused)
			&& !state.afterLater
			&& !state.afterCleared
			&& !state.innerGateReads,
		u"stage-group gate self-test: the refused group resolved, polled, "
		"acted and captured nothing"_q,
		u"refused={%1} afterLater=%2 afterCleared=%3 innerGateReads=%4"_q
			.arg(GroupCountersText(state.refused))
			.arg(state.afterLater)
			.arg(state.afterCleared)
			.arg(state.innerGateReads));
	Check(
		(state.recoveryLater == 1)
			&& (state.recoveryCleared == 1)
			&& (state.independentRuns == 1)
			&& !state.notOwedRuns,
		u"stage-group gate self-test: the recovery stages ran on their own "
		"policy"_q,
		u"recoveryLater=%1 recoveryCleared=%2 independentRuns=%3 "
		"notOwedRuns=%4"_q
			.arg(state.recoveryLater)
			.arg(state.recoveryCleared)
			.arg(state.independentRuns)
			.arg(state.notOwedRuns));
	Check(
		(skipped == kSkippedRows) && !failures,
		u"stage-group gate self-test: the skipped and failure counters "
		"moved by the composed rows alone"_q,
		u"skippedDelta=%1 expected=%2 failuresDelta=%3"_q
			.arg(skipped)
			.arg(kSkippedRows)
			.arg(failures));
}

void CheckGroupRows(const State &state) {
	const auto rows = ReadGroupRows(state.logOffset);
	const auto firstKept = (rows.firstRefusal == kRefusedRows)
		&& (rows.refusedNames.size() == kRefusedRows)
		&& !rows.later
		&& !rows.inner
		&& !rows.own
		&& !rows.timeouts;
	Check(
		firstKept,
		u"stage-group gate self-test: every refused-group row carries the "
		"first refusal"_q,
		u"firstRefusalRows=%1 expected=%2 refusedRows=%3 laterRows=%4 "
		"innerRows=%5 ownRows=%6 timeoutRows=%7%8"_q
			.arg(rows.firstRefusal)
			.arg(kRefusedRows)
			.arg(rows.refusedNames.size())
			.arg(rows.later)
			.arg(rows.inner)
			.arg(rows.own)
			.arg(rows.timeouts)
			.arg(firstKept
				? QString()
				: u" names=[%1]"_q.arg(rows.refusedNames.join(u"; "_q))));
	Check(
		rows.notOwed == 1,
		u"stage-group gate self-test: the restoration not owed wrote its "
		"own reason"_q,
		u"notOwedRows=%1"_q.arg(rows.notOwed));
}

void CheckHealthyControls(const State &state) {
	Check(
		HealthyCounters(state.open)
			&& (state.openGateReads == kDependentStages),
		u"stage-group gate self-test: the healthy gated controls resolved, "
		"polled, acted once and captured"_q,
		u"open={%1} openGateReads=%2 expected=%3"_q
			.arg(GroupCountersText(state.open))
			.arg(state.openGateReads)
			.arg(kDependentStages));
	Check(
		HealthyCounters(state.ungated),
		u"stage-group gate self-test: the healthy ungated controls "
		"resolved, polled, acted once and captured"_q,
		u"ungated={%1}"_q.arg(GroupCountersText(state.ungated)));
}

void AppendChecks(not_null<Runner*> runner, std::shared_ptr<State> state) {
	runner->add({
		.name = u"stage-group gate self-test checks"_q,
		.then = [=] {
			const auto skipped = SkippedCount() - state->skippedBefore;
			const auto failures = FailureCount() - state->failuresBefore;
			CheckRefusedGroup(*state, skipped, failures);
			CheckGroupRows(*state);
			CheckHealthyControls(*state);
		},
	});
}

void AppendEnding(
		not_null<Runner*> runner,
		std::shared_ptr<State> state,
		StageGroupGatePath path) {
	const auto counted = [&](const QString &name) {
		runner->add({
			.name = name,
			.run = [=] { ++state->ending.runs; },
			.until = [=] {
				++state->ending.untilPolls;
				return false;
			},
			.then = [=] { ++state->ending.thens; },
			.timeout = kRefusedTimeout,
		});
	};
	const auto applicable = [&](crl::time timeout) {
		runner->addGated([] { return QString(); }, [&] {
			runner->add({
				.name = u"stage-group gate self-test %1 applicable %2"_q.arg(
					kEndingLabel,
					PathName(path)),
				.until = [] { return false; },
				.timeout = timeout,
			});
		});
	};
	switch (path) {
	case StageGroupGatePath::Complete:
		runner->add({
			.name = u"stage-group gate self-test ending"_q,
			.then = [=] {
				Note(u"stage-group gate self-test: ending path=%1"_q.arg(
					PathName(path)));
			},
		});
		return;
	case StageGroupGatePath::SkippedToEnd:
		runner->addGated([] { return kEndRefusal; }, [&] {
			counted(u"stage-group gate self-test %1 first"_q.arg(
				kEndingLabel));
			counted(u"stage-group gate self-test %1 last"_q.arg(
				kEndingLabel));
		});
		return;
	case StageGroupGatePath::Timeout:
		applicable(kTimeoutPathTimeout);
		return;
	case StageGroupGatePath::Watchdog:
		applicable(kDefaultStageTimeout);
		return;
	}
	Unexpected("Unknown StageGroupGatePath");
}

void RegisterRelease(
		not_null<Runner*> runner,
		std::shared_ptr<State> state,
		StageGroupGatePath path) {
	runner->onFinish([=] {
		const auto armed = state->held.isActive();
		state->held.cancel();
		state->owner = nullptr;
		++state->releases;
		const auto endingOk = (path != StageGroupGatePath::SkippedToEnd)
			|| Untouched(state->ending);
		Check(
			(state->releases == 1) && armed && endingOk,
			u"stage-group gate self-test: the finish release ran once "
			"with the composition-time resource still held"_q,
			u"count=%1 armed=%2 path=%3 ending={%4}"_q
				.arg(state->releases)
				.arg(armed ? 1 : 0)
				.arg(PathName(path), GroupCountersText(state->ending)));
	});
}

} // namespace

void AppendGatedStageSelfTest(not_null<Runner*> runner) {
	struct State {
		crl::time armedAt = 0;
		crl::time appliedAt = 0;
		int skippedRuns = 0;
		int skippedUntilPolls = 0;
		int skippedThens = 0;
		int appliedRuns = 0;
	};
	const auto state = std::make_shared<State>();

	runner->add({
		.name = u"gated stage self-test: arm"_q,
		.then = [=] { state->armedAt = crl::now(); },
	});

	runner->add({
		.name = u"gated stage self-test: a stage whose gate reads false"_q,
		.skipReason = [] {
			return u"applies=0: the self-test gate is false by "
				"construction"_q;
		},
		.run = [=] { ++state->skippedRuns; },
		.until = [=] {
			++state->skippedUntilPolls;
			return false;
		},
		.then = [=] { ++state->skippedThens; },
		.timeout = crl::time(1000),
	});

	runner->add({
		.name = u"gated stage self-test: a stage whose gate reads true"_q,
		.skipReason = [] { return QString(); },
		.run = [=] {
			state->appliedAt = crl::now();
			++state->appliedRuns;
		},
		.until = [] { return true; },
		.then = [=] {
			Check(
				(state->appliedRuns == 1)
					&& !state->skippedRuns
					&& !state->skippedUntilPolls
					&& !state->skippedThens,
				u"gated stage self-test: the false gate skipped run, "
				"until and then and the true gate ran"_q,
				u"skippedRun=%1 skippedUntilPolls=%2 skippedThen=%3 "
				"appliedRun=%4 elapsedSinceArmMs=%5"_q
					.arg(state->skippedRuns)
					.arg(state->skippedUntilPolls)
					.arg(state->skippedThens)
					.arg(state->appliedRuns)
					.arg(state->appliedAt - state->armedAt));
		},
	});
}

void AppendStageGroupGateSelfTest(
		not_null<Runner*> runner,
		StageGroupGatePath path) {
	const auto state = std::make_shared<State>();
	RegisterRelease(runner, state, path);
	runner->add({
		.name = u"stage-group gate self-test arm"_q,
		.then = [=] { Arm(*state); },
	});
	AppendRefusedGroup(runner, state);
	AppendIndependentRestoration(runner, state);
	AppendHealthyControls(runner, state);
	AppendChecks(runner, state);
	AppendEnding(runner, state, path);
}

} // namespace Test

#endif // _DEBUG
