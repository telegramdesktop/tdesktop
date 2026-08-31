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

// Reads one fixture secret the test account carries beside its tdata and
// returns its whitespace-trimmed contents. The live portable folder is read
// first (SETUP deep-copies the golden folder into it), then the golden
// test_TelegramForcePortable sibling, so a file added to the golden folder
// after a marked live copy was made is still found. Nullopt when the file is
// absent or blank, and always outside test-agent mode. The value exists only
// to be typed into the product's own fields at runtime: never copy it into
// overlay code, work/ or evidence/ artifacts, logs, notes, or prompts.
[[nodiscard]] std::optional<QString> FixtureSecret(const QString &name);

// "2svpassword.txt": the test account's two-step-verification (cloud)
// password. Feed it to the real PasscodeBox so the product computes the SRP
// proof. A check that needs it gates the file's presence before the scenario
// exists and blocks the task when it is absent; reached at runtime anyway, a
// missing file is a named fixture gate, never a product FAIL.
[[nodiscard]] std::optional<QString> TwoStepPassword();

// Builds the scenario registered by test/test_scenario.cpp and starts it on
// the event loop. Runs at the end of Application::run(). No-op unless
// Active(), a scenario is registered, and the portable data folder carries
// the "testing" marker of a disposable test account copy.
void Start();

} // namespace Test
