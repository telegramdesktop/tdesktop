/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "test/test_widget_ops.h"

#include <QtCore/QString>

#include <vector>

// The widget conformance runner's reading of a stop the desktop decided
// itself, such as a paint bound's refusal of the widget's frames, which no
// host death carries, and the stop-reason-unmeasured refusal of a stops
// entry's run whose death has no measured reason. Internal to
// test_widget_conformance.cpp; reference:
// docs/helpers/test_widget_conformance.md.

namespace Test {
struct ConformanceRefusal;
struct WidgetFixture;
struct WidgetFixtureMessage;
} // namespace Test

namespace Test::Conformance {

struct ConformanceRun;

// The name of StopRefusal()'s refusal.
inline constexpr auto kStopReasonUnmeasured = "stop-reason-unmeasured";

// In the log stage: WidgetDesktopStop() of |message| for the core of its
// host watch, read only when that watch has exactly one attachment, cut by
// Tgw::ClipLogLine() as a summary's dead is. Empty when the desktop did
// not stop the run's own core. ReadDeath() of
// test_widget_conformance_death.cpp takes it only locally, for a death it
// measured no reason of. Only reads.
[[nodiscard]] QString ReadDesktopStop(
	const WidgetFixture &fixture,
	const WidgetFixtureMessage &message,
	int fatalFrozen);

// |run| of the stops entry |stops| died without a measured reason
// (`deathMeasured`: the reason ReadDeath() of
// test_widget_conformance_death.cpp took, a "Died: " line's other than
// "terminated", a hosted sample's own cause, or a stop the desktop decided
// itself): its dead would be the runner's placeholder, never the stop
// reason that report.mjs judges. The detail tells what the run read, each
// apart: its raw `deathLine`; for a hosted reading (`deathHosted`) that
// its sample gave no attributable cause, with that summary's eligibility
// (`summaryOwn`) and work (`summaryWork`); and the desktop's own stop
// (`desktopStop`), named when one was read, which gives a hosted widget no
// cause, and said not to have happened only when none was.
[[nodiscard]] ConformanceRefusal StopRefusal(
	const QString &stops,
	const ConformanceRun &run);

// StopRefusal() controls on a stops run that died with only "Died:
// terminated", through ReadDeath()'s local reading: none with a stop the
// desktop decided itself and stop-reason-unmeasured without one.
// CheckHostReadings() of test_widget_conformance_hosts.cpp holds its other
// cases. Row names carry no prefix.
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> RunDesktopStopCheck();

} // namespace Test::Conformance
