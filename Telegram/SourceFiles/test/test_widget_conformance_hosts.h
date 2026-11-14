/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "test/test_widget_ops.h"

#include <QtCore/QJsonObject>

#include <vector>

// The widget conformance runner's readings of the content safe area each
// construction of a run's host got, their refusals, and the pure host
// reading controls of those refusals, StopRefusal() and the result wait.
// Internal to test_widget_conformance.cpp; reference:
// docs/helpers/test_widget_conformance.md.

namespace Test {
struct ConformanceRefusal;
} // namespace Test

namespace twidget {
struct WidgetContentSafeArea;
} // namespace twidget

namespace Test::Conformance {

struct ConformanceRun;

// The safeArea extra of a run whose host watch was read: never the
// schema's own host key.
[[nodiscard]] QJsonObject SafeAreaObject(const ConformanceRun &run);

// A captioned run's host must get CLIENTS.md's all 0 in every
// construction, and a loaded run needs a reading at all; a safeArea entry's
// band is whatever the desktop passes, so it is recorded, never refused.
[[nodiscard]] ConformanceRefusal SafeAreaRefusal(
	bool safeAreaEntry,
	const std::vector<twidget::WidgetContentSafeArea> &applied,
	bool loaded);

// The content safe area each armed construction of the run's host got, in
// order; nothing for a run whose message got no watch.
void ReadSafeAreas(ConformanceRun &run);

// Pure SafeAreaRefusal(), StopRefusal() and WantedStatus() controls on
// literal readings: the refusals' positive cases, which the live runs never
// show, and CLIENTS.md's death wait for a stops entry at done.
// Its row's name is Prefixed().
[[nodiscard]] WidgetOpsSelfCheckRow CheckHostReadings();

} // namespace Test::Conformance
