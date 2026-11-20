/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_agent.h"

#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "base/options.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/version.h"
#include "storage/localstorage.h"
#include "test/test_log.h"
#include "ui/style/style_core_scale.h"
#include "wallet/wallet_address.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSaveFile>
#include <QtCore/QTemporaryDir>

#include <cstdio>

#ifdef Q_OS_MACOS
// Keep last: <objc/objc.h> defines id, Class, BOOL, YES, NO, nil and Nil.
#include <objc/message.h>
#include <objc/runtime.h>
#endif // Q_OS_MACOS

namespace Test {
namespace {

const auto kGramAccountFile = u"test_gram_account.txt"_q;

[[nodiscard]] QString LivePath(const QString &name) {
	return cWorkingDir() + name;
}

[[nodiscard]] QString GoldenPath(const QString &name) {
	return cExeDir() + u"test_TelegramForcePortable/"_q + name;
}

[[nodiscard]] std::optional<QString> ReadTrimmed(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	const auto value = QString::fromUtf8(file.readAll()).trimmed();
	if (value.isEmpty()) {
		return std::nullopt;
	}
	return value;
}

[[nodiscard]] std::optional<GramAccountFixture> ParseGramAccount(
		const QString &raw) {
	auto result = GramAccountFixture();
	auto wordsEnded = false;
	for (const auto &line : raw.split(u'\n')) {
		const auto trimmed = line.trimmed();
		if (!wordsEnded) {
			if (trimmed.isEmpty()) {
				wordsEnded = true;
			} else {
				result.words.push_back(trimmed);
			}
		} else if (!trimmed.isEmpty()) {
			result.address = trimmed;
			break;
		}
	}
	if (result.words.empty() || result.address.isEmpty()) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] bool IsWordLine(const QString &word) {
	if (word.isEmpty()) {
		return false;
	}
	for (const auto ch : word) {
		if (ch.isSpace()) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool RewriteExisting(
		const QString &path,
		const QByteArray &content) {
	if (!QFile::exists(path)) {
		return false;
	}
	auto file = QSaveFile(path);
	return file.open(QIODevice::WriteOnly)
		&& (file.write(content) == content.size())
		&& file.commit();
}

[[nodiscard]] bool RewriteCopyWords(
		const QString &path,
		const QStringList &lines,
		const QString &addressRaw) {
	const auto raw = ReadTrimmed(path);
	const auto current = raw ? ParseGramAccount(*raw) : std::nullopt;
	if (!current
		|| Wallet::CanonicalAddress(current->address) != addressRaw) {
		return false;
	}
	const auto content = (lines.join(u'\n')
		+ u"\n\n"_q
		+ current->address
		+ u"\n"_q).toUtf8();
	return RewriteExisting(path, content);
}

[[nodiscard]] base::flat_set<QString> &FiredEvents() {
	static auto result = base::flat_set<QString>();
	return result;
}

// The run's default download folder: reserved by ApplyDownloadFolder()
// unless TDESKTOP_TEST_DOWNLOADS=product, returned by DefaultDownloadFolder()
// and never saved.
auto RunDownloadFolder = QString();

// Returned by AllowAutomaticUrlRegistration(). Permissive until an active
// ApplyUrlSchemeRegistration() selects it, so a call before that selection
// never blocks the product's registration.
auto UrlRegistrationAllowed = true;

// The process's update-check decision, empty until SelectUpdateChecks()
// selects it once; true suppresses every Core::Updater::start().
auto UpdateChecksSuppressed = std::optional<bool>();

struct StartupSwitch {
	QString value;
	QString source;
	bool escape = false;
};

// Reads one TDESKTOP_TEST_* startup switch: only |token| selects its escape
// (source=environment); any other non-empty value is logged as rejected and
// the default is kept (source=default), as is an empty one.
[[nodiscard]] StartupSwitch ReadStartupSwitch(
		const char name[],
		const QString &token) {
	const auto value = qEnvironmentVariable(name);
	const auto escape = (value == token);
	if (!escape && !value.isEmpty()) {
		Note(u"%1 rejected: %2"_q.arg(QString::fromLatin1(name), value));
	}
	return StartupSwitch{
		.value = value,
		.source = escape ? u"environment"_q : u"default"_q,
		.escape = escape,
	};
}

// Reserves a fresh run-local download folder by creating it under
// EvidenceDir(): the "downloads" child when nothing exists under that name
// yet, otherwise a new "downloads-XXXXXX" sibling, kept after exit. An entry
// already there (a folder, a file or a junction a prepared run directory
// left) is never reused. Returns the absolute folder with a trailing slash,
// or an empty string when neither can be created.
[[nodiscard]] QString ReserveRunDownloadFolder() {
	const auto root = EvidenceDir();
	if (QDir().mkdir(root + u"downloads"_q)) {
		return root + u"downloads/"_q;
	}
	auto sibling = QTemporaryDir(root + u"downloads-XXXXXX"_q);
	if (!sibling.isValid()) {
		return QString();
	}
	sibling.setAutoRemove(false);
	return sibling.path() + u"/"_q;
}

// TDESKTOP_TEST_DOWNLOADS=product keeps the product's default download
// folder and reserves nothing, for a campaign whose subject is that folder;
// otherwise the folder ReserveRunDownloadFolder() creates is the default
// folder. It applies only where the copy's stored path is empty and
// ask-for-path is off. The setting itself is never touched. Writes exactly
// one row: the applied one, or a setup refusal, also on stderr, when no
// fresh run folder can be created; then it returns false, so startup stops
// instead of saving into the product's folder.
[[nodiscard]] bool ApplyDownloadFolder() {
	const auto selected = ReadStartupSwitch(
		"TDESKTOP_TEST_DOWNLOADS",
		u"product"_q);
	if (!selected.escape) {
		RunDownloadFolder = ReserveRunDownloadFolder();
		if (RunDownloadFolder.isEmpty()) {
			const auto refusal = u"TDESKTOP_TEST_DOWNLOADS=[%1] setup refused:"
				" no fresh run download folder could be created under %2"
				" - startup stops"_q.arg(selected.value, EvidenceDir());
			Note(refusal);
			std::fprintf(stderr, "%s\n", refusal.toUtf8().constData());
			std::fflush(stderr);
			return false;
		}
	}
	const auto &settings = Core::App().settings();
	const auto stored = settings.downloadPath();
	const auto ask = settings.askDownloadPath();
	const auto first = ask
		? u"the ask-for-path prompt"_q
		: !stored.isEmpty()
		? u"the copy's stored path"_q
		: QString();
	const auto child = EvidenceDir() + u"downloads/"_q;
	const auto folder = (RunDownloadFolder == child)
		? u"the run folder %1"_q.arg(RunDownloadFolder)
		: u"the run folder %1 (%2 could not be created fresh)"_q.arg(
			RunDownloadFolder,
			child);
	const auto reason = selected.escape
		? u"the product's default folder"_q
		: first.isEmpty()
		? folder
		: u"%1, but %2 decides first"_q.arg(folder, first);
	const auto report = u"TDESKTOP_TEST_DOWNLOADS=[%1] applied: "
		"default-folder=%2 source=%3 - %4; stored path [%5] ask=%6"_q.arg(
			selected.value,
			selected.escape ? u"product"_q : u"run"_q,
			selected.source,
			reason,
			stored,
			ask ? u"1"_q : u"0"_q);
	Note(report);
	return true;
}

// TDESKTOP_TEST_UPDATER=allow leaves update checks to the product, for a
// campaign whose subject is the updater; otherwise every
// Core::Updater::start() of this process returns before it computes a
// delay, probes for an update file, starts a checker or arms its timer.
// Selected once per process, on the first SuppressUpdateChecks() or
// ApplyStartupOverrides() call (the crash window can start the checker
// before Application::run()), which logs the rejection row, if any, and the
// one applied row. Kept in memory only; nothing is saved.
void SelectUpdateChecks() {
	if (UpdateChecksSuppressed.has_value()) {
		return;
	}
	const auto selected = ReadStartupSwitch(
		"TDESKTOP_TEST_UPDATER",
		u"allow"_q);
	UpdateChecksSuppressed = !selected.escape;
	const auto reason = selected.escape
		? u"the product decides whether to check"_q
		: u"update checks suppressed for this process"_q;
	const auto report = u"TDESKTOP_TEST_UPDATER=[%1] applied: "
		"checker=%2 source=%3 - %4"_q.arg(
			selected.value,
			selected.escape ? u"product"_q : u"off"_q,
			selected.source,
			reason);
	Note(report);
}

// TDESKTOP_TEST_URL_SCHEME=register lets Application::autoRegisterUrlScheme()
// register the tg, tonsite and ton handlers whenever the product would, for
// a campaign whose subject is the registration; otherwise the launch's
// automatic registration is skipped. The copy's skip-url-scheme-register
// option is only read, never set. Writes exactly one reading row.
void ApplyUrlSchemeRegistration() {
	const auto selected = ReadStartupSwitch(
		"TDESKTOP_TEST_URL_SCHEME",
		u"register"_q);
	UrlRegistrationAllowed = selected.escape;
	const auto skip = base::options::lookup<bool>(
		Core::kOptionSkipUrlSchemeRegister).value();
	const auto due = (Local::oldSettingsVersion() < AppVersion);
	const auto reason = selected.escape
		? u"the product registers tg, tonsite and ton when due"_q
		: u"the launch's automatic registration is skipped"_q;
	const auto report = u"TDESKTOP_TEST_URL_SCHEME=[%1] applied: "
		"registration=%2 source=%3 - %4; due=%5"
		" skip-url-scheme-register=%6"_q.arg(
			selected.value,
			selected.escape ? u"product"_q : u"off"_q,
			selected.source,
			reason,
			due ? u"yes"_q : u"no"_q,
			skip ? u"on"_q : u"off"_q);
	Note(report);
}

#ifdef Q_OS_MACOS

// NSActivityOptions values from Foundation's NSProcessInfo.h, which a C++
// unit cannot include. NSActivityUserInitiatedAllowingIdleSystemSleep is
// NSActivityUserInitiated without NSActivityIdleSystemSleepDisabled, so idle
// system sleep stays allowed; NSActivityLatencyCritical asks for the highest
// timer and I/O precision available. Held together, they keep a background
// client from being napped and its timers from being coalesced.
// kAppNapHoldOptions is 0xFF00EFFFFF, the SDK's
// NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical.
constexpr auto kActivityUserInitiatedAllowingIdleSystemSleep = 0x00EFFFFFULL;
constexpr auto kActivityLatencyCritical = 0xFF00000000ULL;
constexpr auto kAppNapHoldOptions
	= kActivityUserInitiatedAllowingIdleSystemSleep
	| kActivityLatencyCritical;

// The -beginActivityWithOptions:reason: token of the harness hold, retained
// and never ended or released, so the activity lasts until the process
// exits: normal completion, a stage timeout, the watchdog, the quit fuse, a
// test-run kill and a crash alike. Ending it in Runner::onFinish would end it
// before the drain and the quit teardown.
auto AppNapActivity = (void*)nullptr;

// Runs on the main thread inside the Cocoa event loop (the InvokeQueued of
// Sandbox::launchApplication() that calls Application::run()), so the event
// loop's pool owns the NSString and the token as they are returned; the token
// outlives that pool because it is retained here. Returns an empty string
// once the activity is held, otherwise why it is not.
[[nodiscard]] QString BeginAppNapActivity() {
	using SendObject = void*(*)(void*, SEL);
	using SendString = void*(*)(void*, SEL, const char*);
	using SendBegin = void*(*)(void*, SEL, unsigned long long, void*);
	const auto cls = objc_getClass("NSProcessInfo");
	if (!cls) {
		return u"NSProcessInfo is unavailable"_q;
	}
	const auto info = reinterpret_cast<SendObject>(objc_msgSend)(
		cls,
		sel_registerName("processInfo"));
	const auto reason = reinterpret_cast<SendString>(objc_msgSend)(
		objc_getClass("NSString"),
		sel_registerName("stringWithUTF8String:"),
		"Telegram Desktop -testagent run");
	const auto activity = reinterpret_cast<SendBegin>(objc_msgSend)(
		info,
		sel_registerName("beginActivityWithOptions:reason:"),
		kAppNapHoldOptions,
		reason);
	if (!activity) {
		return u"beginActivityWithOptions:reason: returned nil"_q;
	}
	reinterpret_cast<SendObject>(objc_msgSend)(
		activity,
		sel_registerName("retain"));
	AppNapActivity = activity;
	return QString();
}

// TDESKTOP_TEST_APP_NAP=allow leaves the hold off, for a campaign whose
// subject is background scheduling itself; any other non-empty value is
// rejected and the hold is kept. Writes exactly one reading row.
void ApplyAppNapHold() {
	const auto selected = ReadStartupSwitch(
		"TDESKTOP_TEST_APP_NAP",
		u"allow"_q);
	const auto optOut = selected.escape;
	const auto failure = optOut ? QString() : BeginAppNapActivity();
	const auto held = !optOut && failure.isEmpty();
	const auto reason = optOut
		? u"opted out: macOS may nap this background client"
			" and coalesce its timers"_q
		: held
		? u"NSProcessInfo activity"
			" NSActivityUserInitiatedAllowingIdleSystemSleep"
			" | NSActivityLatencyCritical (options 0x%1)"
			" held until the process exits"_q.arg(
				QString::number(kAppNapHoldOptions, 16))
		: failure;
	const auto report = u"TDESKTOP_TEST_APP_NAP=[%1] applied: "
		"hold=%2 source=%3 - %4"_q.arg(
			selected.value,
			held ? u"active"_q : u"off"_q,
			selected.source,
			reason);
	Note(report);
}

#endif // Q_OS_MACOS

} // namespace

bool Active() {
	return cTestAgent();
}

bool ApplyStartupOverrides() {
	if (!Active()) {
		return true;
	}
	const auto value = qEnvironmentVariable("TDESKTOP_TEST_SCALE");
	auto selectedScale = style::kScaleDefault;
	auto source = u"default"_q;
	if (!value.isEmpty()) {
		auto ok = false;
		const auto scale = value.toInt(&ok);
		if (ok && scale >= style::kScaleMin && scale <= style::kScaleMax) {
			selectedScale = style::CheckScale(scale);
			source = u"environment"_q;
		} else {
			Note(u"TDESKTOP_TEST_SCALE rejected: %1"_q.arg(value));
		}
	}
	cSetConfigScale(selectedScale);
	const auto report = u"TDESKTOP_TEST_SCALE=[%1] applied: %2 source=%3"_q
		.arg(
			value,
			QString::number(selectedScale),
			source);
	Note(report);
	if (!ApplyDownloadFolder()) {
		return false;
	}
	SelectUpdateChecks();
	ApplyUrlSchemeRegistration();
#ifdef Q_OS_MACOS
	ApplyAppNapHold();
#endif // Q_OS_MACOS
	return true;
}

QString DefaultDownloadFolder() {
	return RunDownloadFolder;
}

bool SuppressUpdateChecks() {
	if (!Active()) {
		return false;
	}
	SelectUpdateChecks();
	return *UpdateChecksSuppressed;
}

bool AllowAutomaticUrlRegistration() {
	return UrlRegistrationAllowed;
}

void Fire(const QString &event) {
	if (!Active() || !FiredEvents().emplace(event).second) {
		return;
	}
	Note(u"event fired: %1"_q.arg(event));
}

bool HasFired(const QString &event) {
	return Active() && FiredEvents().contains(event);
}

std::optional<QString> FixtureSecret(const QString &name) {
	if (!Active()) {
		return std::nullopt;
	} else if (auto live = ReadTrimmed(LivePath(name))) {
		return live;
	}
	return ReadTrimmed(GoldenPath(name));
}

std::optional<QString> TwoStepPassword() {
	return FixtureSecret(u"2svpassword.txt"_q);
}

QString GramAccountLivePath() {
	return LivePath(kGramAccountFile);
}

QString GramAccountGoldenPath() {
	return GoldenPath(kGramAccountFile);
}

std::optional<GramAccountFixture> GramAccount() {
	const auto raw = FixtureSecret(kGramAccountFile);
	if (!raw) {
		return std::nullopt;
	}
	auto result = ParseGramAccount(*raw);
	if (result) {
		result->addressRaw = Wallet::CanonicalAddress(result->address);
	}
	return result;
}

GramAccountRewrite RewriteGramAccountWords(
		const std::vector<QString> &words,
		const QString &addressRaw) {
	auto result = GramAccountRewrite();
	if (!Active()
		|| words.empty()
		|| addressRaw.isEmpty()
		|| !ranges::all_of(words, IsWordLine)) {
		return result;
	}
	const auto lines = QStringList(words.begin(), words.end());
	result.live = RewriteCopyWords(GramAccountLivePath(), lines, addressRaw);
	result.golden = RewriteCopyWords(
		GramAccountGoldenPath(),
		lines,
		addressRaw);
	return result;
}

bool StageGramAccountLiveWords(
		const std::vector<QString> &prefix,
		const QString &addressRaw) {
	if (!Active()
		|| prefix.empty()
		|| addressRaw.isEmpty()
		|| !ranges::all_of(prefix, IsWordLine)) {
		return false;
	}
	const auto path = GramAccountLivePath();
	const auto raw = ReadTrimmed(path);
	const auto current = raw ? ParseGramAccount(*raw) : std::nullopt;
	if (!current
		|| Wallet::CanonicalAddress(current->address) != addressRaw
		|| prefix.size() >= current->words.size()
		|| !std::equal(
			prefix.begin(),
			prefix.end(),
			current->words.begin())) {
		return false;
	}
	const auto lines = QStringList(prefix.begin(), prefix.end());
	return RewriteCopyWords(path, lines, addressRaw);
}

bool RestoreGramAccountLiveWords(
		const std::vector<QString> &words,
		const QString &addressRaw) {
	if (!Active()
		|| words.empty()
		|| addressRaw.isEmpty()
		|| !ranges::all_of(words, IsWordLine)) {
		return false;
	}
	const auto path = GramAccountLivePath();
	const auto raw = ReadTrimmed(path);
	const auto current = raw ? ParseGramAccount(*raw) : std::nullopt;
	if (!current
		|| Wallet::CanonicalAddress(current->address) != addressRaw
		|| current->words.size() >= words.size()
		|| !std::equal(
			current->words.begin(),
			current->words.end(),
			words.begin())) {
		return false;
	}
	const auto lines = QStringList(words.begin(), words.end());
	return RewriteCopyWords(path, lines, addressRaw);
}

} // namespace Test

#else // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS

namespace Test {

bool Active() {
	return false;
}

bool ApplyStartupOverrides() {
	return true;
}

QString DefaultDownloadFolder() {
	return QString();
}

bool SuppressUpdateChecks() {
	return false;
}

bool AllowAutomaticUrlRegistration() {
	return true;
}

void Fire(const QString &) {
}

bool HasFired(const QString &) {
	return false;
}

std::optional<QString> FixtureSecret(const QString &) {
	return std::nullopt;
}

std::optional<QString> TwoStepPassword() {
	return std::nullopt;
}

std::optional<GramAccountFixture> GramAccount() {
	return std::nullopt;
}

QString GramAccountLivePath() {
	return QString();
}

QString GramAccountGoldenPath() {
	return QString();
}

GramAccountRewrite RewriteGramAccountWords(
		const std::vector<QString> &,
		const QString &) {
	return {};
}

bool StageGramAccountLiveWords(
		const std::vector<QString> &,
		const QString &) {
	return false;
}

bool RestoreGramAccountLiveWords(
		const std::vector<QString> &,
		const QString &) {
	return false;
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
