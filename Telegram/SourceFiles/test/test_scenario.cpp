/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_runner.h"

namespace Test {

// Per-task overlay slot: an automated test overlay replaces this whole file
// with a scenario using the helper catalog and contracts in test/README.md.
// The repository copy must stay a no-op.
void SetupScenario(not_null<Runner*> runner) {
}

} // namespace Test

#endif // _DEBUG
