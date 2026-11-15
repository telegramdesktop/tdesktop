/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "test/test_widget_ops.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <optional>
#include <vector>

// The widget conformance runner's reading of what one widget host
// recorded: the bridge calls it accepted after its gesture gate and the
// uncaught errors its console got, parsed from the raw text Summarize() in
// tgw/tgw_runtime.cpp leaves in Tgw::Runtime::_summary, never from the
// outer Runtime::logText(); and what the run file and the totals may claim
// from a run's readings, so an observation that was not made never reads
// as zero. Internal to test_widget_conformance.cpp; reference:
// docs/helpers/test_widget_conformance.md.

namespace Test {

struct WidgetRuntimeLog;

// One accepted bridge call; |args| is a JSON array by type.
struct ConformanceBridgeCall {
	QString method;
	QJsonArray args;
};

// A named runner refusal and the values that decided it.
struct ConformanceRefusal {
	QString name;
	QString detail;
};

// |bridge| holds every accepted call in order and |errors| every "Uncaught "
// console error, each set only when its section was found and read without
// ambiguity; otherwise it stays unset and |refusals| names why. Section
// counts are -1 while their header is not found. The console's limits are
// readings, not refusals: |consoleOmitted| earlier entries not shown,
// |consoleDropped| for a host that kept no later entries, |errorsClipped|
// texts the formatter cut at 240 characters.
struct ConformanceSummary {
	std::optional<std::vector<ConformanceBridgeCall>> bridge;
	std::optional<QStringList> errors;
	std::vector<ConformanceRefusal> refusals;
	int bridgeCount = -1;
	int bridgeOmitted = 0;
	int bridgeLines = 0;
	int consoleHeaders = 0;
	int consoleCount = -1;
	int consoleOmitted = 0;
	int consoleEntries = 0;
	int consoleContinuations = 0;
	int errorsClipped = 0;
	int hosts = 0;
	bool consoleDropped = false;
};

// Pure. |summary| is one Summarize() text. Its bridge section runs from the
// last line that is exactly "Bridge calls: N." to the end: the formatter's
// "... K earlier entries not shown." line, then one "<method>: <args JSON>"
// line per call, which no newline can split. Its console section is the one
// "Console: N entries[, ...]." line before that header, up to it; an entry's
// text may span lines, and a section whose lines cannot be told apart into
// N - K entries is refused rather than read.
[[nodiscard]] ConformanceSummary ParseConformanceSummary(
	const QString &summary);

// The runner's reading of one run: the raw summary of |log| alone, refused
// as a whole unless it is the Fresh or Final summary of the run's only host:
// |hosts| is the count of factories the run's tap selected, and a last-host
// summary covers no earlier host.
[[nodiscard]] ConformanceSummary ReadConformanceSummary(
	const WidgetRuntimeLog &log,
	int hosts);

// The counts, limits and refusal names of |summary|, for the run's row.
[[nodiscard]] QJsonObject ConformanceSummaryObject(
	const ConformanceSummary &summary);

// Pure controls of the two readers above on literal summaries in
// Summarize()'s format, one inside an outer logText() whose notes carry a
// lookalike bridge section. Row names carry no prefix; the caller turns
// each row into a Check().
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> RunConformanceSummaryCheck();

// The names of |summary|'s refusals that left its bridge calls unread:
// the summary-* and bridge-* ones (console-ambiguous leaves them read).
[[nodiscard]] QStringList ConformanceBridgeRefusals(
	const ConformanceSummary &summary);

// What one run's hosts were observed doing, as the run file states it:
// the accepted calls, or the |bridgeRefusals| that left them unknown (none
// when the summary was never read), the card's end height in CSS px,
// chatFocused with its holder, and whether the input was synthetic.
struct ConformanceObservation {
	std::optional<std::vector<ConformanceBridgeCall>> bridge;
	QStringList bridgeRefusals;
	std::optional<double> height;
	std::optional<bool> chatFocused;
	QString focusHolder;
	bool syntheticInput = false;
};

// A run's run.schema.json "observed", holding only what was observed, and
// its "host", set exactly when the bridge calls are unknown. report.mjs
// counts a missing observed.bridge as no calls, but takes a run's "host"
// in place of judging "observed": there every check of |manifestHost| (the
// manifest entry's "host") is judged as its hostChecks() judges it, except
// that a check counting bridge calls is "skip", "bridge calls not observed
// (...)" naming the refusals, as that function skips an unobserved height.
struct ConformanceHostFields {
	QJsonObject observed;
	std::optional<QJsonArray> host;
};

[[nodiscard]] ConformanceHostFields ConformanceHostFieldsOf(
	const ConformanceObservation &observation,
	const QJsonArray &manifestHost);

// One selected run as the totals' absence judgements read it: whether a
// tap was armed for it, its last tap reading and whether that was taken
// once every host of the run had finished, and its bridge calls or the
// refusals that left them unknown.
struct ConformanceCoverage {
	QString key;
	WidgetOpsReading reading;
	std::optional<std::vector<ConformanceBridgeCall>> bridge;
	QStringList bridgeRefusals;
	bool tapped = false;
	bool final = false;
};

// One absence the totals claim over the selected runs: how many there are,
// how much of the thing was found in any reading, and each run whose
// readings cannot show it absent, "<key>: <why>". |certified| only when a
// run is selected, every one is covered and nothing was found.
struct ConformanceAbsence {
	QStringList uncovered;
	int selected = 0;
	int found = 0;
	bool certified = false;
};

// No URL leaked into the validated ops: each run needs a final reading of
// a tap that wrapped a host, no scan gap and a scanned batch, and the scan
// needs |controls| > 0 control leaks matched (DiscriminatingScan's rule).
// No unscanned host message: each run needs a final reading of a tap that
// wrapped a host. No accepted openTelegramLink call: each run needs its
// bridge calls.
struct ConformanceAbsences {
	ConformanceAbsence leaks;
	ConformanceAbsence gaps;
	ConformanceAbsence telegramLinks;
};

[[nodiscard]] ConformanceAbsences JudgeConformanceAbsences(
	const std::vector<ConformanceCoverage> &runs,
	int controls);

// |absence| with its covered count, for the rows and the run file.
[[nodiscard]] QJsonObject ConformanceAbsenceObject(
	const ConformanceAbsence &absence);

// Pure controls of the three functions above: refused, stale, multi-host,
// clipped, omitted and unread bridge readings against the host checks of
// the pinned manifest's entries, measured empty and exact calls, the
// independent height and focus judgements, and missing, hostless,
// zero-batch, non-final, gapped and bridge-less runs against a good scan
// control. Row names carry no prefix.
[[nodiscard]] auto RunConformanceObservationCheck()
-> std::vector<WidgetOpsSelfCheckRow>;

} // namespace Test
