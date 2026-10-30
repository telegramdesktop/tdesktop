/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "test/test_widget_host.h"
#include "test/test_widget_ops.h"
#include "test/test_widget_stress.h"

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QRect>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <crl/crl_time.h>

#include <optional>
#include <vector>

// The widget stress runner's shared model, used by its two translation
// units: test_widget_stress.cpp (the kit, each level's entry, the envelope,
// the merge and the self-check) and test_widget_stress_live.cpp (the live
// stages that fill them). Internal to them; reference:
// docs/helpers/test_widget_stress.md.

namespace Test::Stress {

// CLIENTS.md's theme: requested, and the scheme a level's renderer must get.
inline constexpr auto kTheme = "light";

// ---------------------------------------------------------------------
// The kit.

// One manifest.widgets entry, with its HTML once a selection read it.
struct StressWidget {
	QString id;
	QString load;
	QString file;
	QStringList crosses;
	QByteArray html;
	double value = 0.;
	int level = 0;
	// Its position in manifest.widgets: the run file's order.
	int index = 0;
};

struct StressLoad {
	QString id;
	QStringList widgets;
};

struct StressManifest {
	std::vector<StressWidget> widgets;
	std::vector<StressLoad> loads;
	crl::time warmupMs = 0;
	crl::time measureMs = 0;
	int version = 0;
	int width = 0;
};

// The launched loads and their widgets, both in manifest order; the
// widgets as indices into StressManifest::widgets.
struct StressSelection {
	QStringList loads;
	std::vector<int> widgets;
};

struct StressPackage {
	StressManifest manifest;
	StressSelection selection;
	QString error;
};

// ---------------------------------------------------------------------
// One level's readings and entry.

// One end of a measuring window, or an unmeasured level's final reading:
// the measured host's record and the tap's newest-host counts, read at
// `at` (crl::profile()).
struct StressSnapshot {
	WidgetHostSample host;
	crl::profile_time at = 0;
	int64 ops = 0;
	int batches = 0;
	int maxBatchOps = 0;
	int generation = 0;
	// The watch had the measured attachment: `host` is its record.
	bool attached = false;
};

// What the live runner observed of one level.
struct StressLevel {
	QString id;
	QString load;
	double value = 0.;
	int index = 0;
	// The window's start snapshot when the window ran. A level that never
	// loaded or died before its window has none, and `end` is its final
	// reading.
	std::optional<StressSnapshot> start;
	StressSnapshot end;
	// QEvent::UpdateRequest stamps of the exact top-level window showing
	// the card, crl::profile(); only those inside the window count.
	std::vector<crl::profile_time> updates;
	// The level's whole work log: ReadWidgetWork() since 0, reserved for
	// this level before its host was created.
	WidgetWorkReading work;
	// The tap's data-stress values and drops, read at or after `end`.
	std::vector<WidgetStressValue> values;
	int valuesDropped = 0;
	// The measured attachment: its index in WidgetHostReading::hosts.
	int host = 0;
	// The fixture, timeout and instrument failures the live runner named.
	QStringList runnerErrors;
	// The live runner's own readings, for the entry's desktop extras.
	QJsonObject desktop;
};

struct StressEntry {
	QJsonObject object;
	// Every named failure of the runner: the live runner's and the
	// arithmetic's. A launch with any is not accepted.
	QStringList runnerErrors;
};

// ---------------------------------------------------------------------
// The launch and its run file.

// The screen the measured window is on, and the scales the card renders
// with, as the live runner read them after placing the window.
struct StressScreen {
	QString name;
	QRect geometry;
	QString surface;
	double refreshHz = 0.;
	double windowDpr = 0.;
	double coreRatio = 0.;
	int uiScale = 0;
};

// What the levels' hosts were built with and what the card showed.
struct StressApplied {
	WidgetHostApplied options;
	QString theme;
	double cardCssWidth = 0.;
};

struct StressMachine {
	QString model;
	QString os;
	std::optional<int64> ramMb;
};

struct StressLaunch {
	QString id;
	QString date;
	QString framework;
	QString engine;
	QString buildNote;
	QStringList notes;
	QStringList loads;
	StressMachine machine;
	StressScreen screen;
	std::optional<StressApplied> applied;
	// The live runner's own readings for this launch's record.
	QJsonObject record;
	crl::time warmupMs = 0;
	crl::time measureMs = 0;
	crl::time manifestWarmupMs = 0;
	crl::time manifestMeasureMs = 0;
	int kitVersion = 0;
	int width = 0;
	WidgetStressPass pass = WidgetStressPass::Measure;
};

struct StressPublication {
	// The file written: the run file when merged, else the launch file;
	// empty when neither could be written.
	QString path;
	// Why the launch did not reach the run file; empty when it did.
	QString error;
	bool merged = false;
};

// "widget stress: " + |text|: every stage name, Check and Note.
[[nodiscard]] QString Prefixed(const QString &text);

// "measure" or "production".
[[nodiscard]] QString PassName(WidgetStressPass pass);

// The arguments, manifest.json and the selected widgets' HTML, read once
// while the stages are registered; |error| names every problem found.
[[nodiscard]] StressPackage ReadStressPackage(const WidgetStressArgs &args);

// What each level's watch overrides: CLIENTS.md's measure table, or
// nothing for the production pass (an empty override only watches).
[[nodiscard]] WidgetHostOverride HostOverride(WidgetStressPass pass);

// A window end of |host|, the index in |reading|.hosts of the measured
// attachment (not attached when there is none), with the tap's counts of
// |latest|, stamped |at|. Read the host and the tap before taking |at|.
[[nodiscard]] StressSnapshot MakeSnapshot(
	const WidgetHostReading &reading,
	int host,
	const WidgetOpsLatest &latest,
	crl::profile_time at);

// The run entry of one level, with every runner error the level carries
// and the arithmetic found. Expects the level's work log reserved before
// its host was created and read from 0, and its values read at or after
// `end`.
[[nodiscard]] StressEntry ComputeEntry(const StressLevel &level);

// Why |entries| cannot be accepted for |selection|: not exactly its levels
// in manifest order, or an entry with a runner error. Empty: accepted.
[[nodiscard]] QStringList AcceptanceRefusals(
	const StressManifest &manifest,
	const StressSelection &selection,
	const std::vector<StressEntry> &entries);

// A launch of |selection| as |args| ask, with the machine read now; the
// live runner fills its screen, applied options and record.
[[nodiscard]] StressLaunch MakeLaunch(
	const WidgetStressArgs &args,
	const StressManifest &manifest,
	const StressSelection &selection);

// Merges a launch without |refusals| into the run file at |output|; any
// other launch, or one the merge or the write refuses, goes to its launch
// file (<output>.launch-<loads>.json) with every reason instead, and the
// run file stays as it was.
[[nodiscard]] StressPublication PublishLaunch(
	const QString &output,
	const StressManifest &manifest,
	const StressLaunch &launch,
	const std::vector<StressEntry> &entries,
	QStringList refusals);

} // namespace Test::Stress
