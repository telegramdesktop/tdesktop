/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_conformance_death.h"

#include "test/test_widget_conformance_state.h"
#include "test/test_widget_conformance_stop.h"
#include "test/test_widget_fixture.h"
#include "tgw/tgw_renderer.h"

#include <QtCore/QStringList>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Test::Conformance {
namespace {

using Outcome = Tgw::HostedOutcome;

// The session of the messages the checks arm their taps for, apart from
// the ops self-check's and the attack audit's, and work ids no
// RendererCore is given (theirs count up from 1).
constexpr auto kCheckSession = uint64(4);
constexpr auto kCheckWork = std::numeric_limits<uint64>::max() - 1000;

// The texts of the checks: the end of the widget helper, which every host
// it served reports alike, spared or charged, and the line of a summary
// that carries it; StalledReason(), an ordinary death's label, one of
// Telegram's own refusals, a paint bound's stop as the run's record and as
// the desktop's own reading of it, and the stops entry the runs belong to.
constexpr auto kHelperEnded = "the widget engine process ended";
constexpr auto kHelperLine = "Died: the widget engine process ended";
constexpr auto kStalledReason = "The widget stopped responding";
constexpr auto kStoppedLabel = "Widget stopped";
constexpr auto kSkippedFrame
	= "The widget renderer skipped a frame asked to paint";
constexpr auto kPaintLimit = "Paint limit exceeded: a control reason";
constexpr auto kDesktopStop = "Paint limit exceeded: the desktop's reading";
constexpr auto kStops = "Text limit exceeded";

// The denial StopRefusal() never makes when its separate reading of the
// desktop's own stop found none, as the desktop's charge of a selected
// work can still be one; the clause it puts in its refusal then instead;
// and the one saying that the run's sample gave no cause.
constexpr auto kNoDesktopStop = "the desktop did not stop it itself";
constexpr auto kNoDesktopStopObserved
	= "no stop of the desktop's own was observed apart from it";
constexpr auto kNoAttributableCause = "its sample gave no attributable cause";

// What a run's death reads as at one stage: its run file's dead (empty
// while it is alive), which its totals' dead list entry also has after
// "<key>: ", whether the dead is measured and whether its stops entry
// refused it as stop-reason-unmeasured.
struct Expected {
	QString dead;
	bool measured = false;
	bool refused = false;
};

// The hosted outcome of the checks' decisions, as the runtime decides it.
[[nodiscard]] Tgw::HostedDecision Stalled() {
	return {
		.reason = QString::fromLatin1(kStalledReason),
		.outcome = Outcome::Stalled,
		.transport = Outcome::Stalled,
	};
}

[[nodiscard]] Tgw::HostedDecision Ordinary() {
	return {
		.reason = QString::fromLatin1(kStoppedLabel),
		.outcome = Outcome::Ordinary,
		.transport = Outcome::Ordinary,
	};
}

[[nodiscard]] Tgw::HostedDecision Spared() {
	return {
		.reason = QString::fromLatin1(kStoppedLabel),
		.outcome = Outcome::Spared,
		.transport = Outcome::Spared,
	};
}

[[nodiscard]] Tgw::HostedDecision Refused(
		const char *reason,
		Outcome transport) {
	return {
		.reason = QString::fromLatin1(reason),
		.outcome = Outcome::Refused,
		.transport = transport,
	};
}

// The final reading of a hosted run's death: |death|, the shared per-work
// reading, decides `died` alone and gives `dead` only its measured cause,
// else the placeholder. A stop the desktop read gives a hosted run
// nothing: its cause is the sampled work's own record or none.
void JudgeHosted(ConformanceRun &run, WidgetHostedDeath death) {
	run.died = death.died;
	run.deathMeasured = death.measured;
	run.dead = death.measured
		? death.reason
		: death.died
		? DeathPlaceholder(true)
		: QString();
	run.hostedDeath = std::move(death);
}

// ReadHostedDeath() of |reading| for the summary |run|'s log stage took,
// only while that summary is the one of the host the log was requested
// for: a stale or absent one keeps its raw line and work in the run but
// takes no record's cause.
[[nodiscard]] WidgetHostedDeath ReadSampledDeath(
		const ConformanceRun &run,
		const WidgetOpsReading &reading,
		const WidgetHostedDeath *earlier) {
	return run.summaryOwn
		? ReadHostedDeath(reading, run.summaryWork, run.deathLine, earlier)
		: ReadHostedDeath(reading, 0, QString(), earlier);
}

[[nodiscard]] GlobalMsgId CheckId(int index) {
	return { FullMsgId(PeerId(), MsgId(-index)), kCheckSession };
}

// A summary sampled from |work|, its first line |line| (a healthy one for
// null), read as |state| says.
[[nodiscard]] WidgetRuntimeLog Sample(
		uint64 work,
		const char *line,
		WidgetLogSummary state = WidgetLogSummary::Fresh) {
	const auto loaded = u"Loaded: yes"_q;
	return {
		.summary = line
			? (QString::fromLatin1(line) + QChar('\n') + loaded)
			: loaded,
		.summaryState = state,
		.summaryWork = work,
	};
}

// RecordLog()'s death steps for a run of the stops entry kStops: ReadDeath()
// of |log| and the tap's reading now, then its refusal.
[[nodiscard]] ConformanceRun Logged(
		const WidgetOpsTap &tap,
		const WidgetRuntimeLog &log,
		const QString &desktopStop = QString()) {
	auto result = ConformanceRun{
		.id = u"auto/death"_q,
		.set = u"auto"_q,
		.theme = u"light"_q,
		.key = u"auto/death.light"_q,
	};
	ReadDeath(result, log, ReadWidgetOps(tap), desktopStop);
	RefuseUnmeasuredStop(result, QString::fromLatin1(kStops));
	return result;
}

// FinalizeRun()'s: the tap's reading after the removal, whether it is
// final, then the death reconciled with it.
void Finalize(ConformanceRun &run, const WidgetOpsTap &tap) {
	run.reading = ReadWidgetOps(tap);
	run.readingFinal = WidgetTapFinished(run.reading);
	ReconcileDeath(run, QString::fromLatin1(kStops));
}

[[nodiscard]] int Unmeasured(const ConformanceRun &run) {
	const auto name = QString::fromLatin1(kStopReasonUnmeasured);
	return int(std::count_if(
		begin(run.refusals),
		end(run.refusals),
		[&](const RunnerRefusal &refusal) { return (refusal.name == name); }));
}

// Whether every stop-reason-unmeasured refusal of |run| tells what the run
// read: the desktop's own stop named when one was read (`desktopStop`);
// when none was, only unobserved, never denied, as a charge of the
// desktop's may still stand; and that its sample gave no attributable
// cause.
[[nodiscard]] bool RefusalTold(const ConformanceRun &run) {
	const auto name = QString::fromLatin1(kStopReasonUnmeasured);
	const auto denial = QString::fromLatin1(kNoDesktopStop);
	const auto unobserved = QString::fromLatin1(kNoDesktopStopObserved);
	const auto uncaused = QString::fromLatin1(kNoAttributableCause);
	return std::all_of(
		begin(run.refusals),
		end(run.refusals),
		[&](const RunnerRefusal &refusal) {
			if (refusal.name != name) {
				return true;
			}
			const auto &detail = refusal.detail;
			const auto denied = detail.contains(denial);
			const auto missed = detail.contains(unobserved);
			const auto stop = !run.desktopStop.isEmpty()
				? (!missed && detail.contains(run.desktopStop))
				: missed;
			return stop && !denied && detail.contains(uncaused);
		});
}

// The run file's dead and the totals' entry agree with |expected|, each
// on its own, and its refusal tells what it read (RefusalTold()).
[[nodiscard]] bool Reads(
		const ConformanceRun &run,
		const Expected &expected) {
	const auto alive = expected.dead.isEmpty();
	const auto value = alive
		? QJsonValue(QJsonValue::Null)
		: QJsonValue(expected.dead);
	const auto entry = alive
		? QString()
		: (run.key + u": "_q + expected.dead);
	return (run.died == !alive)
		&& (run.deathMeasured == expected.measured)
		&& (DeadValue(run) == value)
		&& (DeadEntry(run) == entry)
		&& (Unmeasured(run) == (expected.refused ? 1 : 0))
		&& RefusalTold(run);
}

[[nodiscard]] QString RunText(const ConformanceRun &run) {
	auto refusals = QStringList();
	for (const auto &refusal : run.refusals) {
		refusals.push_back(refusal.name + u" ("_q + refusal.detail + u")"_q);
	}
	const auto sampled = SampledRunValue(run);
	return u"died=%1 dead=[%2] measured=%3 line=[%4] work=%5 own=%6 "
		"final=%7 sampled=[%8] refusals=[%9]"_q.arg(
			run.died ? u"1"_q : u"0"_q,
			run.dead,
			run.deathMeasured ? u"1"_q : u"0"_q,
			run.deathLine,
			QString::number(run.summaryWork),
			run.summaryOwn ? u"1"_q : u"0"_q,
			run.readingFinal ? u"1"_q : u"0"_q,
			sampled.isNull() ? u"none"_q : sampled.toString(),
			refusals.join(u","_q))
		+ u" entry=[%1]"_q.arg(DeadEntry(run));
}

[[nodiscard]] QString RecordText(const std::optional<WidgetHostedRun> &run) {
	return run ? WidgetHostedRunText(*run) : u"none"_q;
}

// The checks' cases, each one's readings joined into its row's details.
class Cases final {
public:
	void add(
		const char *name,
		const ConformanceRun &run,
		const Expected &expected);
	void add(const char *name, bool passed, const QString &details);

