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

// How often the live runner samples the window and the card on the main
// thread through a level's warm-up and window, and how often its worker
// thread samples the console lock and the covering windows inside it.
inline constexpr auto kObserveEvery = crl::time(500);

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

// The console-lock and covering-window samples a harness worker thread
// took inside one window, every kObserveEvery, off the main thread: the
// endpoints' checks between them. Every stamp is crl::profile().
struct StressCoverSamples {
	// What the first failing sample found, latched; empty: none failed.
	QString failure;
	// That sample's start and its number from 1.
	crl::profile_time failureAt = 0;
	int failureSample = 0;
	// The samples' own time on the worker, summed, and the longest one.
	crl::profile_time cost = 0;
	crl::profile_time maxCost = 0;
	// The main thread's stop and join of the worker, before the end
	// snapshot.
	crl::profile_time joinCost = 0;
	int samples = 0;
	// The samples walked the z-order over the card's native rectangle the
	// window's start read (Windows), not only the console lock.
	bool zOrder = false;
};

// Counts one sample from |started| to |finished| into |samples| and
// latches |failure| unless an earlier failure is latched; an empty
// |failure| is a sample that passed. Pure: the worker calls it on its own
// record, which the main thread reads only after joining it.
void AddCoverSample(
	StressCoverSamples &samples,
	crl::profile_time started,
	crl::profile_time finished,
	const QString &failure);

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
	// The worker's samples inside the window; a latched failure, or no
	// sample in a window of two periods or more, is a runner error.
	StressCoverSamples coverAndLock;
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

// The measured window as the live runner left it after placing it: its
// logical size (the top-level QWidget's), the app's column layout
// ("one-column", "normal" or "three-column") and whether the chat filters
// sidebar takes its width. Empty |layout|: never read.
struct StressWindowLayout {
	QString layout;
	int width = 0;
	int height = 0;
	bool filtersSidebar = false;
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
	StressWindowLayout window;
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

// Merges |reason|, a stop the desktop decided itself (RendererCore's, which
// no host death carries) that a reading first saw at |seenAt|
// (crl::profile()), into |snapshot|'s host as its death. Nothing for an
// empty |reason| or a host dead other than "terminated"; otherwise dead is
// |reason|, deadAt |seenAt| and diedAtMs the estimate loadedMs + (seenAt -
// loadedAt) / 1000, null without both. Pure.
void ApplyDesktopStop(
	StressSnapshot &snapshot,
	const QString &reason,
	crl::profile_time seenAt);

// The run entry of one level, with every runner error the level carries,
// its in-window samples latched and the arithmetic found. Expects the
// level's work log reserved before its host was created and read from 0,
// its values read at or after `end` and its worker joined before `end`.
[[nodiscard]] StressEntry ComputeEntry(const StressLevel &level);

// Why |entries| cannot be accepted for |selection|: not exactly its levels
// in manifest order, or an entry with a runner error. Empty: accepted.
[[nodiscard]] QStringList AcceptanceRefusals(
	const StressManifest &manifest,
	const StressSelection &selection,
	const std::vector<StressEntry> &entries);

// A launch of |selection| as |args| ask, with the machine read now; the
// live runner fills its screen, window, applied options and record.
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
