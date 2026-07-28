/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_runner.h"

namespace Test {

// Per-task overlay slot: an automated test overlay replaces this whole file
// with a scenario built on test_runner.h / test_widgets.h / test_capture.h /
// test_log.h. The repository copy must stay a no-op.
void SetupScenario(not_null<Runner*> runner) {
}

} // namespace Test
