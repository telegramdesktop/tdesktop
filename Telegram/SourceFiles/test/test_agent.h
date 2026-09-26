/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Test {

// True only in a Debug build launched with -testagent.
[[nodiscard]] bool Active();

// Applies TDESKTOP_TEST_* environment overrides (interface scale).
// Runs before ValidateScale() in Application::run(). No-op unless Active().
void ApplyStartupOverrides();

// Marks a named waitpoint as reached. Fire-once, sticky. No-op unless
// Active(), so call sites in application code need no condition around them.
void Fire(const QString &event);

[[nodiscard]] bool HasFired(const QString &event);

// Builds the scenario registered by test/test_scenario.cpp and starts it on
// the event loop. Runs at the end of Application::run(). No-op unless
// Active(), a scenario is registered, and the portable data folder carries
// the "testing" marker of a disposable test account copy.
void Start();

} // namespace Test