	[[nodiscard]] WidgetOpsSelfCheckRow row(const QString &name) const;

private:
	QStringList _readings;
	bool _passed = true;

};

void Cases::add(
		const char *name,
		const ConformanceRun &run,
		const Expected &expected) {
	add(name, Reads(run, expected), RunText(run));
}

void Cases::add(const char *name, bool passed, const QString &details) {
	_passed = _passed && passed;
	_readings.push_back(u"{%1: %2%3}"_q.arg(
		QString::fromLatin1(name),
		passed ? QString() : u"FAILED "_q,
		details));
}

WidgetOpsSelfCheckRow Cases::row(const QString &name) const {
	return {
		.name = name,
		.details = _readings.join(QChar(' ')),
		.passed = _passed,
	};
}

// Two works of one message printing the same "Died: " line, the older one
// spared and its replacement charged with an ordinary death, each played
// through the tap's installed observers, which must have kept each
// host's raw death in that work's own record before any verdict counts;
// each sampled in turn, and a stale summary of that line. Then a spared
// sample beside a work withdrawn first, nothing charged.
[[nodiscard]] WidgetOpsSelfCheckRow CheckAttribution() {
	auto cases = Cases();
	const auto unmeasured = WidgetHostedDeathText({ .died = true });
	const auto line = QString::fromLatin1(kHelperLine);
	const auto helperEnded = QString::fromLatin1(kHelperEnded);

	const auto id = CheckId(1);
	const auto tap = ArmWidgetOpsTap(id);
	auto spared = WidgetHostedPlay(tap, id, kCheckWork);
	auto charged = WidgetHostedPlay(tap, id, kCheckWork - 1);
	const auto selected = spared.selected() && charged.selected();
	const auto sparedBuilt = spared.install();
	const auto chargedBuilt = charged.install();
	spared.change(kHelperEnded);
	charged.change(kHelperEnded);
	const auto sparedRecord = spared.record();
	const auto chargedRecord = charged.record();
	const auto observed = sparedRecord
		&& chargedRecord
		&& (sparedRecord->work != chargedRecord->work)
		&& (sparedRecord->died == helperEnded)
		&& (chargedRecord->died == helperEnded);
	spared.finish(Spared());
	charged.finish(Ordinary());
	const auto sparedDropped = spared.drop();
	const auto chargedDropped = charged.drop();
	cases.add(
		"both works played and their raw deaths observed",
		selected
			&& sparedBuilt
			&& chargedBuilt
			&& observed
			&& sparedDropped
			&& chargedDropped,
		u"selected=%1 built=%2/%3 dropped=%4/%5 spared=[%6] "
		"charged=[%7]"_q.arg(
			selected ? u"1"_q : u"0"_q,
			sparedBuilt ? u"1"_q : u"0"_q,
			chargedBuilt ? u"1"_q : u"0"_q,
			sparedDropped ? u"1"_q : u"0"_q,
			chargedDropped ? u"1"_q : u"0"_q,
			RecordText(sparedRecord),
			RecordText(chargedRecord)));
	auto ofSpared = Logged(*tap, Sample(spared.work(), kHelperLine));
	auto ofCharged = Logged(*tap, Sample(charged.work(), kHelperLine));
	auto stale = Logged(*tap, Sample(
		charged.work(),
		kHelperLine,
		WidgetLogSummary::Stale));
	for (auto run : { &ofSpared, &ofCharged, &stale }) {
		Finalize(*run, *tap);
	}
	DisarmWidgetOpsTap(id);
	cases.add("spared sample", ofSpared, {
		.dead = unmeasured,
		.refused = true,
	});
	cases.add("charged replacement sample", ofCharged, {
		.dead = helperEnded,
		.measured = true,
	});
	cases.add("stale summary of the charged work", stale, {
		.dead = unmeasured,
		.refused = true,
	});
	cases.add(
		"raw lines and sampled works kept, a stale one not its own",
		(ofSpared.deathLine == line)
			&& (ofCharged.deathLine == line)
			&& (stale.deathLine == line)
			&& ofSpared.readingFinal
			&& ofSpared.summaryOwn
			&& ofCharged.summaryOwn
			&& !stale.summaryOwn
			&& (ofSpared.summaryWork == spared.work())
			&& (ofCharged.summaryWork == charged.work())
			&& (stale.summaryWork == charged.work()),
		u"spared=%1 charged=%2"_q.arg(
			QString::number(spared.work()),
			QString::number(charged.work())));

	const auto collateralId = CheckId(2);
	const auto collateralTap = ArmWidgetOpsTap(collateralId);
	auto healthy = WidgetHostedPlay(
		collateralTap,
		collateralId,
		kCheckWork - 2);
	auto withdrawn = WidgetHostedPlay(
		collateralTap,
		collateralId,
		kCheckWork - 3);
	const auto healthyBuilt = healthy.selected() && healthy.install();
	healthy.change(kHelperEnded);
	const auto healthyRecord = healthy.record();
	healthy.finish(Spared());
	withdrawn.finish(Tgw::HostedDecision());
	const auto healthyDropped = healthy.drop();
	const auto withdrawnDropped = withdrawn.drop();
	auto collateral = Logged(
		*collateralTap,
		Sample(healthy.work(), kHelperLine));
	Finalize(collateral, *collateralTap);
	DisarmWidgetOpsTap(collateralId);
	cases.add(
		"collateral work played and its raw death observed",
		healthyBuilt
			&& withdrawn.selected()
			&& healthyRecord
			&& (healthyRecord->died == helperEnded)
			&& healthyDropped
			&& withdrawnDropped,
		RecordText(healthyRecord));
	cases.add("healthy collateral sample", collateral, {});
	cases.add(
		"collateral raw line kept",
		(collateral.deathLine == line) && collateral.readingFinal,
		collateral.deathLine);
	return cases.row(u"death reading self-check: two works played through "
		u"the tap's installed observers, each keeping the raw \"Died: \" "
		u"text its own host reported, a spared one and its charged "
		u"replacement, give a dead run whichever is sampled, measured only "
		u"from the charged work's own sample and refused as "
		u"stop-reason-unmeasured for the spared one's or a stale summary, "
		u"whose raw line and work are kept apart from its eligibility; a "
		u"spared sample beside a work withdrawn first stays alive with its "
		u"raw line kept"_q);
}

// A work of the message |index|, installed and sampled healthy at the log
// stage, then dropped, its final left deliberately last at the barrier,
// then given as |decision|, and a copy of its observation called again
// with an ordinary death, which the tap's own guard stops; the final
// reading of its run is the case |name|.
void PlayLateCharge(
		Cases &cases,
		int index,
		const Tgw::HostedDecision &decision,
		const char *name,
		const Expected &expected) {
	const auto id = CheckId(index);
	const auto tap = ArmWidgetOpsTap(id);
	auto played = WidgetHostedPlay(tap, id, kCheckWork - 10 - index);
	const auto built = played.selected() && played.install();
	auto run = Logged(*tap, Sample(played.work(), nullptr));
	cases.add(
		"logged healthy, alive",
		built && Reads(run, {}) && run.deathLine.isEmpty(),
		RunText(run));
	const auto dropped = played.drop();
	Finalize(run, *tap);
	cases.add(
		"final left last, the reading incomplete and alive",
		dropped && !run.readingFinal && Reads(run, {}),
		RunText(run) + u" tap: "_q + WidgetTapCounts(run.reading));
	const auto again = played.observation();
	played.finish(decision);
	if (again) {
		again(Ordinary());
	}
	Finalize(run, *tap);
	DisarmWidgetOpsTap(id);
	const auto &sampled = run.hostedDeath.sampled;
	cases.add(
		name,
		run.readingFinal
			&& (run.reading.hostedFinal == 1)
			&& !run.reading.fatalFrozen
			&& sampled
			&& (sampled->transport == Outcome::Spared)
			&& Reads(run, expected),
		RunText(run) + u" tap: "_q + WidgetTapCounts(run.reading));
}

// A run sampled healthy at the log stage reads alive and incomplete while
// its final is left last, then a refusal Telegram made of its frame on a
// route the stream spared, against a valid reply that leaves it spared.
[[nodiscard]] WidgetOpsSelfCheckRow CheckLateCharge() {
	auto cases = Cases();
	PlayLateCharge(
		cases,
		3,
		Refused(kSkippedFrame, Outcome::Spared),
		"refusal on a spared route, final",
		{ .dead = QString::fromLatin1(kSkippedFrame), .measured = true });
	PlayLateCharge(
		cases,
		4,
		Spared(),
		"valid reply on a spared route, final",
		{});
	return cases.row(u"death reading self-check: a run sampled healthy at "
		u"the log stage reads alive and incomplete at the barrier while its "
		u"final is left last, then dies, measured, of a refusal Telegram "
		u"made of its frame on a route the stream spared, and stays alive "
		u"after a valid reply on that route; a second call of a copy of "
		u"its final observation, which the tap's own guard stops, charges "
		u"nothing"_q);
}

// The stop-reason-unmeasured refusal following the final reading: a
// charge of an older work at the log stage, then the sampled
// replacement's own refusal, measured; and a log stage with nothing
// charged, then a sibling's charge beside the spared sample.
[[nodiscard]] WidgetOpsSelfCheckRow CheckStopRefusal() {
	auto cases = Cases();
	const auto unmeasured = WidgetHostedDeathText({ .died = true });

	const auto droppedId = CheckId(5);
	const auto droppedTap = ArmWidgetOpsTap(droppedId);
	auto older = WidgetHostedPlay(droppedTap, droppedId, kCheckWork - 20);
	const auto olderSelected = older.selected();
	const auto olderFinished = older.finish(Stalled());
	const auto olderDropped = older.drop();
	auto replacement = WidgetHostedPlay(
		droppedTap,
		droppedId,
		kCheckWork - 21);
	const auto replacementBuilt = replacement.selected()
		&& replacement.install();
	auto dropped = Logged(
		*droppedTap,
		Sample(replacement.work(), nullptr));
	cases.add("older work charged at the log stage", dropped, {
		.dead = unmeasured,
		.refused = true,
	});
	replacement.finish(Refused(kSkippedFrame, Outcome::None));
	const auto replacementDropped = replacement.drop();
	auto played = olderSelected
		&& olderFinished
		&& olderDropped
		&& replacementBuilt
		&& replacementDropped;
	Finalize(dropped, *droppedTap);
	DisarmWidgetOpsTap(droppedId);
	cases.add("the sample's own claimed refusal, final", dropped, {
		.dead = QString::fromLatin1(kSkippedFrame),
		.measured = true,
	});

	const auto addedId = CheckId(6);
	const auto addedTap = ArmWidgetOpsTap(addedId);
	auto sampled = WidgetHostedPlay(addedTap, addedId, kCheckWork - 22);
	auto sibling = WidgetHostedPlay(addedTap, addedId, kCheckWork - 23);
	const auto sampledBuilt = sampled.selected() && sampled.install();
	played = played && sampledBuilt && sibling.selected();
	auto added = Logged(*addedTap, Sample(sampled.work(), nullptr));
	cases.add("nothing charged at the log stage", added, {});
	sampled.finish(Spared());
	sibling.finish(Ordinary());
	const auto sampledDropped = sampled.drop();
	const auto siblingDropped = sibling.drop();
	played = sampledDropped && siblingDropped && played;
	Finalize(added, *addedTap);
	DisarmWidgetOpsTap(addedId);
	cases.add("a sibling charged beside the spared sample, final", added, {
		.dead = unmeasured,
		.refused = true,
	});
	cases.add("every work played", played, QString());
	return cases.row(u"death reading self-check: the barrier drops the "
		u"stop-reason-unmeasured refusal of the log stage once the sampled "
		u"work's own refusal measures the death, and adds it once a "
		u"sibling's charge kills a run whose sample was spared"_q);
}

// A charged first selection, then kWidgetHostedRuns + 1 later uncharged
// ones, read by a log stage before the eviction and by one after it, for
// the first work and for the last.
[[nodiscard]] WidgetOpsSelfCheckRow CheckEviction() {
	auto cases = Cases();
	const auto unmeasured = WidgetHostedDeathText({ .died = true });
	const auto id = CheckId(7);
	const auto tap = ArmWidgetOpsTap(id);
	auto first = WidgetHostedPlay(tap, id, kCheckWork - 100);
	const auto firstSelected = first.selected();
	const auto firstFinished = first.finish(Stalled());
	const auto firstDropped = first.drop();
	auto played = firstSelected && firstFinished && firstDropped;
	auto early = Logged(*tap, Sample(first.work(), kHelperLine));
	auto last = uint64();
	for (auto i = 1; i <= kWidgetHostedRuns + 1; ++i) {
		auto later = WidgetHostedPlay(tap, id, first.work() - i);
		const auto chosen = later.selected();
		const auto finished = later.finish((i % 2)
			? Spared()
			: Tgw::HostedDecision());
		const auto dropped = later.drop();
		played = played && chosen && finished && dropped;
		last = later.work();
	}
	auto late = Logged(*tap, Sample(first.work(), kHelperLine));
	auto newest = Logged(*tap, Sample(last, kHelperLine));
	for (auto run : { &early, &late, &newest }) {
		Finalize(*run, *tap);
	}
	DisarmWidgetOpsTap(id);
	const auto &reading = early.reading;
	const auto evicted = std::none_of(
		begin(reading.hostedRuns),
		end(reading.hostedRuns),
		[&](const WidgetHostedRun &run) { return (run.work == first.work()); });
	cases.add(
		"first work evicted, its charge counted",
		played
			&& evicted
			&& (int(reading.hostedRuns.size()) == kWidgetHostedRuns)
			&& (reading.fatalFrozen == 1)
			&& (reading.hostedFinal == kWidgetHostedRuns + 2)
			&& early.readingFinal,
		WidgetTapCounts(reading)
			+ u" fatal=%1"_q.arg(QString::number(reading.fatalFrozen)));
	cases.add("measured before the eviction", early, {
		.dead = QString::fromLatin1(kStalledReason),
		.measured = true,
	});
	cases.add("first sampled after the eviction", late, {
		.dead = unmeasured,
		.refused = true,
	});
	cases.add("spared newest sampled, same line", newest, {
		.dead = unmeasured,
		.refused = true,
	});
	return cases.row(u"death reading self-check: a charged first selection "
		u"evicted from the records by 33 later uncharged ones still kills "
		u"the run through the cumulative charge, measured only when its "
		u"record was read before the eviction, unmeasured and refused as "
		u"stop-reason-unmeasured when it was sampled after it or when a "
		u"spared later work printing the same line was"_q);
}

// A hosted work Telegram refused itself, its one desktop charge final,
// the desktop's own stop read apart for the run as |desktopStop| (empty:
// none observed), read at the log stage and again at the barrier from
// |log|: the case |name|.
void PlaySample(
		Cases &cases,
		const WidgetOpsTap &tap,
		const char *name,
		const WidgetRuntimeLog &log,
		const Expected &expected,
		const char *desktopStop = kDesktopStop) {
	auto run = Logged(tap, log, QString::fromLatin1(desktopStop));
	const auto logged = Reads(run, expected);
	const auto loggedText = RunText(run);
	Finalize(run, tap);
	cases.add(
		name,
		logged
			&& run.readingFinal
			&& (run.reading.desktopCharges == 1)
			&& Reads(run, expected)
			&& (run.summaryWork == log.summaryWork)
			&& (run.desktopStop == QString::fromLatin1(desktopStop)),
		u"log: %1 | final: %2"_q.arg(loggedText, RunText(run)));
}

// The eligibility of a hosted sample: the work's own fresh summary
// measures its recorded refusal, never the desktop's reading; a stale
// summary of that very work, its raw line and work kept, and an absent one
// leave the charge unmeasured and refused at the log stage and at the
// barrier alike, the refusal saying that the sample gave no attributable
// cause and naming the desktop's stop the run read apart (RefusalTold()),
// never that the desktop did not stop it. With no desktop stop read apart,
// at a stale or absent sample and at a barrier whose log stage was
// skipped, the desktop's final charge still stands and the refusal says
// only that no stop of the desktop's own was observed.
[[nodiscard]] WidgetOpsSelfCheckRow CheckSamples() {
	auto cases = Cases();
	const auto unmeasured = WidgetHostedDeathText({ .died = true });
	const auto id = CheckId(8);
	const auto tap = ArmWidgetOpsTap(id);
	auto played = WidgetHostedPlay(tap, id, kCheckWork - 200);
	const auto built = played.selected() && played.install();
	played.finish(Refused(kPaintLimit, Outcome::None));
	const auto dropped = played.drop();
	cases.add(
		"refused work played",
		built && dropped,
		RecordText(played.record()));
	const auto measured = Expected{
		.dead = QString::fromLatin1(kPaintLimit),
		.measured = true,
	};
	const auto charged = Expected{ .dead = unmeasured, .refused = true };
	const auto work = played.work();
	const auto staleLog = Sample(
		work,
		kHelperLine,
		WidgetLogSummary::Stale);
	PlaySample(
		cases,
		*tap,
		"its own fresh summary",
		Sample(work, nullptr),
		measured);
	PlaySample(
		cases,
		*tap,
		"its own final summary",
		Sample(work, nullptr, WidgetLogSummary::Final),
		measured);
	PlaySample(cases, *tap, "a stale summary of it", staleLog, charged);
	PlaySample(cases, *tap, "no summary", WidgetRuntimeLog(), charged);
	PlaySample(
		cases,
		*tap,
		"a stale summary of it, no desktop stop observed",
		staleLog,
		charged,
		"");
	PlaySample(
		cases,
		*tap,
		"no summary, no desktop stop observed",
		WidgetRuntimeLog(),
		charged,
		"");
	auto skipped = ConformanceRun{
		.id = u"auto/death"_q,
		.set = u"auto"_q,
		.theme = u"light"_q,
		.key = u"auto/death.light"_q,
	};
	Finalize(skipped, *tap);
	cases.add(
		"its log stage skipped, final",
		skipped.readingFinal
			&& (skipped.reading.desktopCharges == 1)
			&& Reads(skipped, charged)
			&& skipped.deathLine.isEmpty()
			&& !skipped.summaryWork
			&& skipped.desktopStop.isEmpty(),
		RunText(skipped));
	const auto stale = Logged(*tap, staleLog);
	DisarmWidgetOpsTap(id);
	cases.add(
		"the stale raw line and work kept, not its own",
		(stale.deathLine == QString::fromLatin1(kHelperLine))
			&& (stale.summaryWork == work)
			&& !stale.summaryOwn
			&& !stale.hostedDeath.sampled,
		RunText(stale));
	return cases.row(u"death reading self-check: a hosted charge takes a "
		u"cause only from the record of the work a fresh or final summary "
		u"was sampled from, never from the desktop's own reading of a "
		u"stop; a stale or absent sample leaves it charged and unmeasured, "
		u"refused as stop-reason-unmeasured at the log stage and at the "
		u"barrier with a detail saying the sample gave no attributable "
		u"cause and naming the desktop's stop it read apart, or with none "
		u"read apart, also after a skipped log stage, saying only that none "
		u"was observed beside the desktop's final charge, never that the "
		u"desktop did not stop it; the stale line and its work kept"_q);
}

} // namespace

