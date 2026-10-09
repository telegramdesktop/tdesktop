/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_agent.h"

#ifdef _DEBUG

#include "test/test_log.h"
#include "ui/style/style_core_scale.h"
#include "wallet/wallet_address.h"
#include "settings.h"

#include <QtCore/QFile>
#include <QtCore/QSaveFile>

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

} // namespace

bool Active() {
	return cTestAgent();
}

void ApplyStartupOverrides() {
	if (!Active()) {
		return;
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

#else // _DEBUG

namespace Test {

bool Active() {
	return false;
}

void ApplyStartupOverrides() {
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

#endif // _DEBUG
