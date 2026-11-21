/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>

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
// test_widget_conformance_death.cpp keeps it in every run's `desktopStop`,
// where it gives no death a cause and StopRefusal()'s detail names it.
// Only reads.
[[nodiscard]] QString ReadDesktopStop(
	const WidgetFixture &fixture,
	const WidgetFixtureMessage &message,
	int fatalFrozen);

// |run| of the stops entry |stops| died without a measured reason
// (`deathMeasured`: the reason ReadDeath() or ReconcileDeath() of
// test_widget_conformance_death.cpp took, a hosted sample's own cause):
// its dead would be the runner's placeholder, never the stop reason that
// report.mjs judges. Such a death is always their hosted reading of a
// charge of the run's selected works, as a run whose tap selected no
// hosted work is never dead when they read it. The detail tells what the
// run read, each apart: its raw `deathLine`; that its sample gave no
// attributable cause, with that summary's eligibility (`summaryOwn`) and
// work (`summaryWork`); and the desktop's own stop (`desktopStop`), named
// when one was read, which gives a hosted widget no cause, and when none
// was, only unobserved beside the charge, never denied, as the desktop's
// charge of a selected work may still stand.
[[nodiscard]] ConformanceRefusal StopRefusal(
	const QString &stops,
	const ConformanceRun &run);

} // namespace Test::Conformance
