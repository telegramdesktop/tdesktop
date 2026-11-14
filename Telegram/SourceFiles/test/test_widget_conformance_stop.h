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
// entry's run whose death has neither that reason nor its host's. Internal
// to test_widget_conformance.cpp; reference:
// docs/helpers/test_widget_conformance.md.

namespace Test {
struct ConformanceRefusal;
struct WidgetFixture;
struct WidgetFixtureMessage;
} // namespace Test

namespace Test::Conformance {

// In the log stage, for a run whose host summary gave no "Died: " reason
// other than "terminated": WidgetDesktopStop() of |message| for the core
// of its host watch, read only when that watch has exactly one attachment,
// cut as Clip() of tgw_runtime.cpp cuts a summary's dead (240 characters,
// its "..." included). Empty when the desktop did not stop the run's own
// core. Only reads.
[[nodiscard]] QString ReadDesktopStop(
	const WidgetFixture &fixture,
	const WidgetFixtureMessage &message,
	int fatalFrozen);

// A run of a stops entry that died without the reason ReadDeath() takes,
// a "Died: " line other than "terminated", or a stop the desktop decided
// itself (|desktopStop|): its dead would be the runner's placeholder,
// never the stop reason that report.mjs judges.
[[nodiscard]] ConformanceRefusal StopRefusal(
	const QString &stops,
	bool died,
	const QString &deathLine,
	bool desktopStop);

// Pure StopRefusal() controls on a stops run that died with only "Died:
// terminated": none with a stop the desktop decided itself and
// stop-reason-unmeasured without one. CheckHostReadings() of
// test_widget_conformance_hosts.cpp holds its other cases. Row names carry
// no prefix.
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> RunDesktopStopCheck();

} // namespace Test::Conformance
