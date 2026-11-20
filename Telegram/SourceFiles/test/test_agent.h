/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Test {

// True only in a build with the harness (Debug, or Release configured with
// TDESKTOP_RELEASE_TEST_HARNESS) launched with -testagent.
[[nodiscard]] bool Active();

// Applies TDESKTOP_TEST_* environment overrides (interface scale).
// Runs in Application::run() after the settings read, before
// ValidateScale(), the launch URL-scheme check, startDomain() and Start().
// Returns true at once unless Active(), and in a build without the harness.
// Three more selections, each with its own escape, none of them saved:
// - reserves the run's default download folder (see DefaultDownloadFolder()),
//   unless TDESKTOP_TEST_DOWNLOADS=product; when no fresh run folder can be
//   created it logs a setup refusal, also on stderr, and returns false, and
//   the caller quits before the rest of startup;
// - selects SuppressUpdateChecks() unless an earlier call already did;
// - selects AllowAutomaticUrlRegistration().
// Each logs one "NOTE: <NAME>=[<value>] applied: ..." row; any other
// non-empty value is logged as rejected and the default is kept. See
// docs/launch.md, "Downloads, the update checker and URL handlers".
// On macOS it also begins one NSActivityUserInitiatedAllowingIdleSystemSleep
// | NSActivityLatencyCritical activity, held until the process exits (idle
// sleep stays allowed) unless TDESKTOP_TEST_APP_NAP=allow, and logs one
// "TDESKTOP_TEST_APP_NAP=[...] applied: hold=..." row; nothing off macOS.
[[nodiscard]] bool ApplyStartupOverrides();

// The run's default download folder, created fresh by
// ApplyStartupOverrides() under EvidenceDir(): downloads/, or a new
// downloads-XXXXXX/ sibling when that name is taken. Absolute, with a
// trailing slash. Empty before that call, without -testagent, under
// TDESKTOP_TEST_DOWNLOADS=product and in a build without the harness.
// File::DefaultDownloadPath() returns it when it is not empty.
[[nodiscard]] QString DefaultDownloadFolder();

// Whether Core::Updater::start() returns before any update check. False
// without -testagent and in a build without the harness, reading and
// logging nothing. Under -testagent a process decision, selected and logged
// once on the first call here or in ApplyStartupOverrides() (the crash
// window can start the checker first): true unless
// TDESKTOP_TEST_UPDATER=allow.
[[nodiscard]] bool SuppressUpdateChecks();

// Whether Application::autoRegisterUrlScheme() may queue the product's
// automatic registration, on top of the copy's own skip-url-scheme-register
// option. True without -testagent, in a build without the harness and
// before ApplyStartupOverrides() selects it; then true only with
// TDESKTOP_TEST_URL_SCHEME=register. The registertg code is not gated.
[[nodiscard]] bool AllowAutomaticUrlRegistration();

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

// "test_gram_account.txt": the owner's funded golden wallet: the word
// lines before the first empty line, then the address as the app shows
// it. |words| exist only to be typed or compared in process; never copy
// them anywhere. |addressRaw| is the canonical raw form for comparison.
struct GramAccountFixture {
	std::vector<QString> words;
	QString address;
	QString addressRaw;
};
[[nodiscard]] std::optional<GramAccountFixture> GramAccount();

// The two copies GramAccount() reads and the writers below write: the
// marked live copy in the portable folder and the golden sibling. Paths
// only; nothing is opened. Empty in a build without the harness.
[[nodiscard]] QString GramAccountLivePath();
[[nodiscard]] QString GramAccountGoldenPath();

// After a confirmed key rotation: rewrites the word lines of the live copy
// and of the golden sibling, keeping the empty line and the address line.
// Each copy is parsed on its own and rewritten only when its own address,
// normalized, equals |addressRaw| (the rotated wallet's canonical raw
// address); a copy naming another wallet is left untouched and reported
// false. The address line written back is that copy's own. The golden
// write is the one owner-decided exception to the read-only golden folder
// (test/README.md, "Account fixture secrets").
// P0 reconciliation: Test::ReconcileGramAccount (test_gram_reconcile.h).
struct GramAccountRewrite {
	bool live = false;
	bool golden = false;
};
[[nodiscard]] GramAccountRewrite RewriteGramAccountWords(
	const std::vector<QString> &words,
	const QString &addressRaw);

// Self-test only: rewrites the word lines of the LIVE copy alone, and
// only with a proper prefix of that copy's own current words at its own
// address (addressRaw), so a staged "older, shorter" fixture is always the
// anchor half of the phrase the copy held. The golden sibling is never
// touched. Returns whether the live copy was written.
[[nodiscard]] bool StageGramAccountLiveWords(
	const std::vector<QString> &prefix,
	const QString &addressRaw);

// Self-test only: undoes StageGramAccountLiveWords. Rewrites the word lines
// of the LIVE copy alone with |words|, and only when that copy's own current
// words are a proper prefix of |words| at its own address (addressRaw), so
// it can only lengthen a staged copy back to the phrase it was cut from.
// The golden sibling is never touched. Returns whether the live copy was
// written.
[[nodiscard]] bool RestoreGramAccountLiveWords(
	const std::vector<QString> &words,
	const QString &addressRaw);

// Builds the scenario registered by test/test_scenario.cpp and starts it on
// the event loop. Runs at the end of Application::run(). No-op unless
// Active(), a scenario is registered, and the portable data folder carries
// the "testing" marker of a disposable test account copy.
void Start();

} // namespace Test