QString DeathPlaceholder(bool charged) {
	return charged
		? WidgetHostedDeathText({ .died = true })
		: u"the runtime stopped the widget"_q;
}

void ReadDeath(
		ConformanceRun &run,
		const WidgetRuntimeLog &log,
		const WidgetOpsReading &reading,
		const QString &desktopStop) {
	const auto first = log.summary.section(QChar('\n'), 0, 0);
	const auto line = first.startsWith(u"Died: "_q) ? first : QString();
	run.summaryWork = log.summaryWork;
	run.summaryOwn = WidgetLogSummaryOwn(log);
	run.desktopStop = desktopStop;
	if (WidgetTapHosted(reading)) {
		run.deathLine = line;
		JudgeHosted(run, ReadSampledDeath(run, reading, nullptr));
		return;
	}
	run.deathLine = run.summaryOwn ? line : QString();
}

void RefuseUnmeasuredStop(ConformanceRun &run, const QString &stops) {
	const auto name = QString::fromLatin1(kStopReasonUnmeasured);
	std::erase_if(run.refusals, [&](const RunnerRefusal &refusal) {
		return (refusal.name == name);
	});
	auto refusal = StopRefusal(stops, run);
	if (!refusal.name.isEmpty()) {
		run.refusals.push_back(std::move(refusal));
	}
}

void ReconcileDeath(ConformanceRun &run, const QString &stops) {
	if (!WidgetTapHosted(run.reading)) {
		return;
	}
	const auto earlier = run.hostedDeath;
	JudgeHosted(run, ReadSampledDeath(run, run.reading, &earlier));
	RefuseUnmeasuredStop(run, stops);
}

QJsonValue DeadValue(const ConformanceRun &run) {
	return !run.died
		? QJsonValue(QJsonValue::Null)
		: run.dead.isEmpty()
		? QJsonValue(u"died, reason not observed"_q)
		: QJsonValue(run.dead);
}

QString DeadEntry(const ConformanceRun &run) {
	return run.died ? (run.key + u": "_q + run.dead) : QString();
}

QJsonValue SampledRunValue(const ConformanceRun &run) {
	const auto &sampled = run.hostedDeath.sampled;
	return sampled
		? QJsonValue(WidgetHostedRunText(*sampled))
		: QJsonValue(QJsonValue::Null);
}

std::vector<WidgetOpsSelfCheckRow> RunDeathReadingCheck() {
	return {
		CheckAttribution(),
		CheckLateCharge(),
		CheckStopRefusal(),
		CheckEviction(),
		CheckSamples(),
	};
}

} // namespace Test::Conformance

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
