/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "test/test_widget_ops.h"

#include <QtCore/QJsonValue>
#include <QtCore/QString>

#include <vector>

// The widget conformance runner's reading of a run's death: the "Died: "
// line of the summary its log stage took, kept raw for a hosted reading
// (locally only an own summary's, as ever), with the work that summary was
// sampled from and whether it was the summary of the host the log was
// requested for. For a widget whose tap selected hosted works
// (WidgetTapHosted()), the shared hosted reading (ReadHostedDeath()) of
// the tap's counts, taken at the log stage and again through the final
// barrier, so that a run dies only of a charge of its selected works and
// its cause is measured only from the record of a work its own summary was
// sampled from. Internal to test_widget_conformance.cpp; reference:
// docs/helpers/test_widget_conformance.md.

namespace Test {
struct WidgetRuntimeLog;
} // namespace Test

namespace Test::Conformance {

struct ConformanceRun;

// The dead of a run that died with no measured reason: locally "the host
// sent a fatal frozen notice" once the tap counted one (|charged|), else
// "the runtime stopped the widget"; |hosted|, "charged, reason unmeasured"
// (WidgetHostedDeathText()) once a selected work was charged.
[[nodiscard]] QString DeathPlaceholder(bool charged, bool hosted);

// The log stage's reading of |run|'s death from the runtime |log| and the
// tap's |reading| then. Keeps the work the summary was sampled from in
// `summaryWork`, whatever its state, `summaryOwn` for a fresh or final one
// (WidgetLogSummaryOwn()), the only kind whose line or work can give a
// cause, and |desktopStop| (ReadDesktopStop()) in `desktopStop`. Locally
// as ever: `deathLine` is an own summary's "Died: " line alone, never a
// stale one's, which may be an older host's; a death the tap counted
// kills a run still alive with the placeholder; that line other than
// "terminated" is the death, its reason measured unless empty (dead then
// empty: "died, reason not observed" in the run file); else a desktop stop
// is the measured dead. For a hosted reading (`deathHosted`) `deathLine`
// keeps the summary's "Died: " line verbatim whatever its state, and
// ReadHostedDeath() of |reading| for an own summary (for none else), kept
// in `hostedDeath`, decides `died` alone, and `dead` is its measured
// cause, else the placeholder: a desktop stop gives a hosted run nothing.
void ReadDeath(
	ConformanceRun &run,
	const WidgetRuntimeLog &log,
	const WidgetOpsReading &reading,
	const QString &desktopStop);

// The stop-reason-unmeasured refusal of a run of the stops entry |stops|
// as its death stands now (StopRefusal(), whose detail tells the run's
// death line, a hosted sample's missing cause and the desktop's own stop
// as the run read them), in place of one it got before.
void RefuseUnmeasuredStop(ConformanceRun &run, const QString &stops);

// At the final barrier, with `reading` the run's last tap reading, for
// every run, its log stage read or skipped: when that reading is hosted,
// its death read again for the same own sample (none for a skipped log
// stage), the log stage's reading standing for a record evicted since, so
// a charge after a healthy sample kills it, measured when it is the
// sampled work's own and unmeasured otherwise, and a run is alive when no
// selected work was charged; its dead and its refusal for |stops| follow.
// Locally nothing changes.
void ReconcileDeath(ConformanceRun &run, const QString &stops);

// The run file's `dead` of |run|: null while it is alive.
[[nodiscard]] QJsonValue DeadValue(const ConformanceRun &run);

// The entry of |run| in the totals' dead list, empty while it is alive:
// "<key>: <dead>", as the totals always wrote it, so a local death with an
// empty reason ends in ": " where its run file says "died, reason not
// observed"; a hosted death's dead is its final measured cause or the
// placeholder, never empty.
[[nodiscard]] QString DeadEntry(const ConformanceRun &run);

// The record of the work |run|'s hosted death was read for, as the run's
// log row gives it (WidgetHostedRunText()), null when the reading kept
// none.
[[nodiscard]] QJsonValue SampledRunValue(const ConformanceRun &run);

// The death reading's controls, each through a tap of its own armed for a
// message of no history, its works played as WidgetHostedPlay plays them,
// then read by ReadDeath(), RefuseUnmeasuredStop(), ReconcileDeath(),
// DeadValue() and DeadEntry() as the log stage, the final barrier, the run
// file and the totals read them. Disarms every tap. Row names carry no
// prefix.
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> RunDeathReadingCheck();

} // namespace Test::Conformance
