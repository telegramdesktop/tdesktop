/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_ops.h"

#include "test/test_widget_ops_internal.h"
#include "tgw/tgw_renderer.h"
#include "twidget/widget_host.h"

#include <QtCore/QStringList>

#include <array>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Test::WidgetOps {
namespace {

using Outcome = Tgw::HostedOutcome;

// The death WidgetHost's local glue sends no `frozen` for, which a hosted
// host's onDeath may still carry, and the widget helper's end, which every
// host it served reports alike, spared or charged.
constexpr auto kTerminated = "terminated";
constexpr auto kHostedDeath = "the widget engine process ended";

// The reasons a run's decision carries in the legs: StalledReason(), an
// ordinary death's label, two of Telegram's own refusals and an engine's
// own death.
constexpr auto kStalledReason = "The widget stopped responding";
constexpr auto kStoppedLabel = "Widget stopped";
constexpr auto kSkippedFrame
	= "The widget renderer skipped a frame asked to paint";
constexpr auto kUnaskedFrame
	= "The widget renderer sent a frame nothing asked for";
constexpr auto kOutOfMemory = "Out of memory";

// Work ids no RendererCore is given (theirs count up from 1) and no other
// leg of the self-check uses: the finals leg's five, the eviction leg's
// 34 counted down from its first, and the reading rows' one.
constexpr auto kFinalsWork = std::numeric_limits<uint64>::max() - 40;
constexpr auto kEvictedWork = std::numeric_limits<uint64>::max() - 60;
constexpr auto kReadWork = std::numeric_limits<uint64>::max() - 100;

constexpr auto kHostHtml = "<p>widget ops self-check</p>";
constexpr auto kHostWidth = 240.;

[[nodiscard]] QString Flag(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString Text(const char *text) {
	return QString::fromLatin1(text);
}

// The reason of a summary's "Died: " line, as the conformance runner's
// StopRefusal() reads it.
[[nodiscard]] QString LineReason(const QString &deathLine) {
	return deathLine.startsWith(u"Died: "_q)
		? deathLine.mid(6).trimmed()
		: QString();
}

// An ordinary death's raw diagnosis from the sampled host itself: its
// summary's line, else the first death the host reported, unless neither
// says more than "terminated".
[[nodiscard]] QString OrdinaryDiagnosis(
		const QString &deathLine,
		const std::optional<QString> &died) {
	const auto line = LineReason(deathLine);
	const auto raw = line.isEmpty()
		? died.value_or(QString()).trimmed()
		: line;
	return (raw == Text(kTerminated)) ? QString() : raw;
}

[[nodiscard]] std::optional<WidgetHostedRun> RunOf(
		const WidgetOpsReading &reading,
		int generation) {
	for (const auto &run : reading.hostedRuns) {
		if (run.generation == generation) {
			return run;
		}
	}
	return std::nullopt;
}

[[nodiscard]] QString RunText(const std::optional<WidgetHostedRun> &run) {
	return run ? WidgetHostedRunText(*run) : u"none"_q;
}

[[nodiscard]] QString RunsText(const WidgetOpsReading &reading) {
	auto result = QStringList();
	for (const auto &run : reading.hostedRuns) {
		result.push_back(WidgetHostedRunText(run));
	}
	return u"["_q + result.join(u" | "_q) + u"]"_q;
}

// The counts a final charges, then the barrier's.
[[nodiscard]] auto Charges(const WidgetOpsReading &reading) {
	return std::make_tuple(
		reading.hostedFinal,
		reading.fatalFrozen,
		reading.desktopCharges,
		reading.sparedRuns,
		reading.unaffectedRuns);
}

[[nodiscard]] QString CountsText(const WidgetOpsReading &reading) {
	return u"selected=%1 final=%2 fatal=%3 desktop=%4 spared=%5 "
		"unaffected=%6 factories=%7/%8 wrappers=%9/%10 finished=%11"_q.arg(
			QString::number(reading.hostedSelected),
			QString::number(reading.hostedFinal),
			QString::number(reading.fatalFrozen),
			QString::number(reading.desktopCharges),
			QString::number(reading.sparedRuns),
			QString::number(reading.unaffectedRuns),
			QString::number(reading.factoriesReleased),
			QString::number(reading.factoriesSelected),
			QString::number(reading.wrappersDestroyed),
			QString::number(reading.wrappersBuilt),
			Flag(WidgetTapFinished(reading)));
}

[[nodiscard]] QString DeathText(const WidgetHostedDeath &death) {
	return u"died=%1 measured=%2 reason=[%3] line=[%4] sampled=%5"_q.arg(
		Flag(death.died),
		Flag(death.measured),
		death.reason,
		death.deathLine,
		RunText(death.sampled));
}

[[nodiscard]] bool Counts(
		const WidgetOpsReading &reading,
		int fatal,
		int desktop,
		int spared,
		int unaffected) {
	return (reading.fatalFrozen == fatal)
		&& (reading.desktopCharges == desktop)
		&& (reading.sparedRuns == spared)
		&& (reading.unaffectedRuns == unaffected)
		&& (reading.hostedFinal == fatal + desktop + spared + unaffected);
}

[[nodiscard]] bool Recorded(
		const std::optional<WidgetHostedRun> &run,
		Outcome outcome,
		Outcome transport,
		const char *reason,
		const char *died,
		bool claimed) {
	return run
		&& run->final
		&& (run->outcome == outcome)
		&& (run->transport == transport)
		&& (run->reason == Text(reason))
		&& (died
			? (run->died == Text(died))
			: !run->died)
		&& (run->claimed == claimed);
}

// Five works of one tap played as the runtime's construction makes them,
// the first two installed on stub hosts and three never installed,
// finalized in an order that leaves the first one's final for last.
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> CheckFinals(GlobalMsgId id) {
	const auto tap = ArmWidgetOpsTap(id);
	auto runs = std::vector<WidgetHostedPlay>();
	for (auto i = 0; i != 5; ++i) {
		runs.emplace_back(tap, id, kFinalsWork - i);
	}
	auto &stalled = runs[0];
	auto &refused = runs[1];
	auto &claimed = runs[2];
	auto &spared = runs[3];
	auto &unaffected = runs[4];
	const auto selected = ReadWidgetOps(*tap);
	auto awaited = true;
	for (const auto &run : runs) {
		awaited = awaited && run.selected();
	}
	const auto stalledBuilt = stalled.install();
	const auto refusedBuilt = refused.install();
	const auto built = stalledBuilt && refusedBuilt;

	stalled.death(kHostedDeath);
	stalled.change(kHostedDeath);
	const auto raw = ReadWidgetOps(*tap);
	const auto forwarded = (stalled.deaths()
		== std::vector<std::string>{ kHostedDeath });

	const auto refusedFinished = refused.finish({
		.reason = Text(kSkippedFrame),
		.outcome = Outcome::Refused,
		.transport = Outcome::Spared,
	});
	const auto beforeLate = ReadWidgetOps(*tap);
	refused.change(kHostedDeath);
	const auto late = ReadWidgetOps(*tap);
	claimed.finish({
		.reason = Text(kUnaskedFrame),
		.outcome = Outcome::Refused,
	});
	const auto copy = spared.observation();
	const auto sparedDecision = Tgw::HostedDecision{
		.reason = Text(kStoppedLabel),
		.outcome = Outcome::Spared,
		.transport = Outcome::Spared,
	};
	spared.finish(sparedDecision);
	unaffected.finish(Tgw::HostedDecision());
	if (copy) {
		copy(sparedDecision);
	}
	const auto pending = ReadWidgetOps(*tap);

	auto released = true;
	for (auto &run : runs) {
		released = run.drop() && released;
	}
	const auto dropped = ReadWidgetOps(*tap);
	stalled.finish({
		.reason = Text(kStalledReason),
		.outcome = Outcome::Stalled,
		.transport = Outcome::Stalled,
	});
	const auto finished = ReadWidgetOps(*tap);
	DisarmWidgetOpsTap(id);

	auto ordered = (finished.hostedRuns.size() == runs.size());
	for (auto i = 0; ordered && (i != int(runs.size())); ++i) {
		const auto &run = finished.hostedRuns[i];
		ordered = (run.generation == i + 1) && (run.work == runs[i].work());
	}
	const auto rawRun = RunOf(raw, 1);
	const auto lateBefore = RunOf(beforeLate, 2);
	const auto stalledRun = RunOf(finished, 1);
	const auto hostedDeath = Text(kHostedDeath);
	return {
		{
			.name = u"hosted finals: five works played as the runtime "
				"constructs them await their finals, installed or not; a "
				"host's raw death reaches the runtime's own observer and "
				"charges nothing; each final counts once in its own field, "
				"and a second call of a copy of one work's observation, which "
				"only the tap's own guard stops, counts nothing"_q,
			.details = u"built=%1 awaited=%2 selected: %3 | raw: %4 | "
				"pending: %5 | copy=%6"_q.arg(
					Flag(built),
					Flag(awaited),
					CountsText(selected),
					CountsText(raw),
					CountsText(pending),
					Flag(bool(copy))),
			.passed = built
				&& awaited
				&& (selected.hostedSelected == 5)
				&& (selected.factoriesSelected == 5)
				&& Counts(selected, 0, 0, 0, 0)
				&& forwarded
				&& Counts(raw, 0, 0, 0, 0)
				&& refusedFinished
				&& copy
				&& Counts(pending, 0, 2, 1, 1),
		},
		{
			.name = u"hosted finals: dropping every installer and host "
				"balances the factories and wrappers while the final "
				"observations still wait, so they hold neither, and the "
				"shared barrier (WidgetTapFinished()) stays open until the "
				"last final, which closes it"_q,
			.details = u"released=%1 pending: %2 | dropped: %3 | "
				"finished: %4"_q.arg(
					Flag(released),
					CountsText(pending),
					CountsText(dropped),
					CountsText(finished)),
			.passed = released
				&& !WidgetTapFinished(selected)
				&& !WidgetTapFinished(pending)
				&& (dropped.factoriesReleased == dropped.factoriesSelected)
				&& (dropped.wrappersBuilt == 2)
				&& (dropped.wrappersDestroyed == dropped.wrappersBuilt)
				&& Counts(dropped, 0, 2, 1, 1)
				&& !WidgetTapFinished(dropped)
				&& Counts(finished, 1, 2, 1, 1)
				&& (finished.hostedSelected == 5)
				&& WidgetTapFinished(finished),
		},
		{
			.name = u"hosted finals: each work's record, in selection "
				"order, keeps its final charge and the transport its "
				"deciding event read apart from the raw death its host "
				"reported: a stalled run's raw death before its final, a "
				"refusal on a spared route with both, a refusal whose claim "
				"found no end marked claimed, a spared and an unaffected "
				"run, and a raw death after a final filling its record "
				"alone"_q,
			.details = u"raw: %1 | late: %2 -> %3 | %4"_q.arg(
				RunText(rawRun),
				RunText(lateBefore),
				RunText(RunOf(late, 2)),
				RunsText(finished)),
			.passed = ordered
				&& rawRun
				&& (rawRun->died == hostedDeath)
				&& !rawRun->final
				&& (Charges(beforeLate) == Charges(late))
				&& lateBefore
				&& !lateBefore->died
				&& Recorded(
					stalledRun,
					Outcome::Stalled,
					Outcome::Stalled,
					kStalledReason,
					kHostedDeath,
					false)
				&& Recorded(
					RunOf(finished, 2),
					Outcome::Refused,
					Outcome::Spared,
					kSkippedFrame,
					kHostedDeath,
					false)
				&& Recorded(
					RunOf(finished, 3),
					Outcome::Refused,
					Outcome::None,
					kUnaskedFrame,
					nullptr,
					true)
				&& Recorded(
					RunOf(finished, 4),
					Outcome::Spared,
					Outcome::Spared,
					kStoppedLabel,
					nullptr,
					false)
				&& Recorded(
					RunOf(finished, 5),
					Outcome::None,
					Outcome::None,
					"",
					nullptr,
					false),
		},
	};
}

// A charged first selection, then kWidgetHostedRuns + 1 later uncharged
// ones, each never installed, finalized at once and dropped.
[[nodiscard]] WidgetOpsSelfCheckRow CheckEviction(GlobalMsgId id) {
	const auto tap = ArmWidgetOpsTap(id);
	const auto line = u"Died: "_q + Text(kHostedDeath);
	auto first = WidgetHostedPlay(tap, id, kEvictedWork);
	const auto firstSelected = first.selected();
	const auto firstFinished = first.finish({
		.reason = Text(kSkippedFrame),
		.outcome = Outcome::Refused,
		.transport = Outcome::Spared,
	});
	const auto firstDropped = first.drop();
	auto played = firstSelected && firstFinished && firstDropped;
	const auto charged = ReadWidgetOps(*tap);
	const auto early = ReadHostedDeath(charged, first.work(), line);
	const auto spared = Tgw::HostedDecision{
		.reason = Text(kStoppedLabel),
		.outcome = Outcome::Spared,
		.transport = Outcome::Spared,
	};
	auto last = uint64();
	auto spares = 0;
	for (auto i = 1; i <= kWidgetHostedRuns + 1; ++i) {
		auto later = WidgetHostedPlay(tap, id, kEvictedWork - i);
		const auto spare = (i % 2 == 1);
		const auto chosen = later.selected();
		const auto finished = later.finish(spare
			? spared
			: Tgw::HostedDecision());
		const auto dropped = later.drop();
		played = played && chosen && finished && dropped;
		spares += spare ? 1 : 0;
		last = later.work();
	}
	const auto after = ReadWidgetOps(*tap);
	DisarmWidgetOpsTap(id);
	const auto evicted = ReadHostedDeath(after, first.work(), line);
	const auto kept = ReadHostedDeath(after, first.work(), line, &early);
	const auto borrowed = ReadHostedDeath(after, last, line, &early);
	const auto gone = (ranges::find(
		after.hostedRuns,
		first.work(),
		&WidgetHostedRun::work) == end(after.hostedRuns));
	const auto reason = Text(kSkippedFrame);
	return {
		.name = u"hosted eviction: a charged first selection evicted from "
			"the records by 33 later uncharged ones stays counted, so the "
			"shared reading of its work stays dead with the reason "
			"unmeasured, a reason measured while its record was kept stays "
			"with that work alone, and a spared later sample printing the "
			"same raw line stays dead and unmeasured"_q,
		.details = u"played=%1 charged: %2 | after: %3 | early: %4 | "
			"evicted: %5 | kept: %6 | borrowed: %7"_q.arg(
				Flag(played),
				CountsText(charged),
				CountsText(after),
				DeathText(early),
				DeathText(evicted),
				DeathText(kept),
				DeathText(borrowed)),
		.passed = played
			&& Counts(charged, 0, 1, 0, 0)
			&& early.died
			&& early.measured
			&& (early.reason == reason)
			&& gone
			&& (int(after.hostedRuns.size()) == kWidgetHostedRuns)
			&& (after.hostedSelected == kWidgetHostedRuns + 2)
			&& Counts(after, 0, 1, spares, kWidgetHostedRuns + 1 - spares)
			&& WidgetTapFinished(after)
			&& evicted.died
			&& !evicted.measured
			&& evicted.reason.isEmpty()
			&& !evicted.sampled
			&& (evicted.deathLine == line)
			&& kept.died
			&& kept.measured
			&& (kept.reason == reason)
			&& borrowed.died
			&& !borrowed.measured
			&& borrowed.reason.isEmpty()
			&& borrowed.sampled
			&& (borrowed.sampled->outcome == Outcome::Spared),
	};
}

// ReadHostedDeath() over literal readings of one sampled work.
[[nodiscard]] WidgetOpsSelfCheckRow CheckReadings() {
	struct ReadingCase {
		const char *name = nullptr;
		const char *line = "";
		const char *recorded = "";
		const char *died = nullptr;
		const char *measured = "";
		Outcome outcome = Outcome::None;
		int charges = 0;
		bool final = true;
		bool sampled = true;
		bool dead = false;
	};
	const auto cases = std::array{
		ReadingCase{
			.name = "stalled, healthy sample",
			.recorded = kStalledReason,
			.measured = kStalledReason,
			.outcome = Outcome::Stalled,
			.charges = 1,
			.dead = true,
		},
		ReadingCase{
			.name = "refused, collateral line",
			.line = "Died: the widget engine process ended",
			.recorded = kSkippedFrame,
			.died = kHostedDeath,
			.measured = kSkippedFrame,
			.outcome = Outcome::Refused,
			.charges = 1,
			.dead = true,
		},
		ReadingCase{
			.name = "ordinary, its own line",
			.line = "Died: Out of memory",
			.recorded = kStoppedLabel,
			.died = kOutOfMemory,
			.measured = kOutOfMemory,
			.outcome = Outcome::Ordinary,
			.charges = 1,
			.dead = true,
		},
		ReadingCase{
			.name = "ordinary, healthy sample, its raw death",
			.recorded = kStoppedLabel,
			.died = kOutOfMemory,
			.measured = kOutOfMemory,
			.outcome = Outcome::Ordinary,
			.charges = 1,
			.dead = true,
		},
		ReadingCase{
			.name = "ordinary, terminated",
			.line = "Died: terminated",
			.recorded = kStoppedLabel,
			.died = kTerminated,
			.outcome = Outcome::Ordinary,
			.charges = 1,
			.dead = true,
		},
		ReadingCase{
			.name = "spared sample, another work charged",
			.line = "Died: the widget engine process ended",
			.recorded = kStoppedLabel,
			.died = kHostedDeath,
			.outcome = Outcome::Spared,
			.charges = 1,
			.dead = true,
		},
		ReadingCase{
			.name = "spared sample, nothing charged",
			.line = "Died: the widget engine process ended",
			.recorded = kStoppedLabel,
			.died = kHostedDeath,
			.outcome = Outcome::Spared,
		},
		ReadingCase{
			.name = "sample not final yet",
			.line = "Died: the widget engine process ended",
			.died = kHostedDeath,
			.final = false,
		},
		ReadingCase{
			.name = "no sample, a work charged",
			.line = "",
			.charges = 1,
			.sampled = false,
			.dead = true,
		},
	};
	auto readings = QStringList();
	auto passed = true;
	for (const auto &entry : cases) {
		auto run = WidgetHostedRun{
			.reason = entry.final
				? Text(entry.recorded)
				: QString(),
			.work = kReadWork,
			.generation = 1,
			.outcome = entry.outcome,
			.transport = entry.outcome,
			.final = entry.final,
		};
		if (entry.died) {
			run.died = Text(entry.died);
		}
		auto reading = WidgetOpsReading();
		if (entry.outcome == Outcome::Refused) {
			reading.desktopCharges = entry.charges;
		} else {
			reading.fatalFrozen = entry.charges;
		}
		reading.hostedRuns.push_back(run);
		const auto line = Text(entry.line);
		const auto death = ReadHostedDeath(
			reading,
			entry.sampled ? kReadWork : 0,
			line);
		const auto measured = Text(entry.measured);
		passed = passed
			&& (death.died == entry.dead)
			&& (death.measured == !measured.isEmpty())
			&& (death.reason == measured)
			&& (death.deathLine == line)
			&& (death.sampled.has_value() == entry.sampled);
		readings.push_back(u"{%1: %2}"_q.arg(
			Text(entry.name),
			DeathText(death)));
	}
	return {
		.name = u"hosted reading: only the sampled work's own final charge "
			"gives a cause, a stalled run and a refusal Telegram made their "
			"recorded reason over a healthy or collateral sample, an "
			"ordinary one its own raw diagnosis unless it says only "
			"terminated; a spared, unfinished or absent sample gives none, "
			"and the verdict follows the charges alone"_q,
		.details = readings.join(u" "_q),
		.passed = passed,
	};
}

} // namespace

std::vector<WidgetOpsSelfCheckRow> CheckHostedDeaths(
		WidgetOpsTap &tap,
		twidget::WidgetHost &host,
		const std::vector<std::string> &deaths) {
	const auto generation = ReadWidgetOpsLatest(tap).generation;
	const auto reported = int(deaths.size());
	const auto before = ReadWidgetOps(tap);
	host.onDeath(kTerminated);
	host.onDeath(kHostedDeath);
	const auto notified = ReadWidgetOps(tap);
	auto dying = twidget::WidgetDiagnostics();
	dying.dead = std::string(kHostedDeath);
	host.onChange(dying);
	const auto first = ReadWidgetOps(tap);
	dying.dead = std::string(kTerminated);
	host.onChange(dying);
	const auto later = ReadWidgetOps(tap);
	const auto got = std::vector<std::string>(
		deaths.begin() + reported,
		deaths.end());
	const auto expected = std::vector<std::string>{
		kTerminated,
		kHostedDeath,
	};
	const auto unrecorded = RunOf(notified, generation);
	const auto recorded = RunOf(first, generation);
	const auto kept = RunOf(later, generation);
	const auto rawDeath = [](const std::optional<WidgetHostedRun> &run) {
		return run
			&& (run->died == Text(kHostedDeath))
			&& !run->final;
	};
	return {
		{
			.name = u"hosted: every death the host reports reaches the "
				"runtime's own observer, one reading terminated too, and "
				"counts nothing: no charge comes from a raw notification"_q,
			.details = u"productDeaths=%1 before: %2 | notified: %3 | "
				"changed: %4"_q.arg(
					QString::number(int(got.size())),
					CountsText(before),
					CountsText(notified),
					CountsText(later)),
			.passed = (got == expected)
				&& Counts(before, 0, 0, 0, 0)
				&& (Charges(notified) == Charges(before))
				&& (Charges(later) == Charges(before)),
		},
		{
			.name = u"hosted: the host's first change that carries its "
				"death keeps it raw in its selection's record, a later one "
				"changes nothing, and neither makes the record final"_q,
			.details = u"before=%1 first=%2 later=%3"_q.arg(
				RunText(unrecorded),
				RunText(recorded),
				RunText(kept)),
			.passed = unrecorded
				&& !unrecorded->died
				&& !unrecorded->final
				&& rawDeath(recorded)
				&& rawDeath(kept),
		},
	};
}

std::vector<WidgetOpsSelfCheckRow> CheckHostedFinals(
		GlobalMsgId finals,
		GlobalMsgId evicted) {
	auto result = CheckFinals(finals);
	result.push_back(CheckEviction(evicted));
	result.push_back(CheckReadings());
	return result;
}

} // namespace Test::WidgetOps

namespace Test {

WidgetHostedDeath ReadHostedDeath(
		const WidgetOpsReading &reading,
		uint64 work,
		const QString &deathLine,
		const WidgetHostedDeath *earlier) {
	using namespace WidgetOps;
	auto result = WidgetHostedDeath{
		.deathLine = deathLine,
		.work = work,
	};
	const auto &runs = reading.hostedRuns;
	const auto i = work
		? ranges::find(runs, work, &WidgetHostedRun::work)
		: end(runs);
	if (i != end(runs)) {
		result.sampled = *i;
	}
	const auto &sampled = result.sampled;
	const auto charged = sampled
		&& sampled->final
		&& Tgw::ChargesCrash(sampled->outcome);
	result.died = WidgetTapCharged(reading) || charged;
	if (charged) {
		result.reason = (sampled->outcome == Outcome::Ordinary)
			? OrdinaryDiagnosis(deathLine, sampled->died)
			: sampled->reason;
	} else if (!sampled
		&& work
		&& earlier
		&& earlier->measured
		&& (earlier->work == work)) {
		result.reason = earlier->reason;
	}
	result.measured = !result.reason.isEmpty();
	return result;
}

QString WidgetHostedDeathText(const WidgetHostedDeath &death) {
	return death.measured
		? death.reason
		: death.died
		? u"charged, reason unmeasured"_q
		: QString();
}

bool WidgetTapCharged(const WidgetOpsReading &reading) {
	return (reading.fatalFrozen > 0) || (reading.desktopCharges > 0);
}

bool WidgetTapHosted(const WidgetOpsReading &reading) {
	return (reading.hostedSelected > 0);
}

WidgetHostedPlay::WidgetHostedPlay(
	std::shared_ptr<WidgetOpsTap> tap,
	GlobalMsgId id,
	uint64 work)
: _tap(std::move(tap))
, _deaths(std::make_shared<std::vector<std::string>>())
, _work(work) {
	if (!_tap) {
		return;
	}
	const auto before = ReadWidgetOps(*_tap).hostedSelected;
	_install = TapHostedWidget(id, work, twidget::WidgetTheme(), &_finish);
	_selected = _install
		&& _finish
		&& (ReadWidgetOps(*_tap).hostedSelected == before + 1);
}

uint64 WidgetHostedPlay::work() const {
	return _work;
}

bool WidgetHostedPlay::selected() const {
	return _selected;
}

bool WidgetHostedPlay::install() {
	using namespace WidgetOps;
	if (!_tap || !_install || _host) {
		return false;
	}
	try {
		_host = std::make_shared<twidget::WidgetHost>(
			std::string(kHostHtml),
			twidget::WidgetTheme(),
			kHostWidth,
			twidget::WidgetHostOptions(),
			std::shared_ptr<twidget::SandboxHostedChannel>());
	} catch (const std::exception &) {
		return false;
	}
	_host->onDeath = [deaths = _deaths](const std::string &reason) {
		deaths->push_back(reason);
	};
	const auto before = ReadWidgetOps(*_tap).wrappersBuilt;
	_install(_host);
	_installed = (ReadWidgetOps(*_tap).wrappersBuilt == before + 1);
	return _installed;
}

bool WidgetHostedPlay::change(const char *dead, bool rendered) {
	if (!_installed || !_host || !_host->onChange) {
		return false;
	}
	auto diagnostics = twidget::WidgetDiagnostics();
	diagnostics.rendered = rendered;
	if (dead) {
		diagnostics.dead = std::string(dead);
	}
	_host->onChange(diagnostics);
	return true;
}

bool WidgetHostedPlay::death(const char *reason) {
	if (!_installed || !_host || !_host->onDeath) {
		return false;
	}
	_host->onDeath(std::string(reason));
	return true;
}

std::vector<std::string> WidgetHostedPlay::deaths() const {
	return *_deaths;
}

std::optional<WidgetHostedRun> WidgetHostedPlay::record() const {
	if (!_tap) {
		return std::nullopt;
	}
	const auto reading = ReadWidgetOps(*_tap);
	const auto i = ranges::find(
		reading.hostedRuns,
		_work,
		&WidgetHostedRun::work);
	return (i != end(reading.hostedRuns))
		? std::make_optional(*i)
		: std::nullopt;
}

auto WidgetHostedPlay::observation() const
-> Fn<void(const Tgw::HostedDecision &decision)> {
	return _finish;
}

bool WidgetHostedPlay::finish(const Tgw::HostedDecision &decision) {
	const auto finish = base::take(_finish);
	if (!finish) {
		return false;
	}
	finish(decision);
	return true;
}

bool WidgetHostedPlay::drop() {
	if (!_tap) {
		return false;
	}
	const auto before = ReadWidgetOps(*_tap);
	const auto releases = _install ? 1 : 0;
	const auto destroys = (_installed && _host) ? 1 : 0;
	_install = nullptr;
	_host = nullptr;
	const auto after = ReadWidgetOps(*_tap);
	return (releases > 0)
		&& (after.factoriesReleased == before.factoriesReleased + releases)
		&& (after.wrappersDestroyed == before.wrappersDestroyed + destroys);
}

QString WidgetHostedOutcomeText(Tgw::HostedOutcome outcome) {
	switch (outcome) {
	case Tgw::HostedOutcome::None: return u"none"_q;
	case Tgw::HostedOutcome::Spared: return u"spared"_q;
	case Tgw::HostedOutcome::Ordinary: return u"ordinary"_q;
	case Tgw::HostedOutcome::Stalled: return u"stalled"_q;
	case Tgw::HostedOutcome::Refused: return u"refused"_q;
	}
	return u"unknown"_q;
}

QString WidgetHostedRunText(const WidgetHostedRun &run) {
	return u"%1:%2/%3%4%5 work=%6 reason=[%7] died=[%8]"_q.arg(
		QString::number(run.generation),
		WidgetHostedOutcomeText(run.outcome),
		WidgetHostedOutcomeText(run.transport),
		run.claimed ? u"/claimed"_q : QString(),
		run.final ? QString() : u"/open"_q,
		QString::number(run.work),
		run.reason,
		run.died.value_or(u"-"_q));
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
